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

#include "base_object.hpp"
#include "device.hpp"
#include "instance.hpp"
#include <cstdarg>

namespace MPD
{

std::atomic<uint64_t> BaseObject::uuids = { 1 };

Instance *BaseObject::getInstance() const
{
	return baseDevice->getInstance();
}

static void dispatchLog(Logger &logger, VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT type,
                        uint64_t objHandle, int32_t messageCode, const char *fmt, va_list args)
{
	char buffer[1024 * 10];
	vsnprintf(buffer, sizeof(buffer), fmt, args);

	LoggerMessageInfo inf;
	inf.flags = flags;
	inf.objectType = type;
	inf.object = objHandle;
	inf.messageCode = messageCode;
	logger.write(inf, buffer);
}

void BaseObject::log(VkDebugReportFlagsEXT flags, int32_t messageCode, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	dispatchLog(getInstance()->getLogger(), flags, type, objHandle, messageCode, fmt, args);
	va_end(args);
}

void BaseInstanceObject::log(VkDebugReportFlagsEXT flags, int32_t messageCode, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	dispatchLog(baseInstance->getLogger(), flags, type, objHandle, messageCode, fmt, args);
	va_end(args);
}
}
