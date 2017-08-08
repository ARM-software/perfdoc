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

#include "instance.hpp"
#include <stdio.h>
#include <stdlib.h>

#ifdef ANDROID
#include <android/log.h>
#endif

using namespace std;

namespace MPD
{

static VKAPI_ATTR VkBool32 VKAPI_CALL stdioCallback(VkDebugReportFlagsEXT, VkDebugReportObjectTypeEXT objectType,
                                                    uint64_t object, size_t, int32_t messageCode,
                                                    const char *pLayerPrefix, const char *pMessage, void *pUserData)
{
	fprintf(static_cast<FILE *>(pUserData), "%s (objectType: %d, object: %llu, messageCode: %d): %s\n", pLayerPrefix,
	        objectType, static_cast<unsigned long long>(object), messageCode, pMessage);
	return VK_FALSE;
}

#ifdef ANDROID
static VKAPI_PTR VkBool32 logcatCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType,
                                         uint64_t object, size_t, int32_t messageCode, const char *pLayerPrefix,
                                         const char *pMessage, void *)
{
	int prio;

	switch (flags)
	{
	case VK_DEBUG_REPORT_ERROR_BIT_EXT:
		prio = ANDROID_LOG_ERROR;
		break;

	case VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT:
	case VK_DEBUG_REPORT_WARNING_BIT_EXT:
		prio = ANDROID_LOG_WARN;
		break;

	default:
	case VK_DEBUG_REPORT_INFORMATION_BIT_EXT:
		prio = ANDROID_LOG_INFO;
		break;

	case VK_DEBUG_REPORT_DEBUG_BIT_EXT:
		prio = ANDROID_LOG_DEBUG;
		break;
	}
	__android_log_print(prio, pLayerPrefix, "(objectType: %d, object: %llu, messageCode: %d): %s\n", objectType,
	                    static_cast<unsigned long long>(object), messageCode, pMessage);
	return VK_FALSE;
}
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static VKAPI_ATTR VkBool32 VKAPI_CALL debugOutputCallback(VkDebugReportFlagsEXT, VkDebugReportObjectTypeEXT objectType,
                                                          uint64_t object, size_t, int32_t messageCode,
                                                          const char *pLayerPrefix, const char *pMessage, void *)
{
	char msgBuffer[10 * 1024];
	sprintf(msgBuffer, "%s (objectType: %d, object: %llu, messageCode: %d): %s\n", pLayerPrefix, objectType,
	        static_cast<unsigned long long>(object), messageCode, pMessage);
	OutputDebugStringA(msgBuffer);
	return VK_FALSE;
}
#endif

#ifdef ANDROID
static string getSystemProperty(const char *key)
{
	// Environment variables are not easy to set on Android.
	// Make use of the system properties instead.
	char value[256];
	char command[256];
	snprintf(command, sizeof(command), "getprop %s", key);

	// __system_get_property is apparently removed in recent NDK, so just use popen.
	size_t len = 0;
	FILE *file = popen(command, "rb");
	if (file)
	{
		len = fread(value, 1, sizeof(value) - 1, file);
		// Last character is a newline, so remove that.
		if (len > 1)
			value[len - 1] = '\0';
		else
			len = 0;
		fclose(file);
	}

	return len ? value : "";
}
#endif

bool Instance::init(VkInstance instance_, VkLayerInstanceDispatchTable *pTable_, PFN_vkGetInstanceProcAddr gpa_)
{
	instance = instance_;
	pTable = pTable_;
	gpa = gpa_;

	// Register default callback
	VkDebugReportCallbackCreateInfoEXT ci = {};

#ifdef ANDROID
	auto configPath = getSystemProperty("debug.mali.perfdoc.config");
	const char *path = configPath.empty() ? nullptr : configPath.c_str();
	auto logFilename = getSystemProperty("debug.mali.perfdoc.log");
#else
	const char *path = getenv("MALI_PERFDOC_CONFIG");
	if (!path)
		path = "mali-perfdoc.cfg";
	const char *logFilename = getenv("MALI_PERFDOC_LOG");
#endif

	const char *dumpPath = getenv("MALI_PERFDOC_CONFIG_DUMP");
	if (dumpPath)
		cfg.dumpToFile(dumpPath);

	if (path)
	{
		if (!cfg.tryToLoadFromFile(path))
		{
#ifdef ANDROID
			__android_log_print(ANDROID_LOG_ERROR, "MaliPerfDoc", "Failed to open PerfDoc config: %s.", path);
#endif
		}
	}

#ifdef ANDROID
	if (cfg.loggingFilename.empty())
		cfg.loggingFilename = logFilename;
#else
	if (cfg.loggingFilename.empty() && logFilename)
		cfg.loggingFilename = logFilename;
#endif

	// Setup custom logging callbacks.
	if (!cfg.loggingFilename.empty())
	{
		const auto &path = cfg.loggingFilename;

		// Handle "special" paths first.
		if (path == "stdout")
		{
			ci.flags = ~0u;
			ci.pfnCallback = stdioCallback;
			ci.pUserData = stdout;
			logger.createAndRegisterCallback(VK_NULL_HANDLE, ci);
		}
		else if (path == "stderr")
		{
			ci.flags = ~0u;
			ci.pfnCallback = stdioCallback;
			ci.pUserData = stderr;
			logger.createAndRegisterCallback(VK_NULL_HANDLE, ci);
		}
#ifdef ANDROID
		else if (path == "logcat")
		{
			ci.flags = ~0u;
			ci.pfnCallback = logcatCallback;
			logger.createAndRegisterCallback(VK_NULL_HANDLE, ci);
		}
#endif
#ifdef _WIN32
		else if (path == "debug_output")
		{
			ci.flags = ~0u;
			ci.pfnCallback = debugOutputCallback;
			logger.createAndRegisterCallback(VK_NULL_HANDLE, ci);
		}
#endif
		else
		{
			// This is a regular file.
			cfgLogFile = unique_ptr<FILE, FILEDeleter>(fopen(path.c_str(), "w"));
			if (cfgLogFile)
			{
				ci.flags = ~0u;
				ci.pfnCallback = stdioCallback;
				ci.pUserData = cfgLogFile.get();
				logger.createAndRegisterCallback(VK_NULL_HANDLE, ci);
			}
		}

		LoggerMessageInfo info;
		info.flags = VK_DEBUG_REPORT_INFORMATION_BIT_EXT;
		info.messageCode = 0;
		info.object = 0;
		info.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT;
		logger.write(info, "Config file debug callback registered.");
	}
	return true;
}
}
