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

#pragma once
#include "base_object.hpp"
#include "dispatch_helper.hpp"
#include "perfdoc.hpp"
#include <vector>

namespace MPD
{
class DeviceMemory;
class Image : public BaseObject
{
public:
	using VulkanType = VkImage;
	static const VkDebugReportObjectTypeEXT VULKAN_OBJECT_TYPE = VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT;

	Image(Device *device_, uint64_t objHandle_)
	    : BaseObject(device_, objHandle_, VULKAN_OBJECT_TYPE)
	{
	}

	VkResult init(VkImage image_, const VkImageCreateInfo &createInfo);
	VkResult initSwapchain(VkImage image_, const VkImageCreateInfo &createInfo);
	VkResult bindMemory(DeviceMemory *memory, VkDeviceSize offset);

	const VkMemoryRequirements &getMemoryRequirements() const
	{
		MPD_ASSERT(!swapchainImage);
		return memoryRequirements;
	}

	const VkImageCreateInfo &getCreateInfo() const
	{
		return createInfo;
	}

	bool isSwapchainImage() const
	{
		return swapchainImage;
	}

	enum class Usage
	{
		Undefined,
		RenderPassCleared,
		RenderPassReadToTile,
		Cleared,
		ResourceRead,
		ResourceWrite,
		RenderPassStored,
		RenderPassDiscarded
	};

	void signalUsage(uint32_t arrayLayer, uint32_t mipLevel, Usage usage);
	void signalUsage(const VkImageSubresourceRange &range, Usage usage);
	void signalUsage(const VkImageSubresourceLayers &range, Usage usage);

	Usage getLastUsage(uint32_t arrayLayer, uint32_t mipLevel) const;

private:
	VkImage image = VK_NULL_HANDLE;
	DeviceMemory *memory = nullptr;
	VkDeviceSize memoryOffset = 0;
	VkImageCreateInfo createInfo;
	VkMemoryRequirements memoryRequirements;
	bool swapchainImage = false;

	void checkLazyAndTransient();
	void checkAllocationSize();

	struct MipLevel
	{
		Usage lastUsage = Usage::Undefined;
	};

	struct Layer
	{
		std::vector<MipLevel> mipLevels;
	};

	std::vector<Layer> arrayLayers;
};
}
