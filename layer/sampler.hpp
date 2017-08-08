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

namespace MPD
{
class Sampler : BaseObject
{
public:
	using VulkanType = VkSampler;
	static const VkDebugReportObjectTypeEXT VULKAN_OBJECT_TYPE = VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT;

	Sampler(Device *device_, uint64_t objHandle_)
	    : BaseObject(device_, objHandle_, VULKAN_OBJECT_TYPE)
	{
	}

	VkResult init(VkSampler sampler, const VkSamplerCreateInfo &createInfo);

	const VkSamplerCreateInfo &getCreateInfo() const
	{
		return createInfo;
	}

private:
	VkSampler sampler;
	VkSamplerCreateInfo createInfo;

	void checkIdenticalWrapping();
	void checkLodClamping();
	void checkLodBias();
	void checkBorderClamp();
	void checkUnnormalizedCoords();
	void checkAnisotropy();
};
}
