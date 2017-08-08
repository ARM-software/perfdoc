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

#include "vulkan_test.hpp"
#include "perfdoc.hpp"
#include "util.hpp"
using namespace MPD;
using namespace std;

class Transient : public VulkanTestHelper
{
	bool runTest()
	{
		if (!testTransient(false))
			return false;
		if (!testTransient(true))
			return false;

		if (!testTransientMismatch(false, false))
			return false;
		if (!testTransientMismatch(false, true))
			return false;
		if (!testTransientMismatch(true, false))
			return false;
		if (!testTransientMismatch(true, true))
			return false;

		return true;
	}

	bool testTransientMismatch(bool transientImage, bool transientRenderPass)
	{
		resetCounts();

		auto renderTarget = make_shared<Texture>(device);
		renderTarget->initRenderTarget2D(1024, 1024, VK_FORMAT_R8G8B8A8_UNORM, transientImage);

		auto fb = make_shared<Framebuffer>(device);
		fb->initOnlyColor(renderTarget, transientRenderPass ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
		                  transientRenderPass ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE);

		if (transientRenderPass && !transientImage)
		{
			if (getCount(MESSAGE_CODE_FRAMEBUFFER_ATTACHMENT_SHOULD_BE_TRANSIENT) != 1)
				return false;
			if (getCount(MESSAGE_CODE_FRAMEBUFFER_ATTACHMENT_SHOULD_NOT_BE_TRANSIENT) != 0)
				return false;
		}
		else if (!transientRenderPass && transientImage)
		{
			if (getCount(MESSAGE_CODE_FRAMEBUFFER_ATTACHMENT_SHOULD_BE_TRANSIENT) != 0)
				return false;
			if (getCount(MESSAGE_CODE_FRAMEBUFFER_ATTACHMENT_SHOULD_NOT_BE_TRANSIENT) != 1)
				return false;
		}
		else
		{
			if (getCount(MESSAGE_CODE_FRAMEBUFFER_ATTACHMENT_SHOULD_BE_TRANSIENT) != 0)
				return false;
			if (getCount(MESSAGE_CODE_FRAMEBUFFER_ATTACHMENT_SHOULD_NOT_BE_TRANSIENT) != 0)
				return false;
		}

		return true;
	}

	bool testTransient(bool positiveTest)
	{
		resetCounts();
		VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		info.format = VK_FORMAT_R8G8B8A8_UNORM;
		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.arrayLayers = 1;
		info.mipLevels = 1;
		info.imageType = VK_IMAGE_TYPE_2D;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
		             VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
		info.extent.width = 1024;
		info.extent.height = 1024;
		info.extent.depth = 1;

		VkImage image;
		MPD_ASSERT_RESULT(vkCreateImage(device, &info, nullptr, &image));

		VkMemoryRequirements memoryRequirements;
		vkGetImageMemoryRequirements(device, image, &memoryRequirements);

		// First figure out if implementation supports lazy memory.
		bool implementationSupportsLazy = false;
		bool implementationSupportsNonLazy = false;
		uint32_t lazyType = 0;
		uint32_t nonLazyType = 0;
		for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
		{
			if ((1u << i) & memoryRequirements.memoryTypeBits)
			{
				if (memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)
				{
					implementationSupportsLazy = true;
					lazyType = i;
				}
				else if (memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
				{
					implementationSupportsNonLazy = true;
					nonLazyType = i;
				}
			}
		}

		MPD_ALWAYS_ASSERT(implementationSupportsLazy || implementationSupportsNonLazy);

		// If we have a positive test and we don't support lazy here, we have no way to test it.
		if (positiveTest && !implementationSupportsLazy)
			return true;

		VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		allocInfo.allocationSize = memoryRequirements.size;

		if (positiveTest)
		{
			// For positive test, use the "wrong" type.
			allocInfo.memoryTypeIndex = nonLazyType;
		}
		else
		{
			// For negative test, use the correct type.
			allocInfo.memoryTypeIndex = implementationSupportsLazy ? lazyType : nonLazyType;
		}

		VkDeviceMemory memory;
		MPD_ASSERT_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &memory));
		MPD_ASSERT_RESULT(vkBindImageMemory(device, image, memory, 0));

		if (positiveTest)
		{
			if (getCount(MESSAGE_CODE_NON_LAZY_TRANSIENT_IMAGE) != 1)
				return false;
		}
		else
		{
			if (getCount(MESSAGE_CODE_NON_LAZY_TRANSIENT_IMAGE) != 0)
				return false;
		}

		vkDestroyImage(device, image, nullptr);
		vkFreeMemory(device, memory, nullptr);
		return true;
	}
};

VulkanTestHelper *MPD::createTest()
{
	return new Transient;
}
