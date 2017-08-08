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
#include <stdio.h>
#include <stdexcept>
#include <vector>

using namespace std;

namespace MPD
{
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT, uint64_t, size_t,
                                                    int32_t messageCode, const char *pLayerPrefix, const char *, void *pUserData)
{
	if (flags == VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT && !strcmp(pLayerPrefix, "MaliPerfDoc"))
	{
		MPD_ASSERT(messageCode >= 0 && messageCode < MESSAGE_CODE_COUNT);
		static_cast<VulkanTestHelper *>(pUserData)->notifyCallback(static_cast<MessageCodes>(messageCode));
	}
	return VK_FALSE;
}

VulkanTestHelper::VulkanTestHelper()
{
	if (!vulkanSymbolWrapperInitLoader())
		throw runtime_error("Cannot find Vulkan loader.");
	if (!vulkanSymbolWrapperLoadGlobalSymbols())
		throw runtime_error("Failed to load global Vulkan symbols.");

	uint32_t instanceExtensionCount;
	vector<const char *> activeInstanceExtensions;

	vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, nullptr);
	vector<VkExtensionProperties> instanceExtensions(instanceExtensionCount);
	vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, instanceExtensions.data());

	uint32_t instanceLayerCount;
	vkEnumerateInstanceLayerProperties(&instanceLayerCount, nullptr);
	vector<VkLayerProperties> instanceLayers(instanceLayerCount);
	vkEnumerateInstanceLayerProperties(&instanceLayerCount, instanceLayers.data());

	for (auto &layer : instanceLayers)
	{
		uint32_t count;
		vkEnumerateInstanceExtensionProperties(layer.layerName, &count, nullptr);
		vector<VkExtensionProperties> extensions(count);
		vkEnumerateInstanceExtensionProperties(layer.layerName, &count, extensions.data());
		for (auto &ext : extensions)
			instanceExtensions.push_back(ext);
	}

	bool hasDebugReport = false;
	for (auto &ext : instanceExtensions)
	{
		if (strcmp(ext.extensionName, VK_EXT_DEBUG_REPORT_EXTENSION_NAME) == 0)
		{
			hasDebugReport = true;
			break;
		}
	}

	bool hasPerfDocLayer = false;
	for (auto &layer : instanceLayers)
	{
		if (strcmp(layer.layerName, VK_LAYER_ARM_mali_perf_doc) == 0)
		{
			hasPerfDocLayer = true;
			break;
		}
	}

	if (!hasDebugReport)
		runtime_error("Debug report extension not present. Cannot run tests.");
	if (!hasPerfDocLayer)
		runtime_error("PerfDoc layer not present. Cannot run tests.");

	VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	app.pApplicationName = "PerfDoc Tests";
	app.applicationVersion = 0;
	app.pEngineName = "PerfDoc Tests";
	app.engineVersion = 0;
	app.apiVersion = VK_MAKE_VERSION(1, 0, 57);

	VkInstanceCreateInfo instanceInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	instanceInfo.pApplicationInfo = &app;

	const char *ext = VK_EXT_DEBUG_REPORT_EXTENSION_NAME;
	instanceInfo.enabledExtensionCount = 1;
	instanceInfo.ppEnabledExtensionNames = &ext;
	const char *layer = VK_LAYER_ARM_mali_perf_doc;
	instanceInfo.enabledLayerCount = 1;
	instanceInfo.ppEnabledLayerNames = &layer;

	if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS)
		throw runtime_error("Failed to create instance.");

	if (!vulkanSymbolWrapperLoadCoreInstanceSymbols(instance))
		throw runtime_error("Failed to load instance symbols.");

	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(instance, vkCreateDebugReportCallbackEXT);
	VkDebugReportCallbackCreateInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT };
	info.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
	info.pfnCallback = debugCallback;
	info.pUserData = this;
	vkCreateDebugReportCallbackEXT(instance, &info, nullptr, &callback);

	uint32_t gpuCount = 0;
	vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr);
	if (gpuCount < 1)
		throw runtime_error("No physical devices on system.");
	vector<VkPhysicalDevice> gpus(gpuCount);
	vkEnumeratePhysicalDevices(instance, &gpuCount, gpus.data());
	gpu = gpus.front();

	vkGetPhysicalDeviceProperties(gpu, &gpuProperties);
	vkGetPhysicalDeviceMemoryProperties(gpu, &memoryProperties);

	uint32_t queueCount;
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueCount, nullptr);
	vector<VkQueueFamilyProperties> queueProperties(queueCount);
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueCount, queueProperties.data());
	if (queueCount < 1)
		throw runtime_error("Failed to query number of queues.");

	uint32_t deviceLayerCount;
	vkEnumerateDeviceLayerProperties(gpu, &deviceLayerCount, nullptr);
	vector<VkLayerProperties> deviceLayers(deviceLayerCount);
	vkEnumerateDeviceLayerProperties(gpu, &deviceLayerCount, deviceLayers.data());

	hasPerfDocLayer = false;
	for (auto &layer : deviceLayers)
	{
		if (strcmp(layer.layerName, VK_LAYER_ARM_mali_perf_doc) == 0)
		{
			hasPerfDocLayer = true;
			break;
		}
	}

	if (!hasPerfDocLayer)
		throw runtime_error("No PerfDoc device layer present.");

	uint32_t queueIndex = VK_QUEUE_FAMILY_IGNORED;
	for (unsigned i = 0; i < queueCount; i++)
	{
		const VkQueueFlags required = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
		if ((queueProperties[i].queueFlags & required) == required)
		{
			queueIndex = i;
			break;
		}
	}

	if (queueIndex == VK_QUEUE_FAMILY_IGNORED)
		throw runtime_error("Could not find queue family.");

	static const float one = 1.0f;
	VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	queueInfo.queueFamilyIndex = queueIndex;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &one;

	VkPhysicalDeviceFeatures features = {};
	VkDeviceCreateInfo deviceInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueInfo;
	deviceInfo.enabledLayerCount = 1;
	deviceInfo.ppEnabledLayerNames = &layer;
	deviceInfo.pEnabledFeatures = &features;

	if (vkCreateDevice(gpu, &deviceInfo, nullptr, &device) != VK_SUCCESS)
		throw runtime_error("Failed to create device.");

	if (!vulkanSymbolWrapperLoadCoreDeviceSymbols(device))
		throw runtime_error("Failed to load device symbols.");

	vkGetDeviceQueue(device, queueIndex, 0, &queue);

	if (!initialize())
		throw runtime_error("Failed to initialize test.");
}

void VulkanTestHelper::resetCounts()
{
	memset(warningCount, 0, sizeof(warningCount));
}

unsigned VulkanTestHelper::getCount(MessageCodes code) const
{
	return warningCount[code];
}

void VulkanTestHelper::notifyCallback(MessageCodes code)
{
	warningCount[code]++;
}

VulkanTestHelper::~VulkanTestHelper()
{
	if (device != VK_NULL_HANDLE)
		vkDeviceWaitIdle(device);

	if (callback != VK_NULL_HANDLE)
	{
		VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(instance, vkDestroyDebugReportCallbackEXT);
		vkDestroyDebugReportCallbackEXT(instance, callback, nullptr);
	}

	if (device != VK_NULL_HANDLE)
		vkDestroyDevice(device, nullptr);

	if (instance != VK_NULL_HANDLE)
		vkDestroyInstance(instance, nullptr);
}
}

int main()
{
	auto *test = MPD::createTest();
	if (!test)
		return 1;

	bool result = test->runTest();
	if (result)
		fprintf(stderr, "Test succeeded!\n");
	else
		fprintf(stderr, "Test failed!\n");

	delete test;
	return result ? 0 : 1;
}
