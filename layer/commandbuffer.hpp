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
#include "dispatch_helper.hpp"
#include "heuristic.hpp"
#include "perfdoc.hpp"
#include "pipeline.hpp"
#include "queue_tracker.hpp"

#include <functional>
#include <vector>

namespace MPD
{

class CommandPool;
class Buffer;
class Queue;
class RenderPass;
class DescriptorSet;
class PipelineLayout;

class CommandBuffer : public BaseObject
{
public:
	using VulkanType = VkCommandBuffer;
	static const VkDebugReportObjectTypeEXT VULKAN_OBJECT_TYPE = VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT;

	CommandBuffer(Device *device, uint64_t objHandle_);

	VkResult init(VkCommandBuffer commandBuffer_, CommandPool *commandPool_);

	VkCommandBuffer getCommandBuffer() const
	{
		return commandBuffer;
	}

	CommandPool *getCommandPool() const
	{
		return commandPool;
	}

	void enqueueDeferredFunction(std::function<void(Queue &)> Function);
	void callDeferredFunctions(Queue &queue);

	void bindIndexBuffer(Buffer *buffer, VkDeviceSize offset, VkIndexType indexType);
	void executeCommandBuffer(CommandBuffer *commandBuffer);

	void bindPipeline(VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline);
	void beginRenderPass(const VkRenderPassBeginInfo *pRenderPassBegin, VkSubpassContents contents);
	void nextSubpass(VkSubpassContents contents);
	void endRenderPass();

	void bindDescriptorSets(VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet,
	                        uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets,
	                        uint32_t dynamicOffsetCount, const uint32_t *pDynamicOffsets);

	void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
	void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset,
	                 uint32_t firstInstance);

	void pipelineBarrier(VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
	                     VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount,
	                     const VkMemoryBarrier *pMemoryBarriers, uint32_t bufferMemoryBarrierCount,
	                     const VkBufferMemoryBarrier *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount,
	                     const VkImageMemoryBarrier *pImageMemoryBarriers);

	void clearAttachments(uint32_t attachmentCount, const VkClearAttachment *pAttachments, uint32_t rectCount,
	                      const VkClearRect *pRect);

	void reset();

	void setIsSecondaryCommandBuffer(bool secondary)
	{
		this->secondary = secondary;
	}

	void setCurrentRenderPass(RenderPass *renderPass);

	void setCurrentSubpassIndex(uint32_t index);

	static QueueTracker::StageFlags vkStagesToTracker(VkPipelineStageFlags stages);

	void enqueueGraphicsDescriptorSetUsage();
	void enqueueComputeDescriptorSetUsage();

private:
	void scanIndices(Buffer *buffer, VkDeviceSize indexOffset, VkIndexType indexType, uint32_t indexCount,
	                 uint32_t firstIndex, bool primitiveRestart);

	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	CommandPool *commandPool;

	std::vector<CommandBuffer *> executedCommandBuffers;
	std::vector<std::function<void(Queue &)>> deferredFunctions;

	Buffer *indexBuffer;
	VkDeviceSize indexOffset;
	VkIndexType indexType;
	Pipeline *pipeline;

	uint32_t smallIndexedDrawcallCount = 0;

	std::vector<std::unique_ptr<Heuristic>> heuristics;
	const RenderPass *currentRenderPass;
	uint32_t currentSubpassIndex = 0;
	bool secondary = false;

	struct CacheEntry
	{
		uint32_t value;
		uint32_t age;
	};

	std::vector<CacheEntry> cacheEntries;
	static bool testCache(uint32_t value, uint32_t iteration, CacheEntry *cacheEntries, uint32_t cacheSize);

	void enqueueRenderPassLoadOps(VkRenderPass renderPass, VkFramebuffer framebuffer);
	void enqueueRenderPassStoreOps(VkRenderPass renderPass, VkFramebuffer framebuffer);

	struct DescriptorSetInfo
	{
		DescriptorSet *set = nullptr;
		bool dirty = true;
	};
	std::vector<DescriptorSetInfo> graphicsDescriptorSets;
	std::vector<DescriptorSetInfo> computeDescriptorSets;
	const PipelineLayout *graphicsLayout = nullptr;
	const PipelineLayout *computeLayout = nullptr;
};
}
