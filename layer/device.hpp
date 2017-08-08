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
#include "base_object.hpp"
#include "config.hpp"
#include <memory>
#include <unordered_map>
#include <vector>
#include <vulkan/vk_layer.h>

namespace MPD
{
class CommandBuffer;
class CommandPool;
class Buffer;
class Image;
class DescriptorSetLayout;
class DescriptorSet;
class DescriptorPool;
class DeviceMemory;
class RenderPass;
class Pipeline;
class Framebuffer;
class ImageView;
class Sampler;
class ShaderModule;
class SwapchainKHR;
class Queue;
class Event;
class PipelineLayout;

#define MPD_OBJECT_MAP(ourType) std::unordered_map<Vk##ourType, std::unique_ptr<ourType>>

class ObjectMaps : public MPD_OBJECT_MAP(CommandBuffer),
                   public MPD_OBJECT_MAP(CommandPool),
                   public MPD_OBJECT_MAP(Buffer),
                   public MPD_OBJECT_MAP(Image),
                   public MPD_OBJECT_MAP(DescriptorSetLayout),
                   public MPD_OBJECT_MAP(DescriptorSet),
                   public MPD_OBJECT_MAP(DescriptorPool),
                   public MPD_OBJECT_MAP(DeviceMemory),
                   public MPD_OBJECT_MAP(RenderPass),
                   public MPD_OBJECT_MAP(Pipeline),
                   public MPD_OBJECT_MAP(Framebuffer),
                   public MPD_OBJECT_MAP(ImageView),
                   public MPD_OBJECT_MAP(Sampler),
                   public MPD_OBJECT_MAP(ShaderModule),
                   public MPD_OBJECT_MAP(Queue),
                   public MPD_OBJECT_MAP(SwapchainKHR),
                   public MPD_OBJECT_MAP(Event),
                   public MPD_OBJECT_MAP(PipelineLayout)
{
};

#undef MPD_OBJECT_MAP

class Device : public BaseInstanceObject
{
public:
	using VulkanType = VkDevice;
	static const VkDebugReportObjectTypeEXT VULKAN_OBJECT_TYPE = VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT;

	Device(Instance *inst, uint64_t objHandle_);

	~Device();

	VkResult init(VkPhysicalDevice gpu_, VkDevice device_, const VkLayerInstanceDispatchTable *pInstanceTable_,
	              VkLayerDispatchTable *pTable_);

	VkDevice getDevice() const
	{
		return device;
	}

	const VkLayerDispatchTable *getTable() const
	{
		return pTable;
	}

	const VkLayerInstanceDispatchTable *getInstanceTable() const
	{
		return pInstanceTable;
	}

	const VkPhysicalDeviceMemoryProperties &getMemoryProperties() const
	{
		return memoryProperties;
	}

	const VkPhysicalDeviceProperties &getProperties() const
	{
		return properties;
	}

	void setQueue(uint32_t family, uint32_t index, VkQueue queue);
	VkQueue getQueue(uint32_t family, uint32_t index) const;

	template <typename T>
	T *alloc(typename T::VulkanType handle)
	{
		using VkType = typename T::VulkanType;
		using MapType = std::unordered_map<VkType, std::unique_ptr<T>>;
		auto &map = static_cast<MapType &>(maps);

		MPD_ASSERT(map.find(handle) == map.end());
		// Reinterpret cast while changing integer size doesn't work on MSVC.
		T *n = new T(this, (uint64_t)handle);
		map[handle] = std::unique_ptr<T>(n);
		return n;
	}

	template <class T>
	T *get(typename T::VulkanType handle)
	{
		using VkType = typename T::VulkanType;
		using MapType = std::unordered_map<VkType, std::unique_ptr<T>>;
		auto &map = static_cast<MapType &>(maps);

		auto it = map.find(handle);
		if (it != map.end())
			return it->second.get();
		else
			return nullptr;
	}

	template <class T>
	void destroy(typename T::VulkanType handle)
	{
		if (handle == VK_NULL_HANDLE)
			return;

		using VkType = typename T::VulkanType;
		using MapType = std::unordered_map<VkType, std::unique_ptr<T>>;
		auto &map = static_cast<MapType &>(maps);

		auto it = map.find(handle);
		MPD_ASSERT(it != map.end());
		map.erase(it);
	}

	void freeDescriptorSets(DescriptorPool *pool);
	void freeCommandBuffers(CommandPool *pool);

	const Config &getConfig() const;

private:
	VkPhysicalDevice gpu = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	const VkLayerInstanceDispatchTable *pInstanceTable = nullptr;
	VkLayerDispatchTable *pTable = nullptr;

	ObjectMaps maps;
	VkPhysicalDeviceMemoryProperties memoryProperties;
	VkPhysicalDeviceProperties properties;

	std::vector<std::vector<VkQueue>> queueFamilies;
};
}
