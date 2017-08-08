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

#include "heuristic.hpp"
#include "commandbuffer.hpp"
#include "device.hpp"
#include "format.hpp"
#include "framebuffer.hpp"
#include "image.hpp"
#include "message_codes.hpp"
#include "pipeline.hpp"
#include "render_pass.hpp"

namespace MPD
{

DepthPrePassHeuristic::DepthPrePassHeuristic(CommandBuffer *commandBuffer, Device *device)
    : Heuristic(device)
{
	this->commandBuffer = commandBuffer;
	reset();
}

void DepthPrePassHeuristic::reset()
{
	state = 0;
	numDrawCallsDepthOnly = 0;
	numDrawCallsDepthEqual = 0;
}

void DepthPrePassHeuristic::cmdBeginRenderPass(VkCommandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin,
                                               VkSubpassContents)
{
	MPD_ASSERT((state & INSIDE_RENDERPASS) == 0u);
	reset();

	RenderPass *renderPass = device->get<RenderPass>(pRenderPassBegin->renderPass);
	MPD_ASSERT(renderPass != nullptr);

	const VkRenderPassCreateInfo &createInfo = renderPass->getCreateInfo();
	for (uint32_t i = 0; i < createInfo.subpassCount; i++)
	{
		if (createInfo.pSubpasses[i].pDepthStencilAttachment != nullptr)
			state |= DEPTH_ATTACHMENT;
		if (createInfo.pSubpasses[i].colorAttachmentCount > 0)
			state |= COLOR_ATTACHMENT;
	}

	state |= INSIDE_RENDERPASS;
}

void DepthPrePassHeuristic::cmdEndRenderPass(VkCommandBuffer)
{
	MPD_ASSERT((state & INSIDE_RENDERPASS) != 0u);
	state &= ~INSIDE_RENDERPASS;

	// check current heuristics and report findings (if any)
	if ((state & (COLOR_ATTACHMENT | DEPTH_ATTACHMENT)) == (COLOR_ATTACHMENT | DEPTH_ATTACHMENT))
	{
		if ((numDrawCallsDepthOnly >= device->getConfig().depthPrePassNumDrawCalls) &&
		    (numDrawCallsDepthEqual >= device->getConfig().depthPrePassNumDrawCalls))
		{
			this->commandBuffer->log(
			    VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_DEPTH_PRE_PASS,
			    "Detected possible rendering pattern using depth pre-pass. "
			    "This is not recommended on Mali due to extra geometry pressure and CPU overhead. ");
		}
	}
}

void DepthPrePassHeuristic::cmdBindPipeline(VkCommandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline)
{
	if (pipelineBindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS)
	{
		reset();
		return;
	}

	Pipeline *graphicsPipeline = device->get<Pipeline>(pipeline);
	MPD_ASSERT(graphicsPipeline);

	const VkGraphicsPipelineCreateInfo &pipelineCreateInfo = graphicsPipeline->getGraphicsCreateInfo();

	const VkPipelineDepthStencilStateCreateInfo *depthStencilState = pipelineCreateInfo.pDepthStencilState;
	const VkPipelineColorBlendStateCreateInfo *blendState = pipelineCreateInfo.pColorBlendState;

	// check if color writes are enabled
	state |= DEPTH_ONLY;

	if (blendState)
	{
		for (uint32_t i = 0; i < blendState->attachmentCount; i++)
		{
			if (blendState->pAttachments[i].colorWriteMask != 0)
			{
				state &= ~DEPTH_ONLY;
				break;
			}
		}
	}

	// check if depth equal test is enabled
	state &= ~DEPTH_EQUAL_TEST;
	if (depthStencilState->depthTestEnable)
	{
		switch (depthStencilState->depthCompareOp)
		{
		case VK_COMPARE_OP_EQUAL:
		case VK_COMPARE_OP_LESS_OR_EQUAL:
		case VK_COMPARE_OP_GREATER_OR_EQUAL:
			state |= DEPTH_EQUAL_TEST;
			break;

		default:
			break;
		}
	}
}

void DepthPrePassHeuristic::cmdDraw(VkCommandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t, uint32_t)
{
	uint32_t totalVertices = vertexCount * instanceCount;
	if (totalVertices < device->getConfig().depthPrePassMinVertices)
		return;

	if (state & DEPTH_ONLY)
	{
		numDrawCallsDepthOnly++;
	}

	if (state & DEPTH_EQUAL_TEST)
	{
		numDrawCallsDepthEqual++;
	}
}

void DepthPrePassHeuristic::cmdDrawIndexed(VkCommandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t,
                                           int32_t, uint32_t)
{
	uint32_t totalIndices = indexCount * instanceCount;
	if (totalIndices < device->getConfig().depthPrePassMinIndices)
		return;

	if (state & DEPTH_ONLY)
	{
		numDrawCallsDepthOnly++;
	}

	if (state & DEPTH_EQUAL_TEST)
	{
		numDrawCallsDepthEqual++;
	}
}

TileReadbackHeuristic::TileReadbackHeuristic(CommandBuffer *commandBuffer, Device *device)
    : Heuristic(device)
    , commandBuffer(commandBuffer)
{
}

void TileReadbackHeuristic::cmdBeginRenderPass(VkCommandBuffer commandBuffer,
                                               const VkRenderPassBeginInfo *pRenderPassBegin,
                                               VkSubpassContents contents)
{
	auto *renderPass = device->get<RenderPass>(pRenderPassBegin->renderPass);
	MPD_ASSERT(renderPass);

	auto &info = renderPass->getCreateInfo();

	// Check if any attachments have LOAD operation on them.
	for (uint32_t att = 0; att < info.attachmentCount; att++)
	{
		auto &attachment = info.pAttachments[att];

		bool attachmentHasReadback = false;
		if (!formatIsStencilOnly(attachment.format) && attachment.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
		{
			attachmentHasReadback = true;
		}

		if ((formatIsDepthStencil(attachment.format) || formatIsStencilOnly(attachment.format)) &&
		    attachment.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
		{
			attachmentHasReadback = true;
		}

		bool attachmentNeedsReadback = false;

		// Check if the attachment is actually used in any subpass on-tile.
		if (attachmentHasReadback && renderPass->renderPassUsesAttachmentOnTile(att))
			attachmentNeedsReadback = true;

		// Using LOAD_OP_LOAD is generally a really bad idea, so flag the issue.
		if (attachmentNeedsReadback)
		{
			renderPass->log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_TILE_READBACK,
			                "Attachment #%u (fmt: %s) in render pass has begun with VK_ATTACHMENT_LOAD_OP_LOAD.\n"
			                "Submitting this renderpass will cause the driver to inject a readback of the attachment "
			                "which will copy "
			                "in total %u pixels (renderArea = { %d, %d, %u, %u }) to the tile buffer.",
			                att, formatToString(attachment.format),
			                pRenderPassBegin->renderArea.extent.width * pRenderPassBegin->renderArea.extent.height,
			                pRenderPassBegin->renderArea.offset.x, pRenderPassBegin->renderArea.offset.y,
			                pRenderPassBegin->renderArea.extent.width, pRenderPassBegin->renderArea.extent.height);
		}
	}
}

ClearAttachmentsHeuristic::ClearAttachmentsHeuristic(CommandBuffer *commandBuffer, Device *device)
    : Heuristic(device)
    , commandBuffer(commandBuffer)
{
}

void ClearAttachmentsHeuristic::reset()
{
	renderPassInfo = nullptr;
	currentSubpass = 0;
	hasSeenDrawCall = false;
}

void ClearAttachmentsHeuristic::cmdDraw(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t)
{
	hasSeenDrawCall = true;
}

void ClearAttachmentsHeuristic::cmdDrawIndexed(VkCommandBuffer, uint32_t, uint32_t, uint32_t, int32_t, uint32_t)
{
	hasSeenDrawCall = true;
}

void ClearAttachmentsHeuristic::cmdBeginRenderPass(VkCommandBuffer, const VkRenderPassBeginInfo *pBeginInfo,
                                                   VkSubpassContents)
{
	auto *renderPass = device->get<RenderPass>(pBeginInfo->renderPass);
	MPD_ASSERT(renderPass);
	renderPassInfo = &renderPass->getCreateInfo();
	currentSubpass = 0;
	hasSeenDrawCall = false;
}

void ClearAttachmentsHeuristic::cmdSetSubpass(VkCommandBuffer, uint32_t index, VkSubpassContents)
{
	currentSubpass = index;
}

void ClearAttachmentsHeuristic::cmdSetRenderPass(VkCommandBuffer, RenderPass *renderPass)
{
	renderPassInfo = &renderPass->getCreateInfo();
	hasSeenDrawCall = false;
}

void ClearAttachmentsHeuristic::cmdClearAttachments(VkCommandBuffer, uint32_t attachmentCount,
                                                    const VkClearAttachment *pAttachments, uint32_t rectCount,
                                                    const VkClearRect *pRects)
{
	auto &subpass = renderPassInfo->pSubpasses[currentSubpass];

	uint32_t clearPixels = 0;
	for (uint32_t i = 0; i < rectCount; i++)
		clearPixels += pRects[i].layerCount * pRects[i].rect.extent.width * pRects[i].rect.extent.height;

	// Nothing to clear.
	if (!clearPixels)
		return;

	for (uint32_t i = 0; i < attachmentCount; i++)
	{
		auto &attachment = pAttachments[i];
		if (attachment.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT)
		{
			uint32_t colorAttachment = attachment.colorAttachment;
			MPD_ASSERT(colorAttachment < subpass.colorAttachmentCount);
			uint32_t framebufferAttachment = subpass.pColorAttachments[colorAttachment].attachment;
			if (framebufferAttachment != VK_ATTACHMENT_UNUSED)
			{
				auto &info = renderPassInfo->pAttachments[framebufferAttachment];
				if (info.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
				{
					// Using ClearAttachments with LOAD is very fishy, why not just CLEAR to begin with?
					commandBuffer->log(
					    VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_CLEAR_ATTACHMENTS_AFTER_LOAD,
					    "vkCmdClearAttachments is being called for color attachment #%u (fmt: %s) in this subpass, "
					    "but LOAD_OP_LOAD was used. If you need to clear the framebuffer, always use LOAD_OP_CLEAR as "
					    "vkCmdClearAttachments will create a clear quad with %u pixels.",
					    colorAttachment, formatToString(info.format), clearPixels);
				}

				if (!hasSeenDrawCall && currentSubpass == 0)
				{
					// Otherwise, if we haven't seen any draw call yet, clearing attachments sounds kinda redundant.
					// A somewhat legitimate usage pattern is using CLEAR, doing some draw calls, and then only
					// clearing depth / stencil for example for UI and similar.
					commandBuffer->log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT,
					                   MESSAGE_CODE_CLEAR_ATTACHMENTS_NO_DRAW_CALL,
					                   "vkCmdClearAttachments is being called for color attachment #%u (fmt: %s) in "
					                   "this subpass before any draw call was submitted. "
					                   "Try to use the LOAD_OP_CLEAR way of clearing as vkCmdClearAttachments will "
					                   "create a clear quad with %u pixels.",
					                   colorAttachment, formatToString(info.format), clearPixels);
				}
			}
		}

		if (subpass.pDepthStencilAttachment && (attachment.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT))
		{
			uint32_t framebufferAttachment = subpass.pDepthStencilAttachment->attachment;
			if (framebufferAttachment != VK_ATTACHMENT_UNUSED)
			{
				auto &info = renderPassInfo->pAttachments[framebufferAttachment];
				if (info.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
				{
					// Using ClearAttachments right after LOAD is redundant, applications should just CLEAR to begin with.
					commandBuffer->log(
					    VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_CLEAR_ATTACHMENTS_AFTER_LOAD,
					    "vkCmdClearAttachments is being called for depth attachment (fmt: %s) in this subpass, "
					    "but LOAD_OP_LOAD was used. If you need to clear the framebuffer, always use LOAD_OP_CLEAR as "
					    "vkCmdClearAttachments will create a clear quad of %u pixels.",
					    formatToString(info.format), clearPixels);
				}
				else if (!hasSeenDrawCall && currentSubpass == 0)
				{
					// Otherwise, if we haven't seen any draw call yet, clearing attachments sounds kinda redundant.
					// A somewhat legitimate usage pattern is using CLEAR, doing some draw calls, and then only
					// clearing depth / stencil for example for UI and similar.
					commandBuffer->log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT,
					                   MESSAGE_CODE_CLEAR_ATTACHMENTS_NO_DRAW_CALL,
					                   "vkCmdClearAttachments is being called for depth attachment (fmt: %s) in this "
					                   "subpass before any draw call was submitted. "
					                   "Try to use the LOAD_OP_CLEAR way of clearing as vkCmdClearAttachments will "
					                   "create a clear quad of %u pixels.",
					                   formatToString(info.format), clearPixels);
				}
			}
		}

		if (subpass.pDepthStencilAttachment && (attachment.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT))
		{
			uint32_t framebufferAttachment = subpass.pDepthStencilAttachment->attachment;
			if (framebufferAttachment != VK_ATTACHMENT_UNUSED)
			{
				auto &info = renderPassInfo->pAttachments[framebufferAttachment];
				if (info.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
				{
					// Using ClearAttachments right after LOAD is redundant, applications should just CLEAR to begin with.
					commandBuffer->log(
					    VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_CLEAR_ATTACHMENTS_AFTER_LOAD,
					    "vkCmdClearAttachments is being called for stencil attachment (fmt: %s) in this subpass, "
					    "but LOAD_OP_LOAD was used. If you need to clear the framebuffer, always use LOAD_OP_CLEAR as "
					    "vkCmdClearAttachments will create a clear quad of %u pixels.",
					    formatToString(info.format), clearPixels);
				}
				else if (!hasSeenDrawCall && currentSubpass == 0)
				{
					// Otherwise, if we haven't seen any draw call yet, clearing attachments sounds kinda redundant.
					// A somewhat legitimate usage pattern is using CLEAR, doing some draw calls, and then only
					// clearing depth / stencil for example for UI and similar.
					commandBuffer->log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT,
					                   MESSAGE_CODE_CLEAR_ATTACHMENTS_NO_DRAW_CALL,
					                   "vkCmdClearAttachments is being called for stencil attachment (fmt: %s) in this "
					                   "subpass before any draw call was submitted. "
					                   "Try to use the LOAD_OP_CLEAR way of clearing as vkCmdClearAttachments will "
					                   "create a clear quad of %u pixels.",
					                   formatToString(info.format), clearPixels);
				}
			}
		}
	}
}
}
