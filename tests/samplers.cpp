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

class Samplers : public VulkanTestHelper
{
	bool testWrapping(bool positiveTest)
	{
		resetCounts();
		VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		info.magFilter = VK_FILTER_NEAREST;
		info.minFilter = VK_FILTER_NEAREST;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

		if (positiveTest)
			info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;

		VkSampler sampler;
		MPD_ASSERT_RESULT(vkCreateSampler(device, &info, nullptr, &sampler));
		vkDestroySampler(device, sampler, nullptr);

		if (positiveTest)
		{
			if (getCount(MESSAGE_CODE_DISSIMILAR_WRAPPING) != 1)
				return false;
		}
		else
		{
			if (getCount(MESSAGE_CODE_DISSIMILAR_WRAPPING) != 0)
				return false;
		}
		return true;
	}

	bool testLod(unsigned testIteration)
	{
		resetCounts();
		VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		info.magFilter = VK_FILTER_NEAREST;
		info.minFilter = VK_FILTER_NEAREST;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

		bool positiveTest = testIteration != 0;

		if (positiveTest)
		{
			if (testIteration == 1)
			{
				info.minLod = 0.2f;
				info.maxLod = VK_LOD_CLAMP_NONE;
			}
			else if (testIteration == 2)
			{
				info.maxLod = float(getConfig().unclampedMaxLod) - 0.01f;
			}

			info.mipLodBias = 0.5f;
		}
		else
		{
			info.maxLod = VK_LOD_CLAMP_NONE;
		}

		VkSampler sampler;
		MPD_ASSERT_RESULT(vkCreateSampler(device, &info, nullptr, &sampler));
		vkDestroySampler(device, sampler, nullptr);

		if (positiveTest)
		{
			if (getCount(MESSAGE_CODE_SAMPLER_LOD_BIAS) != 1)
				return false;
			if (getCount(MESSAGE_CODE_SAMPLER_LOD_CLAMPING) != 1)
				return false;
		}
		else
		{
			if (getCount(MESSAGE_CODE_SAMPLER_LOD_BIAS) != 0)
				return false;
			if (getCount(MESSAGE_CODE_SAMPLER_LOD_CLAMPING) != 0)
				return false;
		}
		return true;
	}

	bool testUnnormalized(bool positiveTest)
	{
		resetCounts();
		VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		info.magFilter = VK_FILTER_NEAREST;
		info.minFilter = VK_FILTER_NEAREST;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.unnormalizedCoordinates = positiveTest ? VK_TRUE : VK_FALSE;

		VkSampler sampler;
		MPD_ASSERT_RESULT(vkCreateSampler(device, &info, nullptr, &sampler));
		vkDestroySampler(device, sampler, nullptr);

		if (positiveTest)
		{
			if (getCount(MESSAGE_CODE_SAMPLER_UNNORMALIZED_COORDS) != 1)
				return false;
		}
		else
		{
			if (getCount(MESSAGE_CODE_SAMPLER_UNNORMALIZED_COORDS) != 0)
				return false;
		}
		return true;
	}

	bool testBorderClampColor(bool positiveTest)
	{
		resetCounts();
		VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		info.magFilter = VK_FILTER_NEAREST;
		info.minFilter = VK_FILTER_NEAREST;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		info.borderColor = positiveTest ? VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

		VkSampler sampler;
		MPD_ASSERT_RESULT(vkCreateSampler(device, &info, nullptr, &sampler));
		vkDestroySampler(device, sampler, nullptr);

		if (positiveTest)
		{
			if (getCount(MESSAGE_CODE_SAMPLER_BORDER_CLAMP_COLOR) != 1)
				return false;
		}
		else
		{
			if (getCount(MESSAGE_CODE_SAMPLER_BORDER_CLAMP_COLOR) != 0)
				return false;
		}
		return true;
	}

	bool testAnisotropy(bool positiveTest)
	{
		resetCounts();
		VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		info.magFilter = VK_FILTER_NEAREST;
		info.minFilter = VK_FILTER_NEAREST;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

		if (positiveTest)
		{
			info.anisotropyEnable = VK_TRUE;
			info.maxAnisotropy = 2.0f;
		}

		VkSampler sampler;
		MPD_ASSERT_RESULT(vkCreateSampler(device, &info, nullptr, &sampler));
		vkDestroySampler(device, sampler, nullptr);

		if (positiveTest)
		{
			if (getCount(MESSAGE_CODE_SAMPLER_ANISOTROPY) != 1)
				return false;
		}
		else
		{
			if (getCount(MESSAGE_CODE_SAMPLER_ANISOTROPY) != 0)
				return false;
		}
		return true;
	}

	bool runTest()
	{
		if (!testWrapping(true))
			return false;
		if (!testWrapping(false))
			return false;
		for (unsigned i = 0; i < 3; i++)
			if (!testLod(i))
				return false;
		if (!testUnnormalized(true))
			return false;
		if (!testUnnormalized(false))
			return false;
		if (!testBorderClampColor(true))
			return false;
		if (!testBorderClampColor(false))
			return false;
		if (!testAnisotropy(true))
			return false;
		if (!testAnisotropy(false))
			return false;
		return true;
	}
};

VulkanTestHelper *MPD::createTest()
{
	return new Samplers;
}
