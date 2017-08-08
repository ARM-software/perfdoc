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
#include "perfdoc.hpp"
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace MPD
{

// Opaque, only used internally.
struct LoggerCallback;

struct LoggerMessageInfo
{
	VkDebugReportFlagsEXT flags;
	VkDebugReportObjectTypeEXT objectType;
	uint64_t object;
	int32_t messageCode;
};

/// The main logger.
class Logger
{
public:
	Logger();
	~Logger();

	Logger(const Logger &) = delete;
	Logger &operator=(const Logger &) = delete;

	LoggerCallback *createAndRegisterCallback(VkDebugReportCallbackEXT callback,
	                                          const VkDebugReportCallbackCreateInfoEXT &createInfo);

	void unregisterAndDestroyCallback(VkDebugReportCallbackEXT callback);

	/// Send a formated message.
	void write(const LoggerMessageInfo &inf, const char *msg);

private:
	std::unordered_map<VkDebugReportCallbackEXT, std::unique_ptr<LoggerCallback>> debugCallbacks;
};
}
