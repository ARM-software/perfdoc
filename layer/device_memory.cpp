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

#include "device_memory.hpp"
#include "buffer.hpp"
#include "device.hpp"
#include "message_codes.hpp"

namespace MPD
{
VkResult DeviceMemory::init(VkDeviceMemory memory_, const VkMemoryAllocateInfo &allocInfo_)
{
	memory = memory_;
	allocInfo = allocInfo_;
	mappedMemory = NULL;

	const VkPhysicalDeviceMemoryProperties &memoryProperties = getDevice()->getMemoryProperties();
	if ((memoryProperties.memoryTypes[allocInfo.memoryTypeIndex].propertyFlags &
	     Buffer::INDEXBUFFER_MEMORY_PROPERTIES) == Buffer::INDEXBUFFER_MEMORY_PROPERTIES)
	{
		VkResult res =
		    getDevice()->getTable()->MapMemory(getDevice()->getDevice(), memory, 0, VK_WHOLE_SIZE, 0, &mappedMemory);
		if (res != VK_SUCCESS)
		{
			mappedMemory = NULL;
		}
	}

	if (allocInfo.allocationSize < baseDevice->getConfig().minDeviceAllocationSize)
	{
		log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_SMALL_ALLOCATION,
		    "Allocating a VkDeviceMemory of size %llu. This is a very small allocation (current threshold is %llu "
		    "bytes). "
		    "You should make large allocations and sub-allocate from one large VkDeviceMemory.",
		    allocInfo.allocationSize, baseDevice->getConfig().minDeviceAllocationSize);
	}
	return VK_SUCCESS;
}
}
