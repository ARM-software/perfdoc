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

#include "logger.hpp"

using namespace std;

namespace MPD
{

Logger::Logger()
{
}

Logger::~Logger()
{
}

struct LoggerCallback
{
	VkDebugReportCallbackEXT callback;
	VkDebugReportFlagsEXT flags;
	PFN_vkDebugReportCallbackEXT pfnCallback;
	void *pUserData;
};

LoggerCallback *Logger::createAndRegisterCallback(VkDebugReportCallbackEXT callback,
                                                  const VkDebugReportCallbackCreateInfoEXT &createInfo)
{
	MPD_ASSERT(createInfo.pfnCallback);

	auto pCallback = unique_ptr<LoggerCallback>(new LoggerCallback);
	pCallback->callback = callback;
	pCallback->flags = createInfo.flags;
	pCallback->pfnCallback = createInfo.pfnCallback;
	pCallback->pUserData = createInfo.pUserData;

	auto *ret = pCallback.get();
	debugCallbacks[callback] = move(pCallback);
	return ret;
}

void Logger::unregisterAndDestroyCallback(VkDebugReportCallbackEXT callback)
{
	auto itr = debugCallbacks.find(callback);
	debugCallbacks.erase(itr);
}

void Logger::write(const LoggerMessageInfo &inf, const char *msg)
{
	for (const auto &callback : debugCallbacks)
	{
		auto &cb = callback.second;
		auto u = cb->flags & inf.flags;
		if (u)
		{
			size_t location = 0;
			cb->pfnCallback(u, inf.objectType, inf.object, location, inf.messageCode, "MaliPerfDoc", msg,
			                cb->pUserData);
		}
	}
}
}
