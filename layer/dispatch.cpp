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

#include <algorithm>
#include <fstream>
#include <mutex>
#include <utility>
#include <vector>
#include <vulkan/vk_layer.h>

#include "device.hpp"
#include "dispatch_helper.hpp"
#include "instance.hpp"
#include "logger.hpp"
#include "message_codes.hpp"

#include "buffer.hpp"
#include "commandbuffer.hpp"
#include "commandpool.hpp"
#include "descriptor_pool.hpp"
#include "descriptor_set.hpp"
#include "descriptor_set_layout.hpp"
#include "device_memory.hpp"
#include "event.hpp"
#include "framebuffer.hpp"
#include "image.hpp"
#include "pipeline.hpp"
#include "pipeline_layout.hpp"
#include "queue.hpp"
#include "render_pass.hpp"
#include "sampler.hpp"
#include "shader_module.hpp"
#include "swapchain.hpp"

using namespace std;

namespace MPD
{

// Global data structures to remap VkInstance and VkDevice to internal data structures.
static mutex globalLock;
static InstanceTable instanceDispatch;
static DeviceTable deviceDispatch;
static unordered_map<void *, unique_ptr<Instance>> instanceData;
static unordered_map<void *, unique_ptr<Device>> deviceData;

static VKAPI_ATTR void VKAPI_CALL GetDeviceQueue(VkDevice device, uint32_t familyIndex, uint32_t index, VkQueue *pQueue)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);
	*pQueue = layer->getQueue(familyIndex, index);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice gpu, const VkDeviceCreateInfo *pCreateInfo,
                                                   const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
	auto *layer = getLayerData(getDispatchKey(gpu), instanceData);
	MPD_ASSERT(layer);

	auto *chainInfo = getChainInfo(pCreateInfo, VK_LAYER_LINK_INFO);

	MPD_ASSERT(chainInfo->u.pLayerInfo);
	auto fpGetInstanceProcAddr = chainInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
	auto fpGetDeviceProcAddr = chainInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
	auto fpCreateDevice =
	    reinterpret_cast<PFN_vkCreateDevice>(fpGetInstanceProcAddr(layer->getInstance(), "vkCreateDevice"));
	if (!fpCreateDevice)
		return VK_ERROR_INITIALIZATION_FAILED;

	// Advance the link info for the next element on the chain
	chainInfo->u.pLayerInfo = chainInfo->u.pLayerInfo->pNext;

	auto res = fpCreateDevice(gpu, pCreateInfo, pAllocator, pDevice);
	if (res != VK_SUCCESS)
		return res;

	auto *device = createLayerData(getDispatchKey(*pDevice), deviceData, layer, reinterpret_cast<uintptr_t>(pDevice));

	res =
	    device->init(gpu, *pDevice, layer->getTable(), initDeviceTable(*pDevice, fpGetDeviceProcAddr, deviceDispatch));
	if (res != VK_SUCCESS)
	{
		void *key = getDispatchKey(*pDevice);
		auto fpDestroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(fpGetDeviceProcAddr(*pDevice, "vkDestroyDevice"));
		if (fpDestroyDevice)
			fpDestroyDevice(*pDevice, pAllocator);
		destroyLayerData(key, deviceData);
		return res;
	}

	for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++)
	{
		uint32_t family = pCreateInfo->pQueueCreateInfos[i].queueFamilyIndex;
		for (uint32_t j = 0; j < pCreateInfo->pQueueCreateInfos[i].queueCount; j++)
		{
			VkQueue queue;
			device->getTable()->GetDeviceQueue(*pDevice, family, j, &queue);
			device->setQueue(family, j, queue);

			auto *pQueue = device->alloc<Queue>(queue);
			MPD_ASSERT(pQueue);
			res = pQueue->init(queue);
			if (res != VK_SUCCESS)
			{
				void *key = getDispatchKey(*pDevice);
				auto fpDestroyDevice =
				    reinterpret_cast<PFN_vkDestroyDevice>(fpGetDeviceProcAddr(*pDevice, "vkDestroyDevice"));
				if (fpDestroyDevice)
					fpDestroyDevice(*pDevice, pAllocator);
				destroyLayerData(key, deviceData);
				return res;
			}
		}
	}

	return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                                                     const VkAllocationCallbacks *pAllocator, VkInstance *pInstance)
{
	lock_guard<mutex> holder{ globalLock };

	auto *chainInfo = getChainInfo(pCreateInfo, VK_LAYER_LINK_INFO);
	MPD_ASSERT(chainInfo->u.pLayerInfo);

	auto fpGetInstanceProcAddr = chainInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
	auto fpCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(fpGetInstanceProcAddr(nullptr, "vkCreateInstance"));
	if (!fpCreateInstance)
		return VK_ERROR_INITIALIZATION_FAILED;

	chainInfo->u.pLayerInfo = chainInfo->u.pLayerInfo->pNext;
	auto res = fpCreateInstance(pCreateInfo, pAllocator, pInstance);
	if (res != VK_SUCCESS)
		return res;

	auto *layer = createLayerData(getDispatchKey(*pInstance), instanceData);
	if (!layer->init(*pInstance, initInstanceTable(*pInstance, fpGetInstanceProcAddr, instanceDispatch),
	                 fpGetInstanceProcAddr))
	{
		void *key = getDispatchKey(*pInstance);
		auto fpDestroyInstance =
		    reinterpret_cast<PFN_vkDestroyInstance>(fpGetInstanceProcAddr(*pInstance, "vkDestroyInstance"));
		if (fpDestroyInstance)
			fpDestroyInstance(*pInstance, pAllocator);
		destroyLayerData(key, instanceData);
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator)
{
	lock_guard<mutex> holder{ globalLock };

	void *key = getDispatchKey(instance);
	auto *layer = getLayerData(key, instanceData);
	layer->getTable()->DestroyInstance(instance, pAllocator);
	destroyLayerData(key, instanceData);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo,
                                                        const VkAllocationCallbacks *pAllocator,
                                                        VkCommandPool *pCommandPool)
{
	lock_guard<mutex> holder{ globalLock };

	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	VkResult result = layer->getTable()->CreateCommandPool(device, pCreateInfo, pAllocator, pCommandPool);
	if (result == VK_SUCCESS)
	{
		auto *commandPool = layer->alloc<CommandPool>(*pCommandPool);
		MPD_ASSERT(commandPool != NULL);

		result = commandPool->init(*pCommandPool);
		if (result != VK_SUCCESS)
		{
			layer->destroy<CommandPool>(*pCommandPool);
		}
		else
		{
			if (pCreateInfo->flags & VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)
			{
				commandPool->log(
				    VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_COMMAND_BUFFER_RESET,
				    "VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT is set. Consider resetting entire pool instead.");
			}
		}
	}

	return result;
}

static VKAPI_ATTR void VKAPI_CALL DestroyCommandPool(VkDevice device, VkCommandPool commandPool,
                                                     const VkAllocationCallbacks *pAllocator)
{
	lock_guard<mutex> holder{ globalLock };

	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);
	layer->getTable()->DestroyCommandPool(device, commandPool, pAllocator);

	// destroyCommandPool will also destroy any commandbuffers allocated to this pool
	layer->destroy<CommandPool>(commandPool);
}

static VKAPI_ATTR VkResult VKAPI_CALL AllocateCommandBuffers(VkDevice device,
                                                             const VkCommandBufferAllocateInfo *pAllocateInfo,
                                                             VkCommandBuffer *pCommandBuffers)
{
	lock_guard<mutex> holder{ globalLock };

	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);
	VkResult result = layer->getTable()->AllocateCommandBuffers(device, pAllocateInfo, pCommandBuffers);
	if (result == VK_SUCCESS)
	{
		CommandPool *pCommandPool = layer->get<CommandPool>(pAllocateInfo->commandPool);
		MPD_ASSERT(pCommandPool != NULL);

		for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; i++)
		{
			auto *commandBuffer = layer->alloc<CommandBuffer>(pCommandBuffers[i]);
			result = commandBuffer->init(pCommandBuffers[i], pCommandPool);

			if (result == VK_SUCCESS)
			{
				commandBuffer->setIsSecondaryCommandBuffer(pAllocateInfo->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY);
				pCommandPool->addCommandBuffer(commandBuffer);
			}
			else
			{
				layer->destroy<CommandBuffer>(pCommandBuffers[i]);
			}
		}
	}

	return result;
}

static VKAPI_ATTR void VKAPI_CALL FreeCommandBuffers(VkDevice device, VkCommandPool commandPool,
                                                     uint32_t commandBufferCount,
                                                     const VkCommandBuffer *pCommandBuffers)
{
	lock_guard<mutex> holder{ globalLock };

	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);
	layer->getTable()->FreeCommandBuffers(device, commandPool, commandBufferCount, pCommandBuffers);

	for (uint32_t i = 0; i < commandBufferCount; i++)
	{
		// destroy internal commandbuffer and remove from pool
		layer->destroy<CommandBuffer>(pCommandBuffers[i]);
	}
}

static VKAPI_ATTR VkResult VKAPI_CALL BeginCommandBuffer(VkCommandBuffer commandBuffer,
                                                         const VkCommandBufferBeginInfo *pBeginInfo)
{
	lock_guard<mutex> holder{ globalLock };

	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	CommandBuffer *pCommandBuffer = layer->get<CommandBuffer>(commandBuffer);
	pCommandBuffer->reset();

	if (pBeginInfo->flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT)
	{
		pCommandBuffer->setCurrentRenderPass(layer->get<RenderPass>(pBeginInfo->pInheritanceInfo->renderPass));
		pCommandBuffer->setCurrentSubpassIndex(pBeginInfo->pInheritanceInfo->subpass);
	}

	if (pBeginInfo->flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)
	{
		MPD_ASSERT(pCommandBuffer);
		pCommandBuffer->log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_COMMAND_BUFFER_SIMULTANEOUS_USE,
		                    "VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT is set.");
	}
	return layer->getTable()->BeginCommandBuffer(commandBuffer, pBeginInfo);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateEvent(VkDevice device, const VkEventCreateInfo *pCreateInfo,
                                                  const VkAllocationCallbacks *pAllocator, VkEvent *pEvent)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	auto res = layer->getTable()->CreateEvent(device, pCreateInfo, pAllocator, pEvent);
	if (res == VK_SUCCESS)
	{
		auto *event = layer->alloc<Event>(*pEvent);
		MPD_ASSERT(event);
		res = event->init(*pEvent, *pCreateInfo);
		if (res != VK_SUCCESS)
		{
			layer->destroy<Event>(*pEvent);
			layer->getTable()->DestroyEvent(device, *pEvent, pAllocator);
		}
	}
	return res;
}

static VKAPI_ATTR VkResult ResetEvent(VkDevice device, VkEvent event)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	auto *ev = layer->get<Event>(event);
	MPD_ASSERT(ev);
	ev->reset();

	return layer->getTable()->ResetEvent(device, event);
}

static VKAPI_ATTR VkResult SetEvent(VkDevice device, VkEvent event)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	auto *ev = layer->get<Event>(event);
	MPD_ASSERT(ev);
	ev->signal();

	return layer->getTable()->SetEvent(device, event);
}
static VKAPI_ATTR void CmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	auto *ev = layer->get<Event>(event);
	MPD_ASSERT(ev);

	auto *cmd = layer->get<CommandBuffer>(commandBuffer);
	cmd->enqueueDeferredFunction([=](Queue &queue) { ev->reset(); });

	layer->getTable()->CmdResetEvent(commandBuffer, event, stageMask);
}

static VKAPI_ATTR void CmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	auto *ev = layer->get<Event>(event);
	MPD_ASSERT(ev);

	auto *cmd = layer->get<CommandBuffer>(commandBuffer);
	cmd->enqueueDeferredFunction([=](Queue &queue) {
		auto src = stageMask;
		if (src & VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT)
			src |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		queue.getQueueTracker().signalEvent(*ev, CommandBuffer::vkStagesToTracker(src));
	});

	return layer->getTable()->CmdSetEvent(commandBuffer, event, stageMask);
}

static VKAPI_ATTR void CmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent *pEvents,
                                     VkPipelineStageFlags, VkPipelineStageFlags dstStageMask, uint32_t,
                                     const VkMemoryBarrier *, uint32_t, const VkBufferMemoryBarrier *, uint32_t,
                                     const VkImageMemoryBarrier *)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	auto *cmd = layer->get<CommandBuffer>(commandBuffer);
	for (uint32_t i = 0; i < eventCount; i++)
	{
		auto *ev = layer->get<Event>(pEvents[i]);
		MPD_ASSERT(ev);
		cmd->enqueueDeferredFunction([=](Queue &queue) {
			auto dst = dstStageMask;
			if (dst & VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)
				dst |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			queue.getQueueTracker().waitEvent(*ev, CommandBuffer::vkStagesToTracker(dst));
		});
	}
}

static VKAPI_ATTR void VKAPI_CALL DestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks *pAllocator)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	layer->destroy<Event>(event);
	layer->getTable()->DestroyEvent(device, event, pAllocator);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo,
                                                   const VkAllocationCallbacks *pCallbacks, VkBuffer *pBuffer)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	auto res = layer->getTable()->CreateBuffer(device, pCreateInfo, pCallbacks, pBuffer);
	if (res == VK_SUCCESS)
	{
		auto *buffer = layer->alloc<Buffer>(*pBuffer);
		MPD_ASSERT(buffer);
		res = buffer->init(*pBuffer, *pCreateInfo);

		if (res != VK_SUCCESS)
		{
			layer->destroy<Buffer>(*pBuffer);
			layer->getTable()->DestroyBuffer(device, *pBuffer, pCallbacks);
		}
	}
	return res;
}

static VKAPI_ATTR VkResult VKAPI_CALL BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory,
                                                       VkDeviceSize offset)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	auto *pBuffer = layer->get<Buffer>(buffer);
	auto *pMemory = layer->get<DeviceMemory>(memory);
	// Bind to layer first since we cannot recover if the real bind buffer memory succeeded.
	auto res = pBuffer->bindMemory(pMemory, offset);
	if (res == VK_SUCCESS)
		res = layer->getTable()->BindBufferMemory(device, buffer, memory, offset);
	return res;
}

static VKAPI_ATTR VkResult VKAPI_CALL BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory,
                                                      VkDeviceSize offset)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	auto *pImage = layer->get<Image>(image);
	auto *pMemory = layer->get<DeviceMemory>(memory);
	// Bind to layer first since we cannot recover if the real bind image memory succeeded.
	auto res = pImage->bindMemory(pMemory, offset);
	if (res == VK_SUCCESS)
		res = layer->getTable()->BindImageMemory(device, image, memory, offset);
	return res;
}

static VKAPI_ATTR void VKAPI_CALL DestroyBuffer(VkDevice device, VkBuffer buffer,
                                                const VkAllocationCallbacks *pCallbacks)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	layer->destroy<Buffer>(buffer);
	layer->getTable()->DestroyBuffer(device, buffer, pCallbacks);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo,
                                                         const VkAllocationCallbacks *pAllocator,
                                                         VkSwapchainKHR *pSwapchain)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);
	MPD_ASSERT(pSwapchain != nullptr);

	auto res = layer->getTable()->CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
	if (res == VK_SUCCESS)
	{
		uint32_t imageCount = 0;
		res = layer->getTable()->GetSwapchainImagesKHR(device, *pSwapchain, &imageCount, nullptr);
		if (res != VK_SUCCESS || !imageCount)
		{
			if (res == VK_SUCCESS)
				res = VK_ERROR_OUT_OF_HOST_MEMORY;

			layer->getTable()->DestroySwapchainKHR(device, *pSwapchain, pAllocator);
			return res;
		}

		vector<VkImage> swapchainImages(imageCount);
		res = layer->getTable()->GetSwapchainImagesKHR(device, *pSwapchain, &imageCount, swapchainImages.data());
		if (res != VK_SUCCESS)
		{
			layer->getTable()->DestroySwapchainKHR(device, *pSwapchain, pAllocator);
			return res;
		}

		if (pCreateInfo->oldSwapchain != VK_NULL_HANDLE)
		{
			auto *oldSwapchain = layer->get<SwapchainKHR>(pCreateInfo->oldSwapchain);
			MPD_ASSERT(oldSwapchain != nullptr);
			for (auto &swapchainImage : swapchainImages)
				if (oldSwapchain->potentiallySteal(swapchainImage))
					layer->destroy<Image>(swapchainImage);
		}

		VkImageCreateInfo imageCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		imageCreateInfo.extent.width = pCreateInfo->imageExtent.width;
		imageCreateInfo.extent.height = pCreateInfo->imageExtent.height;
		imageCreateInfo.extent.depth = 1;
		imageCreateInfo.arrayLayers = pCreateInfo->imageArrayLayers;
		imageCreateInfo.format = pCreateInfo->imageFormat;
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL; // Not necessarily known, but the layer shouldn't really care.
		imageCreateInfo.sharingMode = pCreateInfo->imageSharingMode;
		imageCreateInfo.usage = pCreateInfo->imageUsage;
		// Ignore pQueueFamilyIndices, we aren't using it for anything.

		for (auto &swapchainImage : swapchainImages)
		{
			auto *image = layer->alloc<Image>(swapchainImage);
			MPD_ASSERT(image);

			res = image->initSwapchain(swapchainImage, imageCreateInfo);

			if (res != VK_SUCCESS)
			{
				for (int i = 0; i <= (&swapchainImage - swapchainImages.data()); i++)
					layer->destroy<Image>(swapchainImages[i]);
				layer->getTable()->DestroySwapchainKHR(device, *pSwapchain, pAllocator);
				return res;
			}
		}

		auto *swapchain = layer->alloc<SwapchainKHR>(*pSwapchain);
		MPD_ASSERT(swapchain);

		res = swapchain->init(*pSwapchain, *pCreateInfo, swapchainImages);
		if (res != VK_SUCCESS)
		{
			for (auto &swapchainImage : swapchainImages)
				layer->destroy<Image>(swapchainImage);
			layer->destroy<SwapchainKHR>(*pSwapchain);
			layer->getTable()->DestroySwapchainKHR(device, *pSwapchain, pAllocator);
			return res;
		}
	}
	return res;
}

static VKAPI_ATTR void VKAPI_CALL DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                                                      const VkAllocationCallbacks *pAllocator)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	if (swapchain != VK_NULL_HANDLE)
	{
		auto *chain = layer->get<SwapchainKHR>(swapchain);
		MPD_ASSERT(chain);

		for (auto &image : chain->getSwapchainImages())
		{
			// Swapchain images may have been reused in oldSwapchain.
			if (image != VK_NULL_HANDLE)
				layer->destroy<Image>(image);
		}
		layer->destroy<SwapchainKHR>(swapchain);
	}
	layer->getTable()->DestroySwapchainKHR(device, swapchain, pAllocator);
}

static VKAPI_ATTR VkResult VKAPI_CALL GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain,
                                                            uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages)
{
	// We don't really need to implement this, except for the fact that the unique objects layer
	// does not cache the swapchain images properly so it will create new unique IDs every time it's called.
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	auto *chain = layer->get<SwapchainKHR>(swapchain);
	MPD_ASSERT(chain);
	auto &images = chain->getSwapchainImages();

	if (pSwapchainImages)
	{
		VkResult ret = VK_SUCCESS;
		auto toWrite = min(*pSwapchainImageCount, uint32_t(images.size()));
		if (toWrite < *pSwapchainImageCount)
			ret = VK_INCOMPLETE;

		memcpy(pSwapchainImages, images.data(), toWrite * sizeof(VkImage));
		*pSwapchainImageCount = toWrite;
		return ret;
	}
	else
	{
		*pSwapchainImageCount = images.size();
		return VK_SUCCESS;
	}
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo,
                                                  const VkAllocationCallbacks *pCallbacks, VkImage *pImage)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	auto res = layer->getTable()->CreateImage(device, pCreateInfo, pCallbacks, pImage);
	if (res == VK_SUCCESS)
	{
		auto *image = layer->alloc<Image>(*pImage);
		MPD_ASSERT(image);
		res = image->init(*pImage, *pCreateInfo);

		if (res != VK_SUCCESS)
		{
			layer->destroy<Image>(*pImage);
			layer->getTable()->DestroyImage(device, *pImage, pCallbacks);
		}
	}
	return res;
}

static VKAPI_ATTR void VKAPI_CALL GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer,
                                                              VkMemoryRequirements *pMemoryRequirements)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	Buffer *pBuffer = layer->get<Buffer>(buffer);
	MPD_ASSERT(pBuffer);
	*pMemoryRequirements = pBuffer->getMemoryRequirements();
}

static VKAPI_ATTR VkResult VKAPI_CALL AllocateMemory(VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo,
                                                     const VkAllocationCallbacks *pCallbacks, VkDeviceMemory *pMemory)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	auto res = layer->getTable()->AllocateMemory(device, pAllocateInfo, pCallbacks, pMemory);
	if (res == VK_SUCCESS)
	{
		auto *memory = layer->alloc<DeviceMemory>(*pMemory);
		MPD_ASSERT(memory);

		res = memory->init(*pMemory, *pAllocateInfo);

		if (res != VK_SUCCESS)
		{
			layer->destroy<DeviceMemory>(*pMemory);
			layer->getTable()->FreeMemory(device, *pMemory, pCallbacks);
		}
	}
	return res;
}

static VKAPI_ATTR VkResult VKAPI_CALL MapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset,
                                                VkDeviceSize size, VkMemoryMapFlags flags, void **ppData)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	DeviceMemory *device_memory = layer->get<DeviceMemory>(memory);
	MPD_ASSERT(device_memory);

	void *mappedMemory = device_memory->getMappedMemory();
	if (mappedMemory == NULL)
	{
		return layer->getTable()->MapMemory(device, memory, offset, size, flags, ppData);
	}

	*ppData = (uint8_t *)mappedMemory + offset;
	return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL UnmapMemory(VkDevice device, VkDeviceMemory memory)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	DeviceMemory *device_memory = layer->get<DeviceMemory>(memory);
	MPD_ASSERT(device_memory);

	if (device_memory->getMappedMemory() == NULL)
	{
		layer->getTable()->UnmapMemory(device, memory);
	}
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo,
                                                       const VkAllocationCallbacks *pAllocator,
                                                       VkRenderPass *pRenderPass)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	auto res = layer->getTable()->CreateRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
	if (res == VK_SUCCESS)
	{
		auto *renderPass = layer->alloc<RenderPass>(*pRenderPass);
		MPD_ASSERT(renderPass);
		res = renderPass->init(*pRenderPass, *pCreateInfo);
		if (res != VK_SUCCESS)
		{
			layer->destroy<RenderPass>(*pRenderPass);
			layer->getTable()->DestroyRenderPass(device, *pRenderPass, pAllocator);
		}
	}
	return res;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache,
                                                              uint32_t createInfoCount,
                                                              const VkGraphicsPipelineCreateInfo *pCreateInfos,
                                                              const VkAllocationCallbacks *pAllocator,
                                                              VkPipeline *pPipelines)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	if (pipelineCache == VK_NULL_HANDLE)
	{
		layer->log(
		    VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_NO_PIPELINE_CACHE,
		    "Creating a pipeline without pipeline cache, it is highly recommended to always use a pipeline cache, "
		    "even if it is not preloaded from disk.");
	}

	auto res = layer->getTable()->CreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos,
	                                                      pAllocator, pPipelines);
	if (res == VK_SUCCESS)
	{
		for (uint32_t i = 0; i < createInfoCount; i++)
		{
			auto *pipeline = layer->alloc<Pipeline>(pPipelines[i]);
			MPD_ASSERT(pipeline);
			res = pipeline->initGraphics(pPipelines[i], pCreateInfos[i]);
			if (res != VK_SUCCESS)
			{
				for (uint32_t j = 0; j <= i; j++)
					layer->destroy<Pipeline>(pPipelines[j]);
				for (uint32_t j = 0; j < createInfoCount; j++)
					layer->getTable()->DestroyPipeline(device, pPipelines[j], pAllocator);
				break;
			}
		}
	}
	return res;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache,
                                                             uint32_t createInfoCount,
                                                             const VkComputePipelineCreateInfo *pCreateInfos,
                                                             const VkAllocationCallbacks *pAllocator,
                                                             VkPipeline *pPipelines)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	if (pipelineCache == VK_NULL_HANDLE)
	{
		layer->log(
		    VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_NO_PIPELINE_CACHE,
		    "Creating a pipeline without pipeline cache, it is highly recommended to always use a pipeline cache, "
		    "even if it is not preloaded from disk.");
	}

	auto res = layer->getTable()->CreateComputePipelines(device, pipelineCache, createInfoCount, pCreateInfos,
	                                                     pAllocator, pPipelines);
	if (res == VK_SUCCESS)
	{
		for (uint32_t i = 0; i < createInfoCount; i++)
		{
			auto *pipeline = layer->alloc<Pipeline>(pPipelines[i]);
			MPD_ASSERT(pipeline);
			res = pipeline->initCompute(pPipelines[i], pCreateInfos[i]);
			if (res != VK_SUCCESS)
			{
				for (uint32_t j = 0; j <= i; j++)
					layer->destroy<Pipeline>(pPipelines[j]);
				for (uint32_t j = 0; j < createInfoCount; j++)
					layer->getTable()->DestroyPipeline(device, pPipelines[j], pAllocator);
				break;
			}
		}
	}
	return res;
}

static VKAPI_ATTR void VKAPI_CALL DestroyPipeline(VkDevice device, VkPipeline pipeline,
                                                  const VkAllocationCallbacks *pAllocator)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);
	layer->destroy<Pipeline>(pipeline);
	layer->getTable()->DestroyPipeline(device, pipeline, pAllocator);
}

static VKAPI_ATTR void VKAPI_CALL DestroyRenderPass(VkDevice device, VkRenderPass renderPass,
                                                    const VkAllocationCallbacks *pAllocator)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	layer->destroy<RenderPass>(renderPass);
	layer->getTable()->DestroyRenderPass(device, renderPass, pAllocator);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo *pCreateInfo,
                                                        const VkAllocationCallbacks *pAllocator,
                                                        VkFramebuffer *pFramebuffer)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	auto res = layer->getTable()->CreateFramebuffer(device, pCreateInfo, pAllocator, pFramebuffer);
	if (res == VK_SUCCESS)
	{
		auto *framebuffer = layer->alloc<Framebuffer>(*pFramebuffer);
		MPD_ASSERT(framebuffer);
		res = framebuffer->init(*pFramebuffer, *pCreateInfo);
		if (res != VK_SUCCESS)
		{
			layer->destroy<Framebuffer>(*pFramebuffer);
			layer->getTable()->DestroyFramebuffer(device, *pFramebuffer, pAllocator);
		}
	}
	return res;
}

static VKAPI_ATTR void VKAPI_CALL DestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer,
                                                     const VkAllocationCallbacks *pAllocator)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	layer->destroy<Framebuffer>(framebuffer);
	layer->getTable()->DestroyFramebuffer(device, framebuffer, pAllocator);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateImageView(VkDevice device, const VkImageViewCreateInfo *pCreateInfo,
                                                      const VkAllocationCallbacks *pAllocator, VkImageView *pImageView)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	auto res = layer->getTable()->CreateImageView(device, pCreateInfo, pAllocator, pImageView);
	if (res == VK_SUCCESS)
	{
		auto *imageView = layer->alloc<ImageView>(*pImageView);
		MPD_ASSERT(imageView);
		res = imageView->init(*pImageView, *pCreateInfo);
		if (res != VK_SUCCESS)
		{
			layer->destroy<ImageView>(*pImageView);
			layer->getTable()->DestroyImageView(device, *pImageView, pAllocator);
		}
	}
	return res;
}

static VKAPI_ATTR void VKAPI_CALL DestroyImageView(VkDevice device, VkImageView imageView,
                                                   const VkAllocationCallbacks *pAllocator)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	layer->destroy<ImageView>(imageView);
	layer->getTable()->DestroyImageView(device, imageView, pAllocator);
}

static VKAPI_ATTR void VKAPI_CALL FreeMemory(VkDevice device, VkDeviceMemory memory,
                                             const VkAllocationCallbacks *pCallbacks)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	layer->destroy<DeviceMemory>(memory);
	layer->getTable()->FreeMemory(device, memory, pCallbacks);
}

static VKAPI_ATTR void VKAPI_CALL DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks *pCallbacks)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	layer->destroy<Image>(image);
	layer->getTable()->DestroyImage(device, image, pCallbacks);
}

static VKAPI_ATTR void VKAPI_CALL CmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                                                  VkImageLayout srcImageLayout, VkImage dstImage,
                                                  VkImageLayout dstImageLayout, uint32_t regionCount,
                                                  const VkImageResolve *pRegions)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);
	auto *cmd = layer->get<CommandBuffer>(commandBuffer);

	cmd->enqueueDeferredFunction([=](Queue &queue) { queue.getQueueTracker().pushWork(QueueTracker::STAGE_TRANSFER); });

	auto *src = layer->get<Image>(srcImage);
	auto *dst = layer->get<Image>(dstImage);

	for (uint32_t i = 0; i < regionCount; i++)
	{
		// Capture-by-value is vital.
		auto srcRegion = pRegions[i].srcSubresource;
		auto dstRegion = pRegions[i].dstSubresource;

		cmd->enqueueDeferredFunction([=](Queue &) {
			src->signalUsage(srcRegion, Image::Usage::ResourceRead);
			dst->signalUsage(dstRegion, Image::Usage::ResourceWrite);
		});
	}

	// Using this function is always a really bad idea, flat out warn on any use of this function.
	cmd->log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_RESOLVE_IMAGE,
	         "Attempting to use vkCmdResolveImage to resolve a multisampled image. "
	         "This is a very slow and extremely bandwidth intensive path. "
	         "You should always resolve multisampled images on-tile with pResolveAttachments in VkRenderPass. "
	         "This is effectively \"free\" on Mali GPUs.");

	layer->getTable()->CmdResolveImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount,
	                                   pRegions);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreatePipelineLayout(VkDevice device,
                                                           const VkPipelineLayoutCreateInfo *pCreateInfo,
                                                           const VkAllocationCallbacks *pAllocator,
                                                           VkPipelineLayout *pLayout)
{
	lock_guard<mutex> holder{ globalLock };

	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	VkResult result = layer->getTable()->CreatePipelineLayout(device, pCreateInfo, pAllocator, pLayout);
	if (result == VK_SUCCESS)
	{
		auto *layout = layer->alloc<PipelineLayout>(*pLayout);
		MPD_ASSERT(layout != nullptr);

		result = layout->init(pCreateInfo);
		if (result != VK_SUCCESS)
		{
			layer->destroy<PipelineLayout>(*pLayout);
		}
	}

	return result;
}

static VKAPI_ATTR void VKAPI_CALL DestroyPipelineLayout(VkDevice device, VkPipelineLayout layout,
                                                        const VkAllocationCallbacks *pAllocator)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	layer->destroy<PipelineLayout>(layout);
	layer->getTable()->DestroyPipelineLayout(device, layout, pAllocator);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateDescriptorSetLayout(VkDevice device,
                                                                const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                                                                const VkAllocationCallbacks *pAllocator,
                                                                VkDescriptorSetLayout *pSetLayout)
{
	lock_guard<mutex> holder{ globalLock };

	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	VkResult result = layer->getTable()->CreateDescriptorSetLayout(device, pCreateInfo, pAllocator, pSetLayout);
	if (result == VK_SUCCESS)
	{
		auto *dsetLayout = layer->alloc<DescriptorSetLayout>(*pSetLayout);
		MPD_ASSERT(dsetLayout != NULL);

		result = dsetLayout->init(pCreateInfo);
		if (result != VK_SUCCESS)
		{
			layer->destroy<DescriptorSetLayout>(*pSetLayout);
		}
	}

	return result;
}

static VKAPI_ATTR void VKAPI_CALL DestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout layout,
                                                             const VkAllocationCallbacks *pCallbacks)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	layer->destroy<DescriptorSetLayout>(layout);
	layer->getTable()->DestroyDescriptorSetLayout(device, layout, pCallbacks);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateDescriptorPool(VkDevice device,
                                                           const VkDescriptorPoolCreateInfo *pCreateInfo,
                                                           const VkAllocationCallbacks *pAllocator,
                                                           VkDescriptorPool *pDescriptorPool)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	VkResult result = layer->getTable()->CreateDescriptorPool(device, pCreateInfo, pAllocator, pDescriptorPool);
	if (result == VK_SUCCESS)
	{
		auto *pool = layer->alloc<DescriptorPool>(*pDescriptorPool);
		MPD_ASSERT(pool != NULL);

		result = pool->init(*pCreateInfo);
		if (result != VK_SUCCESS)
		{
			layer->destroy<DescriptorPool>(*pDescriptorPool);
		}
	}

	return result;
}

static VKAPI_ATTR void VKAPI_CALL DestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                                                        const VkAllocationCallbacks *pAllocator)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	layer->destroy<DescriptorPool>(descriptorPool);
	layer->getTable()->DestroyDescriptorPool(device, descriptorPool, pAllocator);
}

static VKAPI_ATTR VkResult VKAPI_CALL ResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                                                          VkDescriptorPoolResetFlags flags)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);
	auto *pool = layer->get<DescriptorPool>(descriptorPool);
	pool->reset();

	return layer->getTable()->ResetDescriptorPool(device, descriptorPool, flags);
}

static VKAPI_ATTR VkResult VKAPI_CALL AllocateDescriptorSets(VkDevice device,
                                                             const VkDescriptorSetAllocateInfo *pAllocateInfo,
                                                             VkDescriptorSet *pDescriptorSets)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	auto *pool = layer->get<DescriptorPool>(pAllocateInfo->descriptorPool);

	VkResult result = layer->getTable()->AllocateDescriptorSets(device, pAllocateInfo, pDescriptorSets);
	if (result == VK_SUCCESS)
	{
		unsigned i;
		for (i = 0; i < pAllocateInfo->descriptorSetCount && result == VK_SUCCESS; ++i)
		{
			auto *layout = layer->get<DescriptorSetLayout>(pAllocateInfo->pSetLayouts[i]);
			auto *set = layer->alloc<DescriptorSet>(pDescriptorSets[i]);

			result = set->init(layout, pool);
		}

		if (result != VK_SUCCESS)
		{
			for (unsigned j = 0; j < i; ++j)
			{
				layer->destroy<DescriptorSet>(pDescriptorSets[j]);
			}
		}
	}

	return result;
}

static VKAPI_ATTR VkResult VKAPI_CALL FreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool,
                                                         uint32_t descriptorSetCount,
                                                         const VkDescriptorSet *pDescriptorSets)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	for (unsigned i = 0; i < descriptorSetCount; ++i)
	{
		layer->destroy<DescriptorSet>(pDescriptorSets[i]);
	}

	return layer->getTable()->FreeDescriptorSets(device, descriptorPool, descriptorSetCount, pDescriptorSets);
}

static VKAPI_ATTR VkResult VKAPI_CALL
CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT *pCreateInfo,
                             const VkAllocationCallbacks *pAllocator, VkDebugReportCallbackEXT *pMsgCallback)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(instance);
	auto *layer = getLayerData(key, instanceData);

	auto res = layer->getTable()->CreateDebugReportCallbackEXT(instance, pCreateInfo, pAllocator, pMsgCallback);
	if (res == VK_SUCCESS)
	{
		auto *logger = layer->getLogger().createAndRegisterCallback(*pMsgCallback, *pCreateInfo);
		MPD_ASSERT(logger);
		MPD_UNUSED(logger);
	}
	return res;
}

static VKAPI_ATTR void VKAPI_CALL DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback,
                                                                const VkAllocationCallbacks *pAllocator)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(instance);
	auto *layer = getLayerData(key, instanceData);
	layer->getLogger().unregisterAndDestroyCallback(callback);
	// Presumably the idea here is that we terminate at the loader in the end.
	layer->getTable()->DestroyDebugReportCallbackEXT(instance, callback, pAllocator);
}

static VKAPI_ATTR void VKAPI_CALL DebugReportMessageEXT(VkInstance instance, VkDebugReportFlagsEXT flags,
                                                        VkDebugReportObjectTypeEXT objType, uint64_t object,
                                                        size_t location, int32_t msgCode, const char *pLayerPrefix,
                                                        const char *pMsg)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(instance);
	auto *layer = getLayerData(key, instanceData);

	// Presumably the idea here is that we terminate at the loader in the end.
	layer->getTable()->DebugReportMessageEXT(instance, flags, objType, object, location, msgCode, pLayerPrefix, pMsg);
}

static PFN_vkVoidFunction interceptCoreInstanceCommand(const char *pName)
{
	static const struct
	{
		const char *name;
		PFN_vkVoidFunction proc;
	} coreInstanceCommands[] = {
		{ "vkCreateInstance", reinterpret_cast<PFN_vkVoidFunction>(CreateInstance) },
		{ "vkDestroyInstance", reinterpret_cast<PFN_vkVoidFunction>(DestroyInstance) },
		{ "vkGetInstanceProcAddr", reinterpret_cast<PFN_vkVoidFunction>(vkGetInstanceProcAddr) },
		{ "vkCreateDevice", reinterpret_cast<PFN_vkVoidFunction>(CreateDevice) },
	};

	for (auto &cmd : coreInstanceCommands)
		if (strcmp(cmd.name, pName) == 0)
			return cmd.proc;
	return nullptr;
}

static PFN_vkVoidFunction interceptExtensionInstanceCommand(const char *pName)
{
	static const struct
	{
		const char *name;
		PFN_vkVoidFunction proc;
	} coreInstanceCommands[] = {
		{ "vkCreateDebugReportCallbackEXT", reinterpret_cast<PFN_vkVoidFunction>(CreateDebugReportCallbackEXT) },
		{ "vkDestroyDebugReportCallbackEXT", reinterpret_cast<PFN_vkVoidFunction>(DestroyDebugReportCallbackEXT) },
		{ "vkDebugReportMessageEXT", reinterpret_cast<PFN_vkVoidFunction>(DebugReportMessageEXT) },
	};

	for (auto &cmd : coreInstanceCommands)
		if (strcmp(cmd.name, pName) == 0)
			return cmd.proc;
	return nullptr;
}

static VKAPI_ATTR void VKAPI_CALL DestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator)
{
	lock_guard<mutex> holder{ globalLock };

	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);
	layer->getTable()->DestroyDevice(device, pAllocator);
	destroyLayerData(key, deviceData);
}

static VKAPI_ATTR void VKAPI_CALL CmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount,
                                                     const VkCommandBuffer *pCommandBuffers)
{
	lock_guard<mutex> holder{ globalLock };

	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	CommandBuffer *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	for (uint32_t i = 0; i < commandBufferCount; i++)
	{
		CommandBuffer *cb = layer->get<CommandBuffer>(pCommandBuffers[i]);
		MPD_ASSERT(cb);
		cmdBuffer->executeCommandBuffer(cb);
	}

	layer->getTable()->CmdExecuteCommands(commandBuffer, commandBufferCount, pCommandBuffers);
}

static VKAPI_ATTR void VKAPI_CALL CmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                                     VkDeviceSize offset, VkIndexType indexType)
{
	lock_guard<mutex> holder{ globalLock };

	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	CommandBuffer *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	Buffer *index_buffer = layer->get<Buffer>(buffer);
	MPD_ASSERT(index_buffer);

	layer->getTable()->CmdBindIndexBuffer(commandBuffer, buffer, offset, indexType);
	cmdBuffer->bindIndexBuffer(index_buffer, offset, indexType);
}

static VKAPI_ATTR void VKAPI_CALL CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                                                  VkPipeline pipeline)
{
	lock_guard<mutex> holder{ globalLock };

	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	CommandBuffer *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	layer->getTable()->CmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
	cmdBuffer->bindPipeline(pipelineBindPoint, pipeline);
}

static VKAPI_ATTR void VKAPI_CALL CmdBeginRenderPass(VkCommandBuffer commandBuffer,
                                                     const VkRenderPassBeginInfo *pRenderPassBegin,
                                                     VkSubpassContents contents)
{
	lock_guard<mutex> holder{ globalLock };

	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	CommandBuffer *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	layer->getTable()->CmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
	cmdBuffer->beginRenderPass(pRenderPassBegin, contents);
}

static VKAPI_ATTR void VKAPI_CALL CmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents)
{
	lock_guard<mutex> holder{ globalLock };

	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	CommandBuffer *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	layer->getTable()->CmdNextSubpass(commandBuffer, contents);
	cmdBuffer->nextSubpass(contents);
}

static VKAPI_ATTR void VKAPI_CALL CmdEndRenderPass(VkCommandBuffer commandBuffer)
{
	lock_guard<mutex> holder{ globalLock };

	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	CommandBuffer *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	layer->getTable()->CmdEndRenderPass(commandBuffer);
	cmdBuffer->endRenderPass();
}

static VKAPI_ATTR void VKAPI_CALL CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer,
                                                uint32_t regionCount, const VkBufferCopy *pRegions)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	auto *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	cmdBuffer->enqueueDeferredFunction(
	    [=](Queue &queue) { queue.getQueueTracker().pushWork(QueueTracker::STAGE_TRANSFER); });

	layer->getTable()->CmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, regionCount, pRegions);
}

static VKAPI_ATTR void VKAPI_CALL CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                                               VkImageLayout srcImageLayout, VkImage dstImage,
                                               VkImageLayout dstImageLayout, uint32_t regionCount,
                                               const VkImageCopy *pRegions)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	auto *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	auto *src = layer->get<Image>(srcImage);
	auto *dst = layer->get<Image>(dstImage);

	for (uint32_t i = 0; i < regionCount; i++)
	{
		// Capture-by-value is vital.
		auto srcRegion = pRegions[i].srcSubresource;
		auto dstRegion = pRegions[i].dstSubresource;

		cmdBuffer->enqueueDeferredFunction([=](Queue &) {
			src->signalUsage(srcRegion, Image::Usage::ResourceRead);
			dst->signalUsage(dstRegion, Image::Usage::ResourceWrite);
		});
	}

	cmdBuffer->enqueueDeferredFunction(
	    [=](Queue &queue) { queue.getQueueTracker().pushWork(QueueTracker::STAGE_TRANSFER); });

	layer->getTable()->CmdCopyImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount,
	                                pRegions);
}

static VKAPI_ATTR void VKAPI_CALL CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer,
                                                       VkImage dstImage, VkImageLayout dstImageLayout,
                                                       uint32_t regionCount, const VkBufferImageCopy *pRegions)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	auto *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	auto *dst = layer->get<Image>(dstImage);

	for (uint32_t i = 0; i < regionCount; i++)
	{
		// Capture-by-value is vital.
		auto dstRegion = pRegions[i].imageSubresource;

		cmdBuffer->enqueueDeferredFunction([=](Queue &) { dst->signalUsage(dstRegion, Image::Usage::ResourceWrite); });
	}

	cmdBuffer->enqueueDeferredFunction(
	    [=](Queue &queue) { queue.getQueueTracker().pushWork(QueueTracker::STAGE_TRANSFER); });

	layer->getTable()->CmdCopyBufferToImage(commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
}

static VKAPI_ATTR void VKAPI_CALL CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage,
                                                       VkImageLayout srcImageLayout, VkBuffer dstBuffer,
                                                       uint32_t regionCount, const VkBufferImageCopy *pRegions)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	auto *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	auto *src = layer->get<Image>(srcImage);

	for (uint32_t i = 0; i < regionCount; i++)
	{
		// Capture-by-value is vital.
		auto dstRegion = pRegions[i].imageSubresource;

		cmdBuffer->enqueueDeferredFunction([=](Queue &) { src->signalUsage(dstRegion, Image::Usage::ResourceRead); });
	}

	cmdBuffer->enqueueDeferredFunction(
	    [=](Queue &queue) { queue.getQueueTracker().pushWork(QueueTracker::STAGE_TRANSFER); });

	layer->getTable()->CmdCopyImageToBuffer(commandBuffer, srcImage, srcImageLayout, dstBuffer, regionCount, pRegions);
}

static VKAPI_ATTR void VKAPI_CALL CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                                               VkImageLayout srcImageLayout, VkImage dstImage,
                                               VkImageLayout dstImageLayout, uint32_t regionCount,
                                               const VkImageBlit *pRegions, VkFilter filter)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	auto *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	auto *src = layer->get<Image>(srcImage);
	auto *dst = layer->get<Image>(dstImage);

	for (uint32_t i = 0; i < regionCount; i++)
	{
		// Capture-by-value is vital.
		auto srcRegion = pRegions[i].srcSubresource;
		auto dstRegion = pRegions[i].dstSubresource;

		cmdBuffer->enqueueDeferredFunction([=](Queue &) {
			src->signalUsage(srcRegion, Image::Usage::ResourceRead);
			dst->signalUsage(dstRegion, Image::Usage::ResourceWrite);
		});
	}

	cmdBuffer->enqueueDeferredFunction(
	    [=](Queue &queue) { queue.getQueueTracker().pushWork(QueueTracker::STAGE_TRANSFER); });

	layer->getTable()->CmdBlitImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount,
	                                pRegions, filter);
}

static VKAPI_ATTR void VKAPI_CALL CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                                                VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	auto *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	cmdBuffer->enqueueDeferredFunction(
	    [=](Queue &queue) { queue.getQueueTracker().pushWork(QueueTracker::STAGE_TRANSFER); });

	layer->getTable()->CmdFillBuffer(commandBuffer, dstBuffer, dstOffset, size, data);
}

static VKAPI_ATTR void VKAPI_CALL CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                                                  VkDeviceSize dstOffset, VkDeviceSize size, const void *data)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	auto *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	cmdBuffer->enqueueDeferredFunction(
	    [=](Queue &queue) { queue.getQueueTracker().pushWork(QueueTracker::STAGE_TRANSFER); });

	layer->getTable()->CmdUpdateBuffer(commandBuffer, dstBuffer, dstOffset, size, data);
}

static VKAPI_ATTR void VKAPI_CALL CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                                                          uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer,
                                                          VkDeviceSize dstOffset, VkDeviceSize stride,
                                                          VkQueryResultFlags flags)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	auto *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	cmdBuffer->enqueueDeferredFunction(
	    [=](Queue &queue) { queue.getQueueTracker().pushWork(QueueTracker::STAGE_TRANSFER); });

	layer->getTable()->CmdCopyQueryPoolResults(commandBuffer, queryPool, firstQuery, queryCount, dstBuffer, dstOffset,
	                                           stride, flags);
}

static VKAPI_ATTR void VKAPI_CALL UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount,
                                                       const VkWriteDescriptorSet *pDescriptorWrites,
                                                       uint32_t descriptorCopyCount,
                                                       const VkCopyDescriptorSet *pDescriptorCopies)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	for (uint32_t i = 0; i < descriptorWriteCount; i++)
		DescriptorSet::writeDescriptors(layer, pDescriptorWrites[i]);
	for (uint32_t i = 0; i < descriptorCopyCount; i++)
		DescriptorSet::copyDescriptors(layer, pDescriptorCopies[i]);

	layer->getTable()->UpdateDescriptorSets(device, descriptorWriteCount, pDescriptorWrites, descriptorCopyCount,
	                                        pDescriptorCopies);
}

static VKAPI_ATTR void VKAPI_CALL CmdBindDescriptorSets(VkCommandBuffer commandBuffer,
                                                        VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout,
                                                        uint32_t firstSet, uint32_t descriptorSetCount,
                                                        const VkDescriptorSet *pDescriptorSets,
                                                        uint32_t dynamicOffsetCount, const uint32_t *pDynamicOffsets)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	auto *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	cmdBuffer->bindDescriptorSets(pipelineBindPoint, layout, firstSet, descriptorSetCount, pDescriptorSets,
	                              dynamicOffsetCount, pDynamicOffsets);

	layer->getTable()->CmdBindDescriptorSets(commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount,
	                                         pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
}

static VKAPI_ATTR void VKAPI_CALL CmdDispatch(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y, uint32_t z)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	auto *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	cmdBuffer->enqueueDeferredFunction(
	    [=](Queue &queue) { queue.getQueueTracker().pushWork(QueueTracker::STAGE_COMPUTE); });
	layer->getTable()->CmdDispatch(commandBuffer, x, y, z);
	cmdBuffer->enqueueComputeDescriptorSetUsage();
}

static VKAPI_ATTR void VKAPI_CALL CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                                      VkDeviceSize offset)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	auto *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	cmdBuffer->enqueueDeferredFunction(
	    [=](Queue &queue) { queue.getQueueTracker().pushWork(QueueTracker::STAGE_COMPUTE); });
	layer->getTable()->CmdDispatchIndirect(commandBuffer, buffer, offset);
	cmdBuffer->enqueueComputeDescriptorSetUsage();
}

static VKAPI_ATTR void VKAPI_CALL CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image,
                                                     VkImageLayout imageLayout, const VkClearColorValue *pColor,
                                                     uint32_t rangeCount, const VkImageSubresourceRange *pRanges)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	auto *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	auto *dst = layer->get<Image>(image);
	MPD_ASSERT(dst);
	for (uint32_t i = 0; i < rangeCount; i++)
	{
		// Copy-by-value is important.
		auto dstRange = pRanges[i];
		cmdBuffer->enqueueDeferredFunction([=](Queue &) { dst->signalUsage(dstRange, Image::Usage::Cleared); });
	}

	cmdBuffer->enqueueDeferredFunction(
	    [=](Queue &queue) { queue.getQueueTracker().pushWork(QueueTracker::STAGE_TRANSFER); });

	layer->getTable()->CmdClearColorImage(commandBuffer, image, imageLayout, pColor, rangeCount, pRanges);
}

static VKAPI_ATTR void VKAPI_CALL CmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image,
                                                            VkImageLayout imageLayout,
                                                            const VkClearDepthStencilValue *pDepthStencil,
                                                            uint32_t rangeCount, const VkImageSubresourceRange *pRanges)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	auto *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	auto *dst = layer->get<Image>(image);
	MPD_ASSERT(dst);
	for (uint32_t i = 0; i < rangeCount; i++)
	{
		// Copy-by-value is important.
		auto dstRange = pRanges[i];
		cmdBuffer->enqueueDeferredFunction([=](Queue &) { dst->signalUsage(dstRange, Image::Usage::Cleared); });
	}

	cmdBuffer->enqueueDeferredFunction(
	    [=](Queue &queue) { queue.getQueueTracker().pushWork(QueueTracker::STAGE_TRANSFER); });

	layer->getTable()->CmdClearDepthStencilImage(commandBuffer, image, imageLayout, pDepthStencil, rangeCount, pRanges);
}

static VKAPI_ATTR void VKAPI_CALL CmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                                                      const VkClearAttachment *pAttachments, uint32_t rectCount,
                                                      const VkClearRect *pRects)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	auto *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	cmdBuffer->clearAttachments(attachmentCount, pAttachments, rectCount, pRects);
	layer->getTable()->CmdClearAttachments(commandBuffer, attachmentCount, pAttachments, rectCount, pRects);
}

static VKAPI_ATTR void VKAPI_CALL CmdPipelineBarrier(
    VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
    VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers,
    uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	auto *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	cmdBuffer->pipelineBarrier(srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount, pMemoryBarriers,
	                           bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount,
	                           pImageMemoryBarriers);

	layer->getTable()->CmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags,
	                                      memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount,
	                                      pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
}

static VKAPI_ATTR void VKAPI_CALL CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount,
                                          uint32_t firstVertex, uint32_t firstInstance)
{
	lock_guard<mutex> holder{ globalLock };

	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	CommandBuffer *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	layer->getTable()->CmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
	cmdBuffer->draw(vertexCount, instanceCount, firstVertex, firstInstance);
	cmdBuffer->enqueueGraphicsDescriptorSetUsage();
}

static VKAPI_ATTR void VKAPI_CALL CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                                                  uint32_t drawCount, uint32_t stride)
{
	lock_guard<mutex> holder{ globalLock };

	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	CommandBuffer *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	layer->getTable()->CmdDrawIndirect(commandBuffer, buffer, offset, drawCount, stride);
	cmdBuffer->enqueueGraphicsDescriptorSetUsage();
}

static VKAPI_ATTR void VKAPI_CALL CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount,
                                                 uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset,
                                                 uint32_t firstInstance)
{
	lock_guard<mutex> holder{ globalLock };

	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	CommandBuffer *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	layer->getTable()->CmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset,
	                                  firstInstance);
	cmdBuffer->drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
	cmdBuffer->enqueueGraphicsDescriptorSetUsage();
}

static VKAPI_ATTR void VKAPI_CALL CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                                         VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
	lock_guard<mutex> holder{ globalLock };

	void *key = getDispatchKey(commandBuffer);
	auto *layer = getLayerData(key, deviceData);

	CommandBuffer *cmdBuffer = layer->get<CommandBuffer>(commandBuffer);
	MPD_ASSERT(cmdBuffer);

	layer->getTable()->CmdDrawIndexedIndirect(commandBuffer, buffer, offset, drawCount, stride);
	cmdBuffer->enqueueGraphicsDescriptorSetUsage();
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateSampler(VkDevice device, const VkSamplerCreateInfo *pCreateInfo,
                                                    const VkAllocationCallbacks *pCallbacks, VkSampler *pSampler)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	auto res = layer->getTable()->CreateSampler(device, pCreateInfo, pCallbacks, pSampler);
	if (res == VK_SUCCESS)
	{
		auto *sampler = layer->alloc<Sampler>(*pSampler);
		MPD_ASSERT(sampler);
		res = sampler->init(*pSampler, *pCreateInfo);
		if (res != VK_SUCCESS)
		{
			layer->destroy<Sampler>(*pSampler);
			layer->getTable()->DestroySampler(device, *pSampler, pCallbacks);
		}
	}
	return res;
}

static VKAPI_ATTR void VKAPI_CALL DestroySampler(VkDevice device, VkSampler sampler,
                                                 const VkAllocationCallbacks *pCallbacks)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	layer->destroy<Sampler>(sampler);
	layer->getTable()->DestroySampler(device, sampler, pCallbacks);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo,
                                                         const VkAllocationCallbacks *pCallbacks,
                                                         VkShaderModule *pShaderModule)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	auto res = layer->getTable()->CreateShaderModule(device, pCreateInfo, pCallbacks, pShaderModule);
	if (res == VK_SUCCESS)
	{
		auto *module = layer->alloc<ShaderModule>(*pShaderModule);
		MPD_ASSERT(module);
		res = module->init(*pShaderModule, *pCreateInfo);
		if (res != VK_SUCCESS)
		{
			layer->destroy<ShaderModule>(*pShaderModule);
			layer->getTable()->DestroyShaderModule(device, *pShaderModule, pCallbacks);
		}
	}
	return res;
}

static VKAPI_ATTR void VKAPI_CALL DestroyShaderModule(VkDevice device, VkShaderModule shaderModule,
                                                      const VkAllocationCallbacks *pCallbacks)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(device);
	auto *layer = getLayerData(key, deviceData);

	layer->destroy<ShaderModule>(shaderModule);
	layer->getTable()->DestroyShaderModule(device, shaderModule, pCallbacks);
}

static VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits,
                                                  VkFence fence)
{
	lock_guard<mutex> holder{ globalLock };
	void *key = getDispatchKey(queue);
	auto *layer = getLayerData(key, deviceData);
	auto *pQueue = layer->get<Queue>(queue);
	MPD_ASSERT(pQueue);

	for (uint32_t submit = 0; submit < submitCount; submit++)
	{
		MPD_ASSERT(pSubmits != nullptr);
		auto &submissions = pSubmits[submit];
		for (uint32_t i = 0; i < submissions.commandBufferCount; i++)
		{
			CommandBuffer *commandBuffer = layer->get<CommandBuffer>(submissions.pCommandBuffers[i]);
			MPD_ASSERT(commandBuffer != nullptr);

			commandBuffer->callDeferredFunctions(*pQueue);
		}
	}

	return layer->getTable()->QueueSubmit(queue, submitCount, pSubmits, fence);
}

static PFN_vkVoidFunction interceptCoreDeviceCommand(const char *pName)
{
	static const struct
	{
		const char *name;
		PFN_vkVoidFunction proc;
	} coreDeviceCommands[] = {
		{ "vkGetDeviceProcAddr", reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr) },
		{ "vkDestroyDevice", reinterpret_cast<PFN_vkVoidFunction>(DestroyDevice) },

		{ "vkCreateCommandPool", reinterpret_cast<PFN_vkVoidFunction>(CreateCommandPool) },
		{ "vkDestroyCommandPool", reinterpret_cast<PFN_vkVoidFunction>(DestroyCommandPool) },

		{ "vkAllocateCommandBuffers", reinterpret_cast<PFN_vkVoidFunction>(AllocateCommandBuffers) },
		{ "vkFreeCommandBuffers", reinterpret_cast<PFN_vkVoidFunction>(FreeCommandBuffers) },
		{ "vkBeginCommandBuffer", reinterpret_cast<PFN_vkVoidFunction>(BeginCommandBuffer) },

		{ "vkGetDeviceQueue", reinterpret_cast<PFN_vkVoidFunction>(GetDeviceQueue) },
		{ "vkQueueSubmit", reinterpret_cast<PFN_vkVoidFunction>(QueueSubmit) },

		{ "vkCreateBuffer", reinterpret_cast<PFN_vkVoidFunction>(CreateBuffer) },
		{ "vkDestroyBuffer", reinterpret_cast<PFN_vkVoidFunction>(DestroyBuffer) },

		{ "vkCreateImage", reinterpret_cast<PFN_vkVoidFunction>(CreateImage) },
		{ "vkDestroyImage", reinterpret_cast<PFN_vkVoidFunction>(DestroyImage) },

		{ "vkCmdExecuteCommands", reinterpret_cast<PFN_vkVoidFunction>(CmdExecuteCommands) },
		{ "vkCmdBindIndexBuffer", reinterpret_cast<PFN_vkVoidFunction>(CmdBindIndexBuffer) },
		{ "vkCmdDraw", reinterpret_cast<PFN_vkVoidFunction>(CmdDraw) },
		{ "vkCmdDrawIndirect", reinterpret_cast<PFN_vkVoidFunction>(CmdDrawIndirect) },
		{ "vkCmdDrawIndexed", reinterpret_cast<PFN_vkVoidFunction>(CmdDrawIndexed) },
		{ "vkCmdDrawIndexedIndirect", reinterpret_cast<PFN_vkVoidFunction>(CmdDrawIndexedIndirect) },

		{ "vkCmdBindPipeline", reinterpret_cast<PFN_vkVoidFunction>(CmdBindPipeline) },
		{ "vkCmdBeginRenderPass", reinterpret_cast<PFN_vkVoidFunction>(CmdBeginRenderPass) },
		{ "vkCmdNextSubpass", reinterpret_cast<PFN_vkVoidFunction>(CmdNextSubpass) },
		{ "vkCmdEndRenderPass", reinterpret_cast<PFN_vkVoidFunction>(CmdEndRenderPass) },

		{ "vkCmdPipelineBarrier", reinterpret_cast<PFN_vkVoidFunction>(CmdPipelineBarrier) },
		{ "vkCmdClearColorImage", reinterpret_cast<PFN_vkVoidFunction>(CmdClearColorImage) },
		{ "vkCmdClearDepthStencilImage", reinterpret_cast<PFN_vkVoidFunction>(CmdClearDepthStencilImage) },
		{ "vkCmdClearAttachments", reinterpret_cast<PFN_vkVoidFunction>(CmdClearAttachments) },
		{ "vkCmdCopyBuffer", reinterpret_cast<PFN_vkVoidFunction>(CmdCopyBuffer) },
		{ "vkCmdCopyImage", reinterpret_cast<PFN_vkVoidFunction>(CmdCopyImage) },
		{ "vkCmdCopyBufferToImage", reinterpret_cast<PFN_vkVoidFunction>(CmdCopyBufferToImage) },
		{ "vkCmdCopyImageToBuffer", reinterpret_cast<PFN_vkVoidFunction>(CmdCopyImageToBuffer) },
		{ "vkCmdBlitImage", reinterpret_cast<PFN_vkVoidFunction>(CmdBlitImage) },
		{ "vkCmdFillBuffer", reinterpret_cast<PFN_vkVoidFunction>(CmdFillBuffer) },
		{ "vkCmdUpdateBuffer", reinterpret_cast<PFN_vkVoidFunction>(CmdUpdateBuffer) },
		{ "vkCmdResolveImage", reinterpret_cast<PFN_vkVoidFunction>(CmdResolveImage) },
		{ "vkCmdCopyQueryPoolResults", reinterpret_cast<PFN_vkVoidFunction>(CmdCopyQueryPoolResults) },

		{ "vkCmdDispatch", reinterpret_cast<PFN_vkVoidFunction>(CmdDispatch) },
		{ "vkCmdDispatchIndirect", reinterpret_cast<PFN_vkVoidFunction>(CmdDispatchIndirect) },

		{ "vkCmdBindDescriptorSets", reinterpret_cast<PFN_vkVoidFunction>(CmdBindDescriptorSets) },
		{ "vkUpdateDescriptorSets", reinterpret_cast<PFN_vkVoidFunction>(UpdateDescriptorSets) },

		{ "vkCreateEvent", reinterpret_cast<PFN_vkVoidFunction>(CreateEvent) },
		{ "vkDestroyEvent", reinterpret_cast<PFN_vkVoidFunction>(DestroyEvent) },
		{ "vkSetEvent", reinterpret_cast<PFN_vkVoidFunction>(SetEvent) },
		{ "vkResetEvent", reinterpret_cast<PFN_vkVoidFunction>(ResetEvent) },
		{ "vkCmdSetEvent", reinterpret_cast<PFN_vkVoidFunction>(CmdSetEvent) },
		{ "vkCmdResetEvent", reinterpret_cast<PFN_vkVoidFunction>(CmdResetEvent) },
		{ "vkCmdWaitEvents", reinterpret_cast<PFN_vkVoidFunction>(CmdWaitEvents) },

		{ "vkCreateDescriptorSetLayout", reinterpret_cast<PFN_vkVoidFunction>(CreateDescriptorSetLayout) },
		{ "vkDestroyDescriptorSetLayout", reinterpret_cast<PFN_vkVoidFunction>(DestroyDescriptorSetLayout) },
		{ "vkCreatePipelineLayout", reinterpret_cast<PFN_vkVoidFunction>(CreatePipelineLayout) },
		{ "vkDestroyPipelineLayout", reinterpret_cast<PFN_vkVoidFunction>(DestroyPipelineLayout) },

		{ "vkCreateDescriptorPool", reinterpret_cast<PFN_vkVoidFunction>(CreateDescriptorPool) },
		{ "vkDestroyDescriptorPool", reinterpret_cast<PFN_vkVoidFunction>(DestroyDescriptorPool) },
		{ "vkResetDescriptorPool", reinterpret_cast<PFN_vkVoidFunction>(ResetDescriptorPool) },

		{ "vkAllocateDescriptorSets", reinterpret_cast<PFN_vkVoidFunction>(AllocateDescriptorSets) },
		{ "vkFreeDescriptorSets", reinterpret_cast<PFN_vkVoidFunction>(FreeDescriptorSets) },

		{ "vkAllocateMemory", reinterpret_cast<PFN_vkVoidFunction>(AllocateMemory) },
		{ "vkFreeMemory", reinterpret_cast<PFN_vkVoidFunction>(FreeMemory) },
		{ "vkGetBufferMemoryRequirements", reinterpret_cast<PFN_vkVoidFunction>(GetBufferMemoryRequirements) },
		{ "vkMapMemory", reinterpret_cast<PFN_vkVoidFunction>(MapMemory) },
		{ "vkUnmapMemory", reinterpret_cast<PFN_vkVoidFunction>(UnmapMemory) },
		{ "vkBindBufferMemory", reinterpret_cast<PFN_vkVoidFunction>(BindBufferMemory) },
		{ "vkBindImageMemory", reinterpret_cast<PFN_vkVoidFunction>(BindImageMemory) },

		{ "vkCreateRenderPass", reinterpret_cast<PFN_vkVoidFunction>(CreateRenderPass) },
		{ "vkDestroyRenderPass", reinterpret_cast<PFN_vkVoidFunction>(DestroyRenderPass) },

		{ "vkCreateFramebuffer", reinterpret_cast<PFN_vkVoidFunction>(CreateFramebuffer) },
		{ "vkDestroyFramebuffer", reinterpret_cast<PFN_vkVoidFunction>(DestroyFramebuffer) },

		{ "vkCreateImageView", reinterpret_cast<PFN_vkVoidFunction>(CreateImageView) },
		{ "vkDestroyImageView", reinterpret_cast<PFN_vkVoidFunction>(DestroyImageView) },

		{ "vkCreateGraphicsPipelines", reinterpret_cast<PFN_vkVoidFunction>(CreateGraphicsPipelines) },
		{ "vkCreateComputePipelines", reinterpret_cast<PFN_vkVoidFunction>(CreateComputePipelines) },
		{ "vkDestroyPipeline", reinterpret_cast<PFN_vkVoidFunction>(DestroyPipeline) },

		{ "vkCreateSampler", reinterpret_cast<PFN_vkVoidFunction>(CreateSampler) },
		{ "vkDestroySampler", reinterpret_cast<PFN_vkVoidFunction>(DestroySampler) },

		{ "vkCreateShaderModule", reinterpret_cast<PFN_vkVoidFunction>(CreateShaderModule) },
		{ "vkDestroyShaderModule", reinterpret_cast<PFN_vkVoidFunction>(DestroyShaderModule) },

		{ "vkCreateSwapchainKHR", reinterpret_cast<PFN_vkVoidFunction>(CreateSwapchainKHR) },
		{ "vkDestroySwapchainKHR", reinterpret_cast<PFN_vkVoidFunction>(DestroySwapchainKHR) },
		{ "vkGetSwapchainImagesKHR", reinterpret_cast<PFN_vkVoidFunction>(GetSwapchainImagesKHR) },
	};

	for (auto &cmd : coreDeviceCommands)
		if (strcmp(cmd.name, pName) == 0)
			return cmd.proc;
	return nullptr;
}
}

using namespace MPD;
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char *pName)
{
	lock_guard<mutex> holder{ globalLock };

	auto proc = interceptCoreDeviceCommand(pName);
	if (proc)
		return proc;

	auto *layer = getLayerData(getDispatchKey(device), deviceData);
	MPD_ASSERT(layer);

	return layer->getTable()->GetDeviceProcAddr(device, pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *pName)
{
	lock_guard<mutex> holder{ globalLock };

	auto proc = interceptCoreInstanceCommand(pName);
	if (proc)
		return proc;

	proc = interceptExtensionInstanceCommand(pName);
	if (proc)
		return proc;

	proc = interceptCoreDeviceCommand(pName);
	if (proc)
		return proc;

	auto *layer = getLayerData(getDispatchKey(instance), instanceData);
	MPD_ASSERT(layer);

	return layer->getProcAddr(pName);
}

static const VkLayerProperties layerProps[] = {
	{ VK_LAYER_ARM_mali_perf_doc, VK_MAKE_VERSION(1, 0, 32), 1, "ARM Mali PerfDoc" },
};

static const VkExtensionProperties layerExtensions[] = {
	{ VK_EXT_DEBUG_REPORT_EXTENSION_NAME, VK_EXT_DEBUG_REPORT_SPEC_VERSION },
};
static const uint32_t numExtensions = sizeof(layerExtensions) / sizeof(layerExtensions[0]);

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pPropertyCount,
                                                                      VkExtensionProperties *pProperties)
{
	if (!pLayerName || strcmp(pLayerName, layerProps[0].layerName))
		return VK_ERROR_LAYER_NOT_PRESENT;

	uint32_t writtenCount = 0;
	if (pProperties)
	{
		uint32_t toWrite = min(numExtensions, *pPropertyCount);
		memcpy(pProperties, layerExtensions, toWrite * sizeof(layerExtensions[0]));
		writtenCount = toWrite;
	}

	if (pProperties && writtenCount < numExtensions)
	{
		*pPropertyCount = writtenCount;
		return VK_INCOMPLETE;
	}
	*pPropertyCount = numExtensions;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice gpu, const char *pLayerName,
                                                                    uint32_t *pPropertyCount,
                                                                    VkExtensionProperties *pProperties)
{
	MPD_ASSERT(gpu == VK_NULL_HANDLE);
	MPD_UNUSED(gpu);

	if (pLayerName && !strcmp(pLayerName, layerProps[0].layerName))
	{
		if (pProperties && *pPropertyCount > 0)
			return VK_INCOMPLETE;
		*pPropertyCount = 0;
		return VK_SUCCESS;
	}
	else
		return VK_ERROR_LAYER_NOT_PRESENT;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t *pPropertyCount,
                                                                  VkLayerProperties *pProperties)
{
	if (pProperties)
	{
		uint32_t count = std::min(1u, *pPropertyCount);
		memcpy(pProperties, layerProps, count * sizeof(VkLayerProperties));
		VkResult res = count < *pPropertyCount ? VK_INCOMPLETE : VK_SUCCESS;
		*pPropertyCount = count;
		return res;
	}
	else
	{
		*pPropertyCount = sizeof(layerProps) / sizeof(VkLayerProperties);
		return VK_SUCCESS;
	}
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(VkPhysicalDevice, uint32_t *pPropertyCount,
                                                                VkLayerProperties *pProperties)
{
	if (pProperties)
	{
		uint32_t count = std::min(1u, *pPropertyCount);
		memcpy(pProperties, layerProps, count * sizeof(VkLayerProperties));
		VkResult res = count < *pPropertyCount ? VK_INCOMPLETE : VK_SUCCESS;
		*pPropertyCount = count;
		return res;
	}
	else
	{
		*pPropertyCount = sizeof(layerProps) / sizeof(VkLayerProperties);
		return VK_SUCCESS;
	}
}
