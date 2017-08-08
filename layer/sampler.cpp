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

#include "sampler.hpp"
#include "device.hpp"
#include "message_codes.hpp"

namespace MPD
{

void Sampler::checkIdenticalWrapping()
{
	bool differentAddress =
	    (createInfo.addressModeU != createInfo.addressModeV) || (createInfo.addressModeV != createInfo.addressModeW);
	if (differentAddress)
	{
		log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_DISSIMILAR_WRAPPING,
		    "Creating a sampler object with wrapping modes which do not match (U = %u, V = %u, W = %u). "
		    "This will lead to less efficient descriptors being created on Mali-G71, Mali-G72 and Mali-G51 even if "
		    "only U (1D image) or U/V wrapping "
		    "modes (2D image) are actually used and may cause reduced performance. "
		    "If you need different wrapping modes, disregard this warning.",
		    static_cast<unsigned>(createInfo.addressModeU), static_cast<unsigned>(createInfo.addressModeV),
		    static_cast<unsigned>(createInfo.addressModeW));
	}
}

void Sampler::checkLodClamping()
{
	bool lodClamping = (createInfo.minLod != 0.0f) || (createInfo.maxLod < baseDevice->getConfig().unclampedMaxLod);
	if (lodClamping)
	{
		log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_SAMPLER_LOD_CLAMPING,
		    "Creating a sampler object with LOD clamping (minLod = %f, maxLod = %f). "
		    "This will lead to less efficient descriptors being created on Mali-G71, Mali-G72 and Mali-G51 and may "
		    "cause reduced performance. "
		    "Instead of clamping LOD in the sampler, consider using an VkImageView which restricts the mip-levels, "
		    "set minLod to 0.0, and maxLod to at least %f (or just VK_LOD_CLAMP_NONE).",
		    createInfo.minLod, createInfo.maxLod, baseDevice->getConfig().unclampedMaxLod);
	}
}

void Sampler::checkLodBias()
{
	bool lodBias = createInfo.mipLodBias != 0.0f;
	if (lodBias)
	{
		log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_SAMPLER_LOD_BIAS,
		    "Creating a sampler object with LOD bias != 0.0 (%f). "
		    "This will lead to less efficient descriptors being created on Mali-G71, Mali-G72 and Mali-G51 and may "
		    "cause reduced performance.",
		    createInfo.mipLodBias);
	}
}

void Sampler::checkBorderClamp()
{
	if (createInfo.addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
	    createInfo.addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
	    createInfo.addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER)
	{
		if (createInfo.borderColor != VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK)
		{
			log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_SAMPLER_BORDER_CLAMP_COLOR,
			    "Creating a sampler object with border clamping and borderColor != "
			    "VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK. "
			    "This will lead to less efficient descriptors being created on Mali-G71, Mali-G72 and Mali-G51 and "
			    "may cause reduced performance. "
			    "If possible, use VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK as the border color.");
		}
	}
}

void Sampler::checkUnnormalizedCoords()
{
	if (createInfo.unnormalizedCoordinates)
	{
		log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_SAMPLER_UNNORMALIZED_COORDS,
		    "Creating a sampler object with unnormalized coordinates. "
		    "This will lead to less efficient descriptors being created on Mali-G71, Mali-G72 and Mali-G51 and may "
		    "cause reduced performance.");
	}
}

void Sampler::checkAnisotropy()
{
	if (createInfo.anisotropyEnable)
	{
		log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_SAMPLER_ANISOTROPY,
		    "Creating a sampler object with anisotropy. "
		    "This will lead to less efficient descriptors being created on Mali-G71, Mali-G72 and Mali-G51 and may "
		    "cause reduced performance.");
	}
}

VkResult Sampler::init(VkSampler sampler_, const VkSamplerCreateInfo &createInfo_)
{
	sampler = sampler_;
	createInfo = createInfo_;
	checkIdenticalWrapping();
	checkLodClamping();
	checkLodBias();
	checkBorderClamp();
	checkUnnormalizedCoords();
	checkAnisotropy();
	return VK_SUCCESS;
}
}