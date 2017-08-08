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

#include "descriptor_set.hpp"
#include "descriptor_pool.hpp"
#include "descriptor_set_layout.hpp"
#include "device.hpp"
#include "image_view.hpp"

namespace MPD
{

VkResult DescriptorSet::init(const DescriptorSetLayout *layout_, DescriptorPool *pool_)
{
	MPD_ASSERT(layout_);
	MPD_ASSERT(pool_);

	pool = pool_;
	layout = layout_;
	layoutUuid = layout->getUuid();

	for (auto &binding : layout->getBindings())
	{
		bindings[binding.first].views.resize(binding.second.arraySize);
		bindings[binding.first].descriptorType = binding.second.descriptorType;
	}

	pool->descriptorSetCreated(this);
	return VK_SUCCESS;
}

void DescriptorSet::copyDescriptors(Device *device, const VkCopyDescriptorSet &copy)
{
	auto *dst = device->get<DescriptorSet>(copy.dstSet);
	auto *src = device->get<DescriptorSet>(copy.srcSet);

	auto &dstData = dst->bindings[copy.dstBinding];
	auto &srcData = src->bindings[copy.srcBinding];

	for (uint32_t i = 0; i < copy.descriptorCount; i++)
	{
		MPD_ASSERT(copy.dstArrayElement + i < dstData.views.size());
		MPD_ASSERT(copy.srcArrayElement + i < srcData.views.size());
		MPD_ASSERT(dstData.descriptorType == srcData.descriptorType);
		dstData.views[copy.dstArrayElement + i] = srcData.views[copy.srcArrayElement + i];
	}
}

void DescriptorSet::writeDescriptors(Device *device, const VkWriteDescriptorSet &write)
{
	auto *dst = device->get<DescriptorSet>(write.dstSet);
	uint32_t binding = write.dstBinding;
	uint32_t arrayIndex = write.dstArrayElement;
	uint32_t count = write.descriptorCount;

	auto &bindingData = dst->bindings[binding];
	for (uint32_t i = 0; i < count; i++)
	{
		MPD_ASSERT(arrayIndex + i < bindingData.views.size());
		MPD_ASSERT(bindingData.descriptorType == write.descriptorType);

		switch (write.descriptorType)
		{
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			bindingData.views[arrayIndex + i] = device->get<ImageView>(write.pImageInfo[i].imageView);
			break;

		default:
			bindingData.views[arrayIndex + i] = nullptr;
			break;
		}
	}
}

void DescriptorSet::signalUsage()
{
	for (auto &binding : layout->getSampledImageBindings())
	{
		for (auto &view : bindings[binding].views)
		{
			if (view)
				view->signalUsage(Image::Usage::ResourceRead);
		}
	}

	for (auto &binding : layout->getStorageImageBindings())
	{
		for (auto &view : bindings[binding].views)
		{
			if (view)
				view->signalUsage(Image::Usage::ResourceWrite);
		}
	}
}

DescriptorSet::~DescriptorSet()
{
	if (pool)
		pool->descriptorSetDeleted(this);
}
}
