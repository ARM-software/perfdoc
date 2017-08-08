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
#include <assert.h>
#include <vulkan/vulkan.h>

#define MPD_ASSERT(x) assert(x)

/// Used for test scenarios where we should never fail.
#define MPD_ALWAYS_ASSERT(x)  \
	do                        \
	{                         \
		if (!(x))             \
			std::terminate(); \
	} while (0)

/// Used in test code only.
#define MPD_ASSERT_RESULT(x)   \
	do                         \
	{                          \
		if ((x) != VK_SUCCESS) \
			std::terminate();  \
	} while (0)

#define VK_LAYER_ARM_mali_perf_doc "VK_LAYER_ARM_mali_perf_doc"

#ifdef ANDROID
#include <android/log.h>
#define MPD_LOG(...) __android_log_print(ANDROID_LOG_DEBUG, "MaliPerfDoc", __VA_ARGS__)
#else
#define MPD_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

#define MPD_UNUSED(x) ((void)(x))
