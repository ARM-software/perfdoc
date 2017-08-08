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

#include "perfdoc.hpp"
#include "util.hpp"
#include "vulkan_test.hpp"
using namespace MPD;

class Compute : public VulkanTestHelper
{
	enum class Test
	{
		Aligned,
		Unaligned,
		Large
	};

	bool checkWorkGroupSize(Test test)
	{
		resetCounts();
		static const uint32_t alignedCode[] =
#include "compute.wg.4.1.1.comp.inc"
		    ;

		static const uint32_t unalignedCode[] =
#include "compute.wg.4.1.3.comp.inc"
		    ;

		static const uint32_t largeGroup[] =
#include "compute.wg.16.8.1.comp.inc"
		    ;

		Pipeline ppline(device);
		switch (test)
		{
		case Test::Aligned:
			ppline.initCompute(alignedCode, sizeof(alignedCode));
			break;
		case Test::Unaligned:
			ppline.initCompute(unalignedCode, sizeof(unalignedCode));
			break;
		case Test::Large:
			ppline.initCompute(largeGroup, sizeof(largeGroup));
			break;
		}

		switch (test)
		{
		case Test::Aligned:
			if (getCount(MESSAGE_CODE_COMPUTE_NO_THREAD_GROUP_ALIGNMENT) != 0)
				return false;
			if (getCount(MESSAGE_CODE_COMPUTE_LARGE_WORK_GROUP) != 0)
				return false;
			break;
		case Test::Unaligned:
			if (getCount(MESSAGE_CODE_COMPUTE_NO_THREAD_GROUP_ALIGNMENT) != 1)
				return false;
			if (getCount(MESSAGE_CODE_COMPUTE_LARGE_WORK_GROUP) != 0)
				return false;
			break;
		case Test::Large:
			if (getCount(MESSAGE_CODE_COMPUTE_NO_THREAD_GROUP_ALIGNMENT) != 0)
				return false;
			if (getCount(MESSAGE_CODE_COMPUTE_LARGE_WORK_GROUP) != 1)
				return false;
			break;
		}

		return true;
	}

	enum class DimTest
	{
		Negative2D,
		Negative1D,
		Positive2D
	};

	bool checkDimensions(DimTest test)
	{
		resetCounts();
		static const uint32_t dim_2d_negative[] =
#include "compute.sampler.2d.8.8.1.comp.inc"
		    ;
		static const uint32_t dim_1d_negative[] =
#include "compute.sampler.1d.64.1.1.comp.inc"
		    ;
		static const uint32_t dim_2d_positive[] =
#include "compute.sampler.2d.64.1.1.comp.inc"
		    ;

		const uint32_t *code;
		size_t size;
		switch (test)
		{
		case DimTest::Negative2D:
			code = dim_2d_negative;
			size = sizeof(dim_2d_negative);
			break;
		case DimTest::Negative1D:
			code = dim_1d_negative;
			size = sizeof(dim_1d_negative);
			break;
		case DimTest::Positive2D:
			code = dim_2d_positive;
			size = sizeof(dim_2d_positive);
			break;
		}

		Pipeline ppline(device);
		ppline.initCompute(code, size);

		switch (test)
		{
		case DimTest::Negative2D:
			if (getCount(MESSAGE_CODE_COMPUTE_POOR_SPATIAL_LOCALITY) != 0)
				return false;
			break;
		case DimTest::Negative1D:
			if (getCount(MESSAGE_CODE_COMPUTE_POOR_SPATIAL_LOCALITY) != 0)
				return false;
			break;
		case DimTest::Positive2D:
			if (getCount(MESSAGE_CODE_COMPUTE_POOR_SPATIAL_LOCALITY) != 1)
				return false;
			break;
		}

		return true;
	}

	bool runTest()
	{
		if (!checkWorkGroupSize(Test::Aligned))
			return false;
		if (!checkWorkGroupSize(Test::Unaligned))
			return false;
		if (!checkWorkGroupSize(Test::Large))
			return false;
		if (!checkDimensions(DimTest::Negative2D))
			return false;
		if (!checkDimensions(DimTest::Negative1D))
			return false;
		if (!checkDimensions(DimTest::Positive2D))
			return false;
		return true;
	}
};

VulkanTestHelper *MPD::createTest()
{
	return new Compute;
}
