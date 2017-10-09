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
#include <memory>
#include <unordered_map>

namespace MPD
{

class DescriptorSet;

class DescriptorPool : public BaseObject
{
public:
	using VulkanType = VkDescriptorPool;
	static const VkDebugReportObjectTypeEXT VULKAN_OBJECT_TYPE = VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_POOL_EXT;

	DescriptorPool(Device *device, uint64_t objHandle_)
	    : BaseObject(device, objHandle_, VULKAN_OBJECT_TYPE)
	{
	}

	~DescriptorPool();

	VkResult init(const VkDescriptorPoolCreateInfo &)
	{
		return VK_SUCCESS;
	}

	void descriptorSetCreated(DescriptorSet *dset);

	void descriptorSetDeleted(DescriptorSet *dset);

	void reset();

private:
	struct DescriptorSetLayoutInfo
	{
		uint32_t descriptorSetsFreedCount;
	};

	std::unordered_map<uint64_t, DescriptorSetLayoutInfo> layoutInfos;
};
}
