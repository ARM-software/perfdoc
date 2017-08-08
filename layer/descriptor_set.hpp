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
#include <atomic>
#include <unordered_map>
#include <vector>

namespace MPD
{

class DescriptorSetLayout;
class DescriptorPool;
class ImageView;

class DescriptorSet : public BaseObject
{
public:
	using VulkanType = VkDescriptorSet;
	static const VkDebugReportObjectTypeEXT VULKAN_OBJECT_TYPE = VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT;

	DescriptorSet(Device *device, uint64_t objHandle_)
	    : BaseObject(device, objHandle_, VULKAN_OBJECT_TYPE)
	{
	}

	~DescriptorSet();

	/// @note The DescriptorSet will not hold any reference to the layout. The spec allows layouts to be deleted before
	/// sets.
	VkResult init(const DescriptorSetLayout *layout, DescriptorPool *pool);

	uint64_t getLayoutUuid() const
	{
		return layoutUuid;
	}

	const DescriptorPool *getPool() const
	{
		return pool;
	}

	void signalUsage();

	static void writeDescriptors(Device *device, const VkWriteDescriptorSet &write);
	static void copyDescriptors(Device *device, const VkCopyDescriptorSet &copy);

private:
	uint64_t layoutUuid = 0;
	DescriptorPool *pool = nullptr;
	const DescriptorSetLayout *layout = nullptr;

	struct BindingData
	{
		std::vector<ImageView *> views;
		VkDescriptorType descriptorType;
	};

	std::unordered_map<uint32_t, BindingData> bindings;
};
}
