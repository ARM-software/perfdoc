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

namespace MPD
{

class Device;
class CommandBuffer;
class RenderPass;

class Heuristic
{
public:
	Heuristic(Device *device)
	    : device(device)
	{
	}

	virtual ~Heuristic()
	{
	}

	virtual void cmdBeginRenderPass(VkCommandBuffer, const VkRenderPassBeginInfo *, VkSubpassContents)
	{
	}

	virtual void cmdSetRenderPass(VkCommandBuffer, RenderPass *)
	{
	}

	virtual void cmdClearAttachments(VkCommandBuffer, uint32_t, const VkClearAttachment *, uint32_t,
	                                 const VkClearRect *)
	{
	}

	virtual void cmdSetSubpass(VkCommandBuffer, uint32_t index, VkSubpassContents)
	{
	}

	virtual void cmdEndRenderPass(VkCommandBuffer)
	{
	}

	virtual void cmdBindPipeline(VkCommandBuffer, VkPipelineBindPoint, VkPipeline)
	{
	}

	virtual void cmdDraw(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t)
	{
	}

	virtual void cmdDrawIndexed(VkCommandBuffer, uint32_t, uint32_t, uint32_t, int32_t, uint32_t)
	{
	}

	virtual void submit()
	{
	}

	virtual void reset()
	{
	}

protected:
	Device *device;
};

class DepthPrePassHeuristic : public Heuristic
{
public:
	DepthPrePassHeuristic(CommandBuffer *commandBuffer, Device *device);

	void cmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin,
	                        VkSubpassContents contents) override;

	void cmdEndRenderPass(VkCommandBuffer commandBuffer) override;

	void cmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
	                     VkPipeline pipeline) override;

	void cmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
	             uint32_t firstInstance) override;

	void cmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
	                    int32_t vertexOffset, uint32_t firstInstance) override;

private:
	void reset() override;

	static const uint32_t DEPTH_ATTACHMENT = 0x1;
	static const uint32_t COLOR_ATTACHMENT = 0x2;
	static const uint32_t DEPTH_ONLY = 0x4;
	static const uint32_t DEPTH_EQUAL_TEST = 0x8;
	static const uint32_t INSIDE_RENDERPASS = 0x10;

	CommandBuffer *commandBuffer;

	uint32_t state;
	uint32_t numDrawCallsDepthOnly;
	uint32_t numDrawCallsDepthEqual;
};

class TileReadbackHeuristic : public Heuristic
{
public:
	TileReadbackHeuristic(CommandBuffer *commandBuffer, Device *device);

	void cmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin,
	                        VkSubpassContents contents) override;

private:
	CommandBuffer *commandBuffer;
};

class ClearAttachmentsHeuristic : public Heuristic
{
public:
	ClearAttachmentsHeuristic(CommandBuffer *commandBuffer, Device *device);
	void cmdBeginRenderPass(VkCommandBuffer, const VkRenderPassBeginInfo *, VkSubpassContents) override;
	void cmdClearAttachments(VkCommandBuffer, uint32_t, const VkClearAttachment *, uint32_t,
	                         const VkClearRect *) override;
	void cmdSetSubpass(VkCommandBuffer, uint32_t index, VkSubpassContents) override;
	void cmdSetRenderPass(VkCommandBuffer, RenderPass *renderPass) override;

	void cmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
	             uint32_t firstInstance) override;

	void cmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
	                    int32_t vertexOffset, uint32_t firstInstance) override;

	void reset() override;

private:
	CommandBuffer *commandBuffer;
	const VkRenderPassCreateInfo *renderPassInfo = nullptr;
	uint32_t currentSubpass = 0;
	bool hasSeenDrawCall = false;
};
}
