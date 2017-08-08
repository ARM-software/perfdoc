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
#include <atomic>

namespace MPD
{

class Instance;
class Device;

/// The base of all Vulkan objects which derive from a VkDevice.
class BaseObject
{
public:
	BaseObject(Device *device, uint64_t objHandle_, VkDebugReportObjectTypeEXT type_)
	    : baseDevice(device)
	    , objHandle(objHandle_)
	    , type(type_)
	    , uuid(uuids.fetch_add(1))
	{
	}

	void log(VkDebugReportFlagsEXT flags, int32_t messageCode, const char *fmt, ...);

	Instance *getInstance() const;
	Device *getDevice() const
	{
		return baseDevice;
	}

	/// Get universaly unique identifier. It's unique for all objects.
	uint64_t getUuid() const
	{
		return uuid;
	}

protected:
	Device *baseDevice;

private:
	uint64_t objHandle;
	VkDebugReportObjectTypeEXT type;
	uint64_t uuid;
	static std::atomic<uint64_t> uuids;
};

/// The base of all Vulkan objects which derive from a VkInstance.
class BaseInstanceObject
{
public:
	BaseInstanceObject(Instance *inst, uint64_t objHandle_, VkDebugReportObjectTypeEXT type_)
	    : baseInstance(inst)
	    , objHandle(objHandle_)
	    , type(type_)
	{
	}

	void log(VkDebugReportFlagsEXT flags, int32_t messageCode, const char *fmt, ...);

	Instance *getInstance() const
	{
		return baseInstance;
	}

protected:
	Instance *baseInstance;

private:
	uint64_t objHandle;
	VkDebugReportObjectTypeEXT type;
};
}
