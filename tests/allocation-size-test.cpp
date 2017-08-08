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

class AllocationSizeTest : public VulkanTestHelper
{
	bool initialize() override
	{
		if (!VulkanTestHelper::initialize())
			return false;

		// Just find a sensible memory type that we can use for our test.
		VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		info.size = 64;
		info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

		VkBuffer buffer;
		MPD_ASSERT_RESULT(vkCreateBuffer(device, &info, nullptr, &buffer));

		VkMemoryRequirements memoryRequirements;
		vkGetBufferMemoryRequirements(device, buffer, &memoryRequirements);
		memoryType = ctz(memoryRequirements.memoryTypeBits);
		vkDestroyBuffer(device, buffer, nullptr);
		return true;
	}

	bool runTest() override
	{
		if (!testSmallAllocationPositive())
			return false;
		if (!testSmallAllocationNegative())
			return false;
		if (!testSmallDedicatedAllocationPositive())
			return false;
		if (!testSmallDedicatedAllocationNegative())
			return false;
		return true;
	}

	bool testDedicatedImageAllocation(bool large)
	{
		resetCounts();

		VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		info.arrayLayers = 1;
		info.mipLevels = 1;
		info.extent.depth = 1;
		info.imageType = VK_IMAGE_TYPE_2D;
		info.format = VK_FORMAT_R8G8B8A8_UNORM;
		info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		info.tiling = VK_IMAGE_TILING_LINEAR;
		info.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
		info.samples = VK_SAMPLE_COUNT_1_BIT;

		if (large)
		{
			info.extent.width = 1024;
			info.extent.height = 1024;
		}
		else
		{
			info.extent.width = 64;
			info.extent.height = 64;
		}

		VkImage image;
		MPD_ASSERT_RESULT(vkCreateImage(device, &info, nullptr, &image));

		VkMemoryRequirements memoryRequirements;
		vkGetImageMemoryRequirements(device, image, &memoryRequirements);

		// Sanity check our test so we're testing the right thing.
		if (large)
		{
			MPD_ALWAYS_ASSERT(memoryRequirements.size >= getConfig().minDedicatedAllocationSize);
		}
		else
		{
			MPD_ALWAYS_ASSERT(memoryRequirements.size < getConfig().minDedicatedAllocationSize);
		}

		uint32_t type = ctz(memoryRequirements.memoryTypeBits);
		VkDeviceMemory memory;
		VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		allocInfo.memoryTypeIndex = type;
		allocInfo.allocationSize = memoryRequirements.size;

		MPD_ASSERT_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &memory));
		MPD_ASSERT_RESULT(vkBindImageMemory(device, image, memory, 0));

		if (large)
		{
			if (getCount(MESSAGE_CODE_SMALL_ALLOCATION) != 0)
				return false;
		}
		else
		{
			if (getCount(MESSAGE_CODE_SMALL_ALLOCATION) != 1)
				return false;
		}

		vkFreeMemory(device, memory, nullptr);
		vkDestroyImage(device, image, nullptr);

		return true;
	}

	bool testDedicatedBufferAllocation(VkDeviceSize size)
	{
		resetCounts();

		VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		info.size = size;
		info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

		VkBuffer buffer;
		MPD_ASSERT_RESULT(vkCreateBuffer(device, &info, nullptr, &buffer));

		VkMemoryRequirements memoryRequirements;
		vkGetBufferMemoryRequirements(device, buffer, &memoryRequirements);
		uint32_t type = ctz(memoryRequirements.memoryTypeBits);

		VkDeviceMemory memory;
		VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		allocInfo.memoryTypeIndex = type;
		allocInfo.allocationSize = memoryRequirements.size;
		MPD_ASSERT_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &memory));

		MPD_ASSERT_RESULT(vkBindBufferMemory(device, buffer, memory, 0));

		if (size < getConfig().minDedicatedAllocationSize)
		{
			if (getCount(MESSAGE_CODE_SMALL_DEDICATED_ALLOCATION) != 1)
				return false;
		}
		else
		{
			if (getCount(MESSAGE_CODE_SMALL_DEDICATED_ALLOCATION) != 0)
				return false;
		}

		vkFreeMemory(device, memory, nullptr);
		vkDestroyBuffer(device, buffer, nullptr);

		return true;
	}

	bool testSmallDedicatedAllocationPositive()
	{
		if (!testDedicatedBufferAllocation(getConfig().minDedicatedAllocationSize >> 1))
			return false;
		if (!testDedicatedImageAllocation(false))
			return false;
		return true;
	}

	bool testSmallDedicatedAllocationNegative()
	{
		if (!testDedicatedBufferAllocation(getConfig().minDedicatedAllocationSize))
			return false;
		if (!testDedicatedImageAllocation(true))
			return false;
		return true;
	}

	bool testSmallAllocationNegative()
	{
		resetCounts();

		VkMemoryAllocateInfo info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		info.allocationSize = getConfig().minDeviceAllocationSize;
		info.memoryTypeIndex = memoryType;

		VkDeviceMemory memory;
		MPD_ASSERT_RESULT(vkAllocateMemory(device, &info, nullptr, &memory));

		if (getCount(MESSAGE_CODE_SMALL_ALLOCATION) != 0)
			return false;

		vkFreeMemory(device, memory, nullptr);
		return true;
	}

	bool testSmallAllocationPositive()
	{
		resetCounts();

		VkMemoryAllocateInfo info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		info.allocationSize = getConfig().minDeviceAllocationSize >> 1;
		info.memoryTypeIndex = memoryType;

		VkDeviceMemory memory;
		MPD_ASSERT_RESULT(vkAllocateMemory(device, &info, nullptr, &memory));

		if (getCount(MESSAGE_CODE_SMALL_ALLOCATION) != 1)
			return false;

		vkFreeMemory(device, memory, nullptr);
		return true;
	}

	uint32_t memoryType;
};

VulkanTestHelper *MPD::createTest()
{
	return new AllocationSizeTest;
}
