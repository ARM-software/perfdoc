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

#include "buffer.hpp"
#include "device.hpp"
#include "device_memory.hpp"
#include "message_codes.hpp"

namespace MPD
{
VkResult Buffer::init(VkBuffer buffer_, const VkBufferCreateInfo &createInfo_)
{
	buffer = buffer_;
	createInfo = createInfo_;

	baseDevice->getTable()->GetBufferMemoryRequirements(baseDevice->getDevice(), buffer, &memoryRequirements);

	// we need to be able to map indexbuffer back to host memory
	// so modify memory requirements such that host_visible is always used
	if (createInfo.usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
	{
		const VkPhysicalDeviceMemoryProperties &memoryProperties = getDevice()->getMemoryProperties();
		memoryRequirements.memoryTypeBits = INDEXBUFFER_MEMORY_PROPERTIES;

		for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
		{
			if ((memoryProperties.memoryTypes[i].propertyFlags & INDEXBUFFER_MEMORY_PROPERTIES) ==
			    INDEXBUFFER_MEMORY_PROPERTIES)
			{
				memoryRequirements.memoryTypeBits = 1u << i;
				break;
			}
		}
	}

	return VK_SUCCESS;
}

VkResult Buffer::bindMemory(DeviceMemory *memory_, VkDeviceSize offset)
{
	memory = memory_;
	memoryOffset = offset;

	auto memorySize = memory->getAllocateInfo().allocationSize;

	// If we're consuming an entire memory block here, it better be a very large allocation.
	if (memorySize == memoryRequirements.size && memorySize < baseDevice->getConfig().minDedicatedAllocationSize)
	{
		// Sanity check.
		MPD_ASSERT(offset == 0);

		log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_SMALL_DEDICATED_ALLOCATION,
		    "Trying to bind a VkBuffer to a memory block which is fully consumed by the buffer. "
		    "The required size of the allocation is %llu, but smaller buffers like this should be sub-allocated from "
		    "larger memory blocks. "
		    "(Current threshold is %llu bytes.)",
		    memorySize, baseDevice->getConfig().minDedicatedAllocationSize);
	}
	return VK_SUCCESS;
}
}
