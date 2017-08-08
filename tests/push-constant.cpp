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

class PushConstant : public VulkanTestHelper
{
	bool checkPushConstant(bool positive)
	{
		resetCounts();
		static const uint32_t negativeCode[] =
#include "push_constant.push.comp.inc"
		    ;

		static const uint32_t positiveCode[] =
#include "push_constant.nopush.comp.inc"
		    ;

		const uint32_t *code = positive ? positiveCode : negativeCode;
		size_t codeSize = positive ? sizeof(positiveCode) : sizeof(negativeCode);

		Pipeline ppline(device);
		ppline.initCompute(code, codeSize);

		if (positive)
		{
			if (getCount(MESSAGE_CODE_POTENTIAL_PUSH_CONSTANT) != 4)
				return false;
		}
		else
		{
			if (getCount(MESSAGE_CODE_POTENTIAL_PUSH_CONSTANT) != 0)
				return false;
		}

		if (getCount(MESSAGE_CODE_NO_PIPELINE_CACHE) != 1)
			return false;

		return true;
	}

	bool runTest()
	{
		if (!checkPushConstant(false))
			return false;
		if (!checkPushConstant(true))
			return false;
		return true;
	}
};

VulkanTestHelper *MPD::createTest()
{
	return new PushConstant;
}
