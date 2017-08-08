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
#include "config.hpp"
#include "dispatch_helper.hpp"
#include "logger.hpp"
#include "perfdoc.hpp"
#include <memory>
#include <stdio.h>

namespace MPD
{
class Instance
{
public:
	bool init(VkInstance instance_, VkLayerInstanceDispatchTable *pTable_, PFN_vkGetInstanceProcAddr gpa_);

	VkInstance getInstance() const
	{
		return instance;
	}

	const VkLayerInstanceDispatchTable *getTable() const
	{
		return pTable;
	}

	PFN_vkVoidFunction getProcAddr(const char *pName)
	{
		return gpa(instance, pName);
	}

	Logger &getLogger()
	{
		return logger;
	}

	const Config &getConfig() const
	{
		return cfg;
	}

private:
	VkInstance instance = VK_NULL_HANDLE;
	VkLayerInstanceDispatchTable *pTable = nullptr;
	PFN_vkGetInstanceProcAddr gpa = nullptr;

	struct FILEDeleter
	{
		void operator()(FILE *file)
		{
			if (file)
				fclose(file);
		}
	};
	std::unique_ptr<FILE, FILEDeleter> cfgLogFile;
	Logger logger;
	Config cfg;
};
}
