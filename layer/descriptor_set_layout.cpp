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

#include "descriptor_set_layout.hpp"

namespace MPD
{
VkResult DescriptorSetLayout::init(const VkDescriptorSetLayoutCreateInfo *pCreateInfo)
{
	uint32_t bindings = pCreateInfo->bindingCount;
	for (uint32_t i = 0; i < bindings; i++)
	{
		auto &binding = pCreateInfo->pBindings[i];

		switch (binding.descriptorType)
		{
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			sampledImageBindings.insert(binding.binding);
			break;

		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			storageImageBindings.insert(binding.binding);
			break;

		default:
			break;
		}

		this->bindings[binding.binding] = { binding.descriptorType, binding.descriptorCount };
	}

	return VK_SUCCESS;
}
}