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

#include "commandbuffer.hpp"
#include "buffer.hpp"
#include "device.hpp"
#include "device_memory.hpp"
#include "message_codes.hpp"
#include "queue.hpp"
#include "render_pass.hpp"

#include "descriptor_set.hpp"
#include "format.hpp"
#include "framebuffer.hpp"
#include "pipeline_layout.hpp"
#include <algorithm>
#include <vector>

#ifdef _MSC_VER
#include <intrin.h>
#endif

using namespace std;

namespace MPD
{
CommandBuffer::CommandBuffer(Device *device, uint64_t objHandle_)
    : BaseObject(device, objHandle_, VULKAN_OBJECT_TYPE)
{
	// register heuristics
	heuristics.emplace_back(new DepthPrePassHeuristic(this, device));
	heuristics.emplace_back(new TileReadbackHeuristic(this, device));
	heuristics.emplace_back(new ClearAttachmentsHeuristic(this, device));
}

VkResult CommandBuffer::init(VkCommandBuffer commandBuffer_, CommandPool *commandPool_)
{
	commandBuffer = commandBuffer_;
	commandPool = commandPool_;
	reset();
	return VK_SUCCESS;
}

void CommandBuffer::reset()
{
	indexBuffer = nullptr;
	indexOffset = 0;
	executedCommandBuffers.clear();
	deferredFunctions.clear();
	smallIndexedDrawcallCount = 0;
	currentRenderPass = nullptr;
	currentSubpassIndex = 0;

	for (auto &it : heuristics)
		it->reset();

	graphicsDescriptorSets.clear();
	computeDescriptorSets.clear();
	uint32_t maxSets = baseDevice->getProperties().limits.maxBoundDescriptorSets;
	graphicsDescriptorSets.resize(maxSets);
	computeDescriptorSets.resize(maxSets);
	graphicsLayout = nullptr;
	computeLayout = nullptr;
}

void CommandBuffer::enqueueComputeDescriptorSetUsage()
{
	MPD_ASSERT(computeLayout);
	size_t numSets = computeLayout->getDescriptorSetLayouts().size();

	for (size_t i = 0; i < numSets; i++)
	{
		if (!computeDescriptorSets[i].dirty)
			continue;

		auto *set = computeDescriptorSets[i].set;
		if (set)
		{
			enqueueDeferredFunction([set](Queue &) { set->signalUsage(); });
		}
		computeDescriptorSets[i].dirty = false;
	}
}

void CommandBuffer::enqueueGraphicsDescriptorSetUsage()
{
	MPD_ASSERT(graphicsLayout);
	size_t numSets = graphicsLayout->getDescriptorSetLayouts().size();

	for (size_t i = 0; i < numSets; i++)
	{
		if (!graphicsDescriptorSets[i].dirty)
			continue;

		auto *set = graphicsDescriptorSets[i].set;
		if (set)
		{
			enqueueDeferredFunction([set](Queue &) { set->signalUsage(); });
		}
		graphicsDescriptorSets[i].dirty = false;
	}
}

void CommandBuffer::bindDescriptorSets(VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout, uint32_t firstSet,
                                       uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets, uint32_t,
                                       const uint32_t *)
{
	auto &sets = pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS ? graphicsDescriptorSets : computeDescriptorSets;

	for (uint32_t i = 0; i < descriptorSetCount; i++)
	{
		MPD_ASSERT(i + firstSet < sets.size());
		sets[i + firstSet].set = baseDevice->get<DescriptorSet>(pDescriptorSets[i]);
		sets[i + firstSet].dirty = true;
	}
}

void CommandBuffer::bindIndexBuffer(Buffer *buffer, VkDeviceSize offset, VkIndexType indexType)
{
	this->indexBuffer = buffer;
	this->indexOffset = offset;
	this->indexType = indexType;
}

void CommandBuffer::enqueueDeferredFunction(std::function<void(Queue &)> Function)
{
	deferredFunctions.push_back(std::move(Function));
}

void CommandBuffer::callDeferredFunctions(Queue &queue)
{
	for (auto func : deferredFunctions)
	{
		func(queue);
	}
	deferredFunctions.clear();
}

void CommandBuffer::executeCommandBuffer(CommandBuffer *commandBuffer)
{
	enqueueDeferredFunction([commandBuffer](Queue &queue) { commandBuffer->callDeferredFunctions(queue); });
}

void CommandBuffer::bindPipeline(VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline)
{
	for (auto &it : heuristics)
		it->cmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
	this->pipeline = baseDevice->get<Pipeline>(pipeline);

	if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS)
		graphicsLayout = this->pipeline->getPipelineLayout();
	else
		computeLayout = this->pipeline->getPipelineLayout();
}

void CommandBuffer::clearAttachments(uint32_t attachmentCount, const VkClearAttachment *pAttachments,
                                     uint32_t rectCount, const VkClearRect *pRect)
{
	for (auto &it : heuristics)
		it->cmdClearAttachments(commandBuffer, attachmentCount, pAttachments, rectCount, pRect);
}

void CommandBuffer::nextSubpass(VkSubpassContents contents)
{
	MPD_ASSERT(currentRenderPass);
	currentSubpassIndex++;
	MPD_ASSERT(currentSubpassIndex < currentRenderPass->getCreateInfo().subpassCount);
	for (auto &it : heuristics)
		it->cmdSetSubpass(commandBuffer, currentSubpassIndex, contents);
}

void CommandBuffer::setCurrentRenderPass(RenderPass *renderPass)
{
	currentRenderPass = renderPass;
	for (auto &it : heuristics)
		it->cmdSetRenderPass(commandBuffer, renderPass);
}

void CommandBuffer::setCurrentSubpassIndex(uint32_t index)
{
	currentSubpassIndex = index;
	for (auto &it : heuristics)
		it->cmdSetSubpass(commandBuffer, currentSubpassIndex, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
}

void CommandBuffer::enqueueRenderPassLoadOps(VkRenderPass renderPass, VkFramebuffer framebuffer)
{
	auto *rp = baseDevice->get<RenderPass>(renderPass);
	auto *fb = baseDevice->get<Framebuffer>(framebuffer);
	MPD_ASSERT(rp);
	MPD_ASSERT(fb);

	auto &fbInfo = fb->getCreateInfo();
	auto &rpInfo = rp->getCreateInfo();

	// Check if any attachments have LOAD or CLEAR operation on them.
	// Don't care is treated as undefined.

	for (uint32_t att = 0; att < rpInfo.attachmentCount; att++)
	{
		auto &attachment = rpInfo.pAttachments[att];

		// If the attachment is unused, don't register anything.
		if (!rp->renderPassUsesAttachmentAsImageOnly(att) && !rp->renderPassUsesAttachmentOnTile(att))
			continue;

		Image::Usage usage = Image::Usage::Undefined;

		if (!formatIsStencilOnly(attachment.format) && attachment.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
		{
			usage = Image::Usage::RenderPassReadToTile;
		}

		if ((formatIsDepthStencil(attachment.format) || formatIsStencilOnly(attachment.format)) &&
		    attachment.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
		{
			usage = Image::Usage::RenderPassReadToTile;
		}

		if (!formatIsStencilOnly(attachment.format) && attachment.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
		{
			usage = Image::Usage::RenderPassCleared;
		}

		if ((formatIsDepthStencil(attachment.format) || formatIsStencilOnly(attachment.format)) &&
		    attachment.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
		{
			usage = Image::Usage::RenderPassCleared;
		}

		if (rp->renderPassUsesAttachmentAsImageOnly(att))
		{
			// If the attachment is only used as an input attachment, it is basically a fancy way of reading as a texture.
			// LOAD_OP_LOAD doesn't actually read-back to tile.
			usage = Image::Usage::ResourceRead;
		}

		ImageView *view = baseDevice->get<ImageView>(fbInfo.pAttachments[att]);
		enqueueDeferredFunction([view, usage](Queue &) { view->signalUsage(usage); });
	}
}
void CommandBuffer::enqueueRenderPassStoreOps(VkRenderPass renderPass, VkFramebuffer framebuffer)
{
	auto *rp = baseDevice->get<RenderPass>(renderPass);
	auto *fb = baseDevice->get<Framebuffer>(framebuffer);
	MPD_ASSERT(rp);
	MPD_ASSERT(fb);

	auto &fbInfo = fb->getCreateInfo();
	auto &rpInfo = rp->getCreateInfo();

	for (uint32_t att = 0; att < rpInfo.attachmentCount; att++)
	{
		auto &attachment = rpInfo.pAttachments[att];

		// If the attachment is unused on tile, don't register anything.
		if (!rp->renderPassUsesAttachmentOnTile(att))
			continue;

		Image::Usage usage = Image::Usage::RenderPassDiscarded;

		if (!formatIsStencilOnly(attachment.format) && attachment.storeOp == VK_ATTACHMENT_STORE_OP_STORE)
		{
			usage = Image::Usage::RenderPassStored;
		}

		if ((formatIsDepthStencil(attachment.format) || formatIsStencilOnly(attachment.format)) &&
		    attachment.stencilStoreOp == VK_ATTACHMENT_STORE_OP_STORE)
		{
			usage = Image::Usage::RenderPassStored;
		}

		ImageView *view = baseDevice->get<ImageView>(fbInfo.pAttachments[att]);
		MPD_ASSERT(view);
		enqueueDeferredFunction([view, usage](Queue &) { view->signalUsage(usage); });
	}
}

void CommandBuffer::beginRenderPass(const VkRenderPassBeginInfo *pRenderPassBegin, VkSubpassContents contents)
{
	for (auto &it : heuristics)
	{
		it->cmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
		it->cmdSetSubpass(commandBuffer, 0, contents);
	}

	enqueueRenderPassLoadOps(pRenderPassBegin->renderPass, pRenderPassBegin->framebuffer);
	// Don't need to wait for CmdEndRenderPass.
	enqueueRenderPassStoreOps(pRenderPassBegin->renderPass, pRenderPassBegin->framebuffer);

	currentRenderPass = baseDevice->get<RenderPass>(pRenderPassBegin->renderPass);
	currentSubpassIndex = 0;
	auto &createInfo = currentRenderPass->getCreateInfo();

	QueueTracker::StageFlags src = 0;
	QueueTracker::StageFlags dst = 0;

	// Handle implicit barriers before the render pass.
	for (uint32_t i = 0; i < createInfo.dependencyCount; i++)
	{
		auto &dependency = createInfo.pDependencies[i];
		if (dependency.srcSubpass == VK_SUBPASS_EXTERNAL)
		{
			auto srcMask = dependency.srcStageMask;
			if (srcMask & VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT)
				srcMask |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			src |= vkStagesToTracker(srcMask);

			auto dstMask = dependency.dstStageMask;
			if (dstMask & VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)
				dstMask |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			dst |= vkStagesToTracker(dstMask);
		}
	}

	enqueueDeferredFunction([=](Queue &queue) {
		auto &tracker = queue.getQueueTracker();
		tracker.pipelineBarrier(src, dst);
		tracker.pushWork(QueueTracker::STAGE_GEOMETRY);
		tracker.pipelineBarrier(QueueTracker::STAGE_GEOMETRY_BIT, QueueTracker::STAGE_FRAGMENT_BIT);
		tracker.pushWork(QueueTracker::STAGE_FRAGMENT);
	});
}

void CommandBuffer::endRenderPass()
{
	for (auto &it : heuristics)
		it->cmdEndRenderPass(commandBuffer);
	auto &createInfo = currentRenderPass->getCreateInfo();

	QueueTracker::StageFlags src = 0;
	QueueTracker::StageFlags dst = 0;

	// Handle implicit barriers after the render pass.
	for (uint32_t i = 0; i < createInfo.dependencyCount; i++)
	{
		auto &dependency = createInfo.pDependencies[i];
		if (dependency.dstSubpass == VK_SUBPASS_EXTERNAL)
		{
			auto srcMask = dependency.srcStageMask;
			if (srcMask & VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT)
				srcMask |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			src |= vkStagesToTracker(srcMask);

			auto dstMask = dependency.dstStageMask;
			if (dstMask & VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)
				dstMask |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			dst |= vkStagesToTracker(dstMask);
		}
	}

	enqueueDeferredFunction([=](Queue &queue) {
		auto &tracker = queue.getQueueTracker();
		tracker.pipelineBarrier(src, dst);
	});

	currentRenderPass = nullptr;
	currentSubpassIndex = 0;
}

void CommandBuffer::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
	for (auto &it : heuristics)
		it->cmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

bool CommandBuffer::testCache(uint32_t value, uint32_t iteration, CacheEntry *cacheEntries, uint32_t cacheSize)
{
	uint32_t lru = 0;
	for (uint32_t i = 0; i < std::min(iteration, cacheSize); i++)
	{
		if (cacheEntries[i].value == value)
		{
			cacheEntries[i].age = iteration;
			return true;
		}
		else if (cacheEntries[i].age < cacheEntries[lru].age)
			lru = i;
	}

	if (iteration < cacheSize)
	{
		cacheEntries[iteration].value = value;
		cacheEntries[iteration].age = iteration;
	}
	else
	{
		MPD_ASSERT(lru < cacheSize);
		cacheEntries[lru].value = value;
		cacheEntries[lru].age = iteration;
	}
	return false;
}

void CommandBuffer::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset,
                                uint32_t firstInstance)
{
	MPD_ASSERT(indexBuffer != nullptr);

	for (auto &it : heuristics)
		it->cmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);

	// Check small drawcalls
	const auto &cfg = baseDevice->getConfig();
	if ((indexCount * instanceCount) <= cfg.smallIndexedDrawcallIndices)
	{
		if (++smallIndexedDrawcallCount == cfg.maxSmallIndexedDrawcalls)
		{
			log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_MANY_SMALL_INDEXED_DRAWCALLS,
			    "The command buffer contains many small indexed drawcalls "
			    "(at least %u drawcalls with less than %u indices each). This may cause pipeline bubbles. "
			    "You can try batching drawcalls or instancing when applicable.",
			    cfg.maxSmallIndexedDrawcalls, cfg.smallIndexedDrawcallIndices);
		}
	}

	if (indexCount < cfg.indexBufferScanMinIndexCount)
		return;

	if (cfg.indexBufferScanningEnable)
	{
		bool primitiveRestart = pipeline->getGraphicsCreateInfo().pInputAssemblyState->primitiveRestartEnable;
		if (cfg.indexBufferScanningInPlace)
			scanIndices(indexBuffer, indexOffset, indexType, indexCount, firstIndex, primitiveRestart);
		else
		{
			// Capture a copy of these members.
			auto indexBufferCopy = indexBuffer;
			auto indexOffsetCopy = indexOffset;
			auto indexTypeCopy = indexType;
			enqueueDeferredFunction([=](Queue &) {
				this->scanIndices(indexBufferCopy, indexOffsetCopy, indexTypeCopy, indexCount, firstIndex,
				                  primitiveRestart);
			});
		}
	}
}

void CommandBuffer::scanIndices(Buffer *buffer, VkDeviceSize indexOffset, VkIndexType indexType, uint32_t indexCount,
                                uint32_t firstIndex, bool primitiveRestart)
{
	MPD_ASSERT(buffer != nullptr);

	const DeviceMemory *deviceMemory = buffer->getDeviceMemory();
	MPD_ASSERT(deviceMemory);

	const void *indexData = deviceMemory->getMappedMemory();
	if (indexData)
	{
		uint32_t scanStride = (indexType == VK_INDEX_TYPE_UINT16) ? sizeof(uint16_t) : sizeof(uint32_t);

		const uint8_t *scanBegin =
		    static_cast<const uint8_t *>(indexData) + buffer->getMemoryOffset() + indexOffset + scanStride * firstIndex;
		const uint8_t *scanEnd = scanBegin + indexCount * scanStride;

		uint32_t minValue = ~0u;
		uint32_t maxValue = 0u;

		cacheEntries.resize(baseDevice->getConfig().indexBufferVertexPostTransformCache);

		uint32_t iteration = 0;
		uint32_t vertexShadeCount = 0;

		uint32_t primitiveRestartValue;
		if (indexType == VK_INDEX_TYPE_UINT16)
			primitiveRestartValue = 0xFFFF;
		else
			primitiveRestartValue = 0xFFFFFFFF;

		// get min/max range
		const uint8_t *scanPtr = scanBegin;
		while (scanPtr != scanEnd)
		{
			uint32_t scanValue;

			if (indexType == VK_INDEX_TYPE_UINT16)
				scanValue = *(uint16_t *)scanPtr;
			else
				scanValue = *(uint32_t *)scanPtr;

			if (!primitiveRestart || scanValue != primitiveRestartValue)
			{
				minValue = std::min(minValue, scanValue);
				maxValue = std::max(maxValue, scanValue);
				if (!testCache(scanValue, iteration++, cacheEntries.data(), cacheEntries.size()))
					vertexShadeCount++;
			}
			scanPtr += scanStride;
		}

		if (maxValue < minValue)
		{
			// all indices are primitive restarts so early exit
			return;
		}

		// We already know that this is going to be sparse.
		// To potentially avoid an explosion in memory, just exit early.
		if (maxValue - minValue >= indexCount)
		{
			buffer->log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_INDEX_BUFFER_SPARSE,
			            "Indexbuffer data used by drawcall is fragmented. Number of indices (%u) is smaller than range "
			            "of index buffer data (%u).\n",
			            indexCount, maxValue - minValue + 1);
			return;
		}

		std::vector<uint64_t> buckets;
		buckets.resize(((maxValue - minValue + 1) + 63) / 64);
		memset(buckets.data(), 0, sizeof(uint64_t) * buckets.size());

#define FRAGMENT_SIZE 16
		char fragmentation[FRAGMENT_SIZE + 1];
		memset(fragmentation, ' ', sizeof(fragmentation));
		fragmentation[FRAGMENT_SIZE] = '\0';

		scanPtr = scanBegin;
		while (scanPtr != scanEnd)
		{
			uint32_t scanValue;

			if (indexType == VK_INDEX_TYPE_UINT16)
				scanValue = *reinterpret_cast<const uint16_t *>(scanPtr);
			else
				scanValue = *reinterpret_cast<const uint32_t *>(scanPtr);

			if (!primitiveRestart || scanValue != primitiveRestartValue)
			{
				uint32_t index = (scanValue - minValue) / 64;
				uint64_t bit = 1ull << (uint64_t)((scanValue - minValue) & 63);

				uint32_t frag_index = ((scanValue - minValue) * FRAGMENT_SIZE) / (maxValue - minValue + 1);
				fragmentation[frag_index] = '#';

				buckets[index] |= bit;
			}
			scanPtr += scanStride;
		}

		uint32_t verticesReferenced = 0;
		for (auto it : buckets)
		{
#ifdef _MSC_VER
			verticesReferenced += (uint32_t)__popcnt(it & 0xffffffffu);
			verticesReferenced += (uint32_t)__popcnt(it >> 32);
#else
			verticesReferenced += (uint32_t)__builtin_popcountll(it);
#endif
		}

		float utilization = float(verticesReferenced) / float(maxValue - minValue + 1);
		if (utilization < baseDevice->getConfig().indexBufferUtilizationThreshold)
		{
			buffer->log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_INDEX_BUFFER_SPARSE,
			            "Indexbuffer data used by drawcall is fragmented: [%s]", fragmentation);
		}

		float cacheHitRate = float(verticesReferenced) / float(vertexShadeCount);
		if (cacheHitRate <= baseDevice->getConfig().indexBufferCacheHitThreshold)
		{
			buffer->log(
			    VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_INDEX_BUFFER_CACHE_THRASHING,
			    "Indexbuffer data causes thrashing of post-transform vertex cache.\n"
			    "Percentage of unique vertices to number of vertices theoretically shaded is estimated to %.02f%%.",
			    cacheHitRate * 100.0f);
		}
	}
}

QueueTracker::StageFlags CommandBuffer::vkStagesToTracker(VkPipelineStageFlags stages)
{
	QueueTracker::StageFlags flags = 0;
	if (stages & (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
	              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT))
		flags |= QueueTracker::STAGE_FRAGMENT_BIT;

	if (stages &
	    (VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
	     VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT |
	     VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT))
		flags |= QueueTracker::STAGE_GEOMETRY_BIT;

	if (stages & VK_PIPELINE_STAGE_TRANSFER_BIT)
		flags |= QueueTracker::STAGE_TRANSFER_BIT;

	if (stages & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
		flags |= QueueTracker::STAGE_COMPUTE_BIT;

	if (stages & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)
		flags |= QueueTracker::STAGE_ALL_BITS;

	if (stages & VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT)
		flags |= QueueTracker::STAGE_GEOMETRY_BIT | QueueTracker::STAGE_FRAGMENT_BIT;

	return flags;
}

void CommandBuffer::pipelineBarrier(VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
                                    VkDependencyFlags, uint32_t, const VkMemoryBarrier *, uint32_t,
                                    const VkBufferMemoryBarrier *, uint32_t, const VkImageMemoryBarrier *)
{
	if (currentRenderPass)
		return;

	enqueueDeferredFunction([=](Queue &queue) {
		auto &tracker = queue.getQueueTracker();

		auto src = srcStageMask;
		auto dst = dstStageMask;

		if (dst & VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)
			dst |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		if (src & VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT)
			src |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

		tracker.pipelineBarrier(vkStagesToTracker(src), vkStagesToTracker(dst));
	});
}
}
