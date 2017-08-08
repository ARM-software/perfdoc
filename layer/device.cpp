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

#include "device.hpp"
#include "buffer.hpp"
#include "commandbuffer.hpp"
#include "commandpool.hpp"
#include "descriptor_pool.hpp"
#include "descriptor_set.hpp"
#include "descriptor_set_layout.hpp"
#include "device_memory.hpp"
#include "dispatch_helper.hpp"
#include "event.hpp"
#include "framebuffer.hpp"
#include "image.hpp"
#include "instance.hpp"
#include "pipeline.hpp"
#include "pipeline_layout.hpp"
#include "queue.hpp"
#include "render_pass.hpp"
#include "sampler.hpp"
#include "shader_module.hpp"
#include "swapchain.hpp"

namespace MPD
{
Device::Device(Instance *inst, uint64_t objHandle_)
    : BaseInstanceObject(inst, objHandle_, VULKAN_OBJECT_TYPE)
{
}

Device::~Device()
{
}

void Device::setQueue(uint32_t family, uint32_t index, VkQueue queue)
{
	if (family >= queueFamilies.size())
		queueFamilies.resize(family + 1);

	auto &list = queueFamilies[family];
	if (index >= list.size())
		list.resize(index + 1);

	list[index] = queue;
}

VkQueue Device::getQueue(uint32_t family, uint32_t index) const
{
	MPD_ASSERT(family < queueFamilies.size());
	MPD_ASSERT(index < queueFamilies[family].size());
	return queueFamilies[family][index];
}

VkResult Device::init(VkPhysicalDevice gpu_, VkDevice device_, const VkLayerInstanceDispatchTable *pInstanceTable_,
                      VkLayerDispatchTable *pTable_)
{
	gpu = gpu_;
	device = device_;
	pInstanceTable = pInstanceTable_;
	pTable = pTable_;

	getInstanceTable()->GetPhysicalDeviceMemoryProperties(gpu, &memoryProperties);
	getInstanceTable()->GetPhysicalDeviceProperties(gpu, &properties);

	return VK_SUCCESS;
}

void Device::freeDescriptorSets(DescriptorPool *pool)
{
	MPD_ASSERT(pool);
	auto &map = static_cast<std::unordered_map<VkDescriptorSet, std::unique_ptr<DescriptorSet>> &>(maps);

	while (1)
	{
		auto it = map.begin();
		const auto end = map.end();
		while (it != end)
		{
			if (it->second->getPool() == pool)
			{
				map.erase(it);
				break;
			}

			++it;
		}

		if (it == end)
			break;
	}
}

void Device::freeCommandBuffers(CommandPool *pool)
{
	MPD_ASSERT(pool);
	auto &map = static_cast<std::unordered_map<VkCommandBuffer, std::unique_ptr<CommandBuffer>> &>(maps);

	while (1)
	{
		auto it = map.begin();
		const auto end = map.end();
		while (it != end)
		{
			if (it->second->getCommandPool() == pool)
			{
				map.erase(it);
				break;
			}

			++it;
		}

		if (it == end)
			break;
	}
}

const Config &Device::getConfig() const
{
	return baseInstance->getConfig();
}
}
