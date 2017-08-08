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

#include "libvulkan-stub.h"
#include "layer/config.hpp"
#include "layer/message_codes.hpp"

namespace MPD
{
class VulkanTestHelper
{
public:
	VulkanTestHelper();
	virtual ~VulkanTestHelper();

	virtual bool initialize()
	{
		return true;
	}

	virtual bool runTest() = 0;

	const Config &getConfig() const
	{
		return cfg;
	}

	void notifyCallback(MessageCodes code);

protected:
	void resetCounts();
	unsigned getCount(MessageCodes code) const;

	VkInstance instance = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkPhysicalDevice gpu = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	VkPhysicalDeviceMemoryProperties memoryProperties = {};
	VkPhysicalDeviceProperties gpuProperties = {};
	VkDebugReportCallbackEXT callback = VK_NULL_HANDLE;

	unsigned warningCount[MESSAGE_CODE_COUNT] = {};
	Config cfg;
};

// Implemented by tests.
VulkanTestHelper *createTest();
}