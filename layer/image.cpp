/* Copyright (c) 2017, ARM Limited and Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "image.hpp"
#include "device.hpp"
#include "device_memory.hpp"
#include "message_codes.hpp"
#include <algorithm>

namespace MPD
{
VkResult Image::init(VkImage image_, const VkImageCreateInfo &createInfo_)
{
	image = image_;
	createInfo = createInfo_;

	arrayLayers.resize(createInfo.arrayLayers);
	for (auto &layer : arrayLayers)
		layer.mipLevels.resize(createInfo.mipLevels);

	// Static casting here and compare is fine, the enum == its numeric value.
	if (static_cast<uint32_t>(createInfo.samples) > baseDevice->getConfig().maxEfficientSamples)
	{
		log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_TOO_LARGE_SAMPLE_COUNT,
		    "Trying to create an image with %u samples. "
		    "The hardware revision may not have full throughput for framebuffers with more than %u samples.",
		    static_cast<uint32_t>(createInfo.samples), baseDevice->getConfig().maxEfficientSamples);
	}

	// If we're multisampling, always use a transient attachment.
	if ((static_cast<uint32_t>(createInfo.samples) > 1) &&
	    (createInfo.usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) == 0)
	{
		log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_NON_LAZY_MULTISAMPLED_IMAGE,
		    "Trying to create a multisampled image, but createInfo.usage did not have "
		    "VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT set. Multisampled images should be resolved on-chip, "
		    "and do not need to be backed by physical storage. "
		    "TRANSIENT_ATTACHMENT allows Mali to not back the multisampled image with physical memory.");
	}

	if (!swapchainImage)
		baseDevice->getTable()->GetImageMemoryRequirements(baseDevice->getDevice(), image, &memoryRequirements);
	return VK_SUCCESS;
}

Image::Usage Image::getLastUsage(uint32_t arrayLayer, uint32_t mipLevel) const
{
	MPD_ASSERT(arrayLayer < createInfo.arrayLayers);
	MPD_ASSERT(mipLevel < createInfo.mipLevels);
	auto &resource = arrayLayers[arrayLayer].mipLevels[mipLevel];
	return resource.lastUsage;
}

void Image::signalUsage(const VkImageSubresourceRange &range, Usage usage)
{
	uint32_t maxLayers = createInfo.arrayLayers - range.baseArrayLayer;
	uint32_t arrayLayers = std::min(range.layerCount, maxLayers);
	uint32_t maxLevels = createInfo.mipLevels - range.baseMipLevel;
	uint32_t mipLevels = std::min(createInfo.mipLevels, maxLevels);

	for (uint32_t arrayLayer = 0; arrayLayer < arrayLayers; arrayLayer++)
		for (uint32_t mipLevel = 0; mipLevel < mipLevels; mipLevel++)
			signalUsage(arrayLayer + range.baseArrayLayer, mipLevel + range.baseMipLevel, usage);
}

void Image::signalUsage(const VkImageSubresourceLayers &range, Usage usage)
{
	uint32_t maxLayers = createInfo.arrayLayers - range.baseArrayLayer;
	uint32_t arrayLayers = std::min(range.layerCount, maxLayers);

	for (uint32_t arrayLayer = 0; arrayLayer < arrayLayers; arrayLayer++)
		signalUsage(arrayLayer + range.baseArrayLayer, range.mipLevel, usage);
}

void Image::signalUsage(uint32_t arrayLayer, uint32_t mipLevel, Usage usage)
{
	auto oldUsage = getLastUsage(arrayLayer, mipLevel);

	// Swapchain images are implicitly read so clear after store is expected.
	if (usage == Usage::RenderPassCleared && oldUsage == Usage::RenderPassStored && !swapchainImage)
	{
		log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_REDUNDANT_RENDERPASS_STORE,
		    "Subresource (arrayLayer: %u, mipLevel: %u) of image was cleared as part of LOAD_OP_CLEAR, but last time "
		    "image was used, it was written to with STORE_OP_STORE. "
		    "Storing to the image is probably redundant in this case, and wastes bandwidth on tile-based "
		    "architectures.",
		    arrayLayer, mipLevel);
	}
	else if (usage == Usage::RenderPassCleared && oldUsage == Usage::Cleared)
	{
		log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_REDUNDANT_IMAGE_CLEAR,
		    "Subresource (arrayLayer: %u, mipLevel: %u) of image was cleared as part of LOAD_OP_CLEAR, but last time "
		    "image was used, it was written to with vkCmdClear*Image(). "
		    "Clearing the image with vkCmdClear*Image() is probably redundant in this case, and wastes bandwidth on "
		    "tile-based architectures.",
		    arrayLayer, mipLevel);
	}
	else if (usage == Usage::RenderPassReadToTile && oldUsage == Usage::Cleared)
	{
		log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_INEFFICIENT_CLEAR,
		    "Subresource (arrayLayer: %u, mipLevel: %u) of image was loaded to tile as part of LOAD_OP_LOAD, but last "
		    "time image was used, it was written to with vkCmdClear*Image(). "
		    "Clearing the image with vkCmdClear*Image() is probably redundant in this case, and wastes bandwidth on "
		    "tile-based architectures. "
		    "Use LOAD_OP_CLEAR instead to clear the image for free.",
		    arrayLayer, mipLevel);
	}

	arrayLayers[arrayLayer].mipLevels[mipLevel].lastUsage = usage;
}

VkResult Image::initSwapchain(VkImage image_, const VkImageCreateInfo &createInfo)
{
	swapchainImage = true;
	return init(image_, createInfo);
}

void Image::checkLazyAndTransient()
{
	auto memoryType = memory->getAllocateInfo().memoryTypeIndex;

	// If we're binding memory to a image which was created as TRANSIENT and the image supports LAZY allocation,
	// make sure this type is actually used.
	// If this layer is run on desktop, we won't catch the LAZILY_ALLOCATED_BIT since desktop doesn't support it,
	// so this will only show up when run on mobile.
	if (createInfo.usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT)
	{
		auto &memoryProperties = baseDevice->getMemoryProperties();
		bool supportsLazy = false;
		uint32_t suggestedType = 0;
		for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
		{
			if ((1u << i) & memoryRequirements.memoryTypeBits)
			{
				if (memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)
				{
					supportsLazy = true;
					suggestedType = i;
					break;
				}
			}
		}

		uint32_t allocatedProperties = memoryProperties.memoryTypes[memoryType].propertyFlags;

		if (supportsLazy && (allocatedProperties & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) == 0)
		{
			log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_NON_LAZY_TRANSIENT_IMAGE,
			    "Attempting to bind memory type %u to VkImage which was created with TRANSIENT_ATTACHMENT_BIT, "
			    "but this memory type is not LAZILY_ALLOCATED_BIT. You should use memory type %u here instead to save "
			    "%llu bytes of physical memory.",
			    memoryType, suggestedType, memoryRequirements.size);
		}
	}
}

void Image::checkAllocationSize()
{
	auto memorySize = memory->getAllocateInfo().allocationSize;

	// If we're consuming an entire memory block here, it better be a very large allocation.
	if (memorySize == memoryRequirements.size && memorySize < baseDevice->getConfig().minDedicatedAllocationSize)
	{
		// Sanity check.
		MPD_ASSERT(memoryOffset == 0);

		log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_SMALL_DEDICATED_ALLOCATION,
		    "Trying to bind a VkImage to a memory block which is fully consumed by the image. "
		    "The required size of the allocation is %llu, but smaller images like this should be sub-allocated from "
		    "larger memory blocks. "
		    "(Current threshold is %llu bytes.)",
		    memorySize, baseDevice->getConfig().minDedicatedAllocationSize);
	}
}

VkResult Image::bindMemory(DeviceMemory *memory_, VkDeviceSize offset)
{
	MPD_ASSERT(!swapchainImage);
	memory = memory_;
	memoryOffset = offset;

	checkLazyAndTransient();
	checkAllocationSize();

	return VK_SUCCESS;
}
}
