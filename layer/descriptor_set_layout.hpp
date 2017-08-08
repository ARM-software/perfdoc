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
#include <unordered_map>
#include <unordered_set>

namespace MPD
{

class DescriptorSetLayout : public BaseObject
{
public:
	using VulkanType = VkDescriptorSetLayout;
	static const VkDebugReportObjectTypeEXT VULKAN_OBJECT_TYPE = VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT;

	DescriptorSetLayout(Device *device, uint64_t objHandle_)
	    : BaseObject(device, objHandle_, VULKAN_OBJECT_TYPE)
	{
	}

	VkResult init(const VkDescriptorSetLayoutCreateInfo *pCreateInfo);

	const std::unordered_set<uint32_t> &getSampledImageBindings() const
	{
		return sampledImageBindings;
	}

	const std::unordered_set<uint32_t> &getStorageImageBindings() const
	{
		return storageImageBindings;
	}

	struct Binding
	{
		VkDescriptorType descriptorType;
		uint32_t arraySize;
	};

	const std::unordered_map<uint32_t, Binding> &getBindings() const
	{
		return bindings;
	}

private:
	std::unordered_set<uint32_t> sampledImageBindings;
	std::unordered_set<uint32_t> storageImageBindings;
	std::unordered_map<uint32_t, Binding> bindings;
};
}
