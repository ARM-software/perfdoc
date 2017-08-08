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
#include "perfdoc.hpp"
#include "util/util.hpp"
#include <vector>

using namespace MPD;
using namespace std;

class CommandBufferTest : public VulkanTestHelper
{
	bool runTest()
	{
		if (!testSimultaneousUseBit())
			return false;

		if (!testSmallIndexedDrawcalls())
			return false;

		if (!testDepthPrePass())
			return false;

		if (!testIndexScanning())
			return false;

		return true;
	}

	bool testSimultaneousUseBit()
	{
		resetCounts();
		VkCommandPoolCreateInfo cpAllocInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL,
			                                    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, 0 };
		VkCommandPool commandPool;
		MPD_ASSERT_RESULT(vkCreateCommandPool(device, &cpAllocInfo, NULL, &commandPool));

		VkCommandBufferAllocateInfo cbAllocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, commandPool,
			                                        VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
		VkCommandBuffer commandBuffer;
		MPD_ASSERT_RESULT(vkAllocateCommandBuffers(device, &cbAllocInfo, &commandBuffer));

		VkCommandBufferBeginInfo cbBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL,
			                                     VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL };
		MPD_ASSERT_RESULT(vkBeginCommandBuffer(commandBuffer, &cbBeginInfo));
		if (getCount(MESSAGE_CODE_COMMAND_BUFFER_SIMULTANEOUS_USE) != 0)
			return false;

		MPD_ASSERT_RESULT(vkEndCommandBuffer(commandBuffer));

		VkCommandBufferBeginInfo cbBeginInfoWarn = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL,
			                                         VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT, NULL };
		MPD_ASSERT_RESULT(vkBeginCommandBuffer(commandBuffer, &cbBeginInfoWarn));
		if (getCount(MESSAGE_CODE_COMMAND_BUFFER_SIMULTANEOUS_USE) != 1)
			return false;

		MPD_ASSERT_RESULT(vkEndCommandBuffer(commandBuffer));

		// teardown
		vkDestroyCommandPool(device, commandPool, NULL);

		return true;
	}

	bool testDepthPrePass()
	{
		resetCounts();
		const VkFormat FMT = VK_FORMAT_R8G8B8A8_UNORM;
		const VkFormat DEPTH_FMT = VK_FORMAT_D32_SFLOAT;
		const uint32_t WIDTH = 64, HEIGHT = 64;

		// Create texture
		auto tex = make_shared<Texture>(device);
		tex->initRenderTarget2D(WIDTH, HEIGHT, FMT);

		auto texDepth = make_shared<Texture>(device);
		texDepth->initDepthStencil(WIDTH, HEIGHT, DEPTH_FMT);

		// Create FB
		auto fb = make_shared<Framebuffer>(device);
		fb->initDepthColor(texDepth, tex);

		// Create shaders
		static const uint32_t vertCode[] =
#include "quad_no_attribs.vert.inc"
		    ;

		static const uint32_t fragCode[] =
#include "quad.frag.inc"
		    ;

		// Crete pipeline

		VkPipelineColorBlendAttachmentState ColorWriteMaskOff = {};
		VkPipelineColorBlendAttachmentState ColorWriteMaskOn = {};
		ColorWriteMaskOn.colorWriteMask = 0xF;

		VkPipelineColorBlendStateCreateInfo cbDepthOnlyState = {};
		cbDepthOnlyState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cbDepthOnlyState.attachmentCount = 1;
		cbDepthOnlyState.pAttachments = &ColorWriteMaskOff;

		VkPipelineColorBlendStateCreateInfo cbDepthEqualState = {};
		cbDepthEqualState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cbDepthEqualState.attachmentCount = 1;
		cbDepthEqualState.pAttachments = &ColorWriteMaskOn;

		VkPipelineDepthStencilStateCreateInfo dsDepthOnlyState = {};
		dsDepthOnlyState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		dsDepthOnlyState.depthTestEnable = VK_TRUE;
		dsDepthOnlyState.depthWriteEnable = VK_TRUE;
		dsDepthOnlyState.depthCompareOp = VK_COMPARE_OP_LESS;

		VkPipelineDepthStencilStateCreateInfo dsDepthEqualState = {};
		dsDepthEqualState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		dsDepthEqualState.depthTestEnable = VK_TRUE;
		dsDepthEqualState.depthWriteEnable = VK_FALSE;
		dsDepthEqualState.depthCompareOp = VK_COMPARE_OP_EQUAL;

		VkGraphicsPipelineCreateInfo piDepthOnly = {};
		piDepthOnly.renderPass = fb->renderPass;
		piDepthOnly.pDepthStencilState = &dsDepthOnlyState;
		piDepthOnly.pColorBlendState = &cbDepthOnlyState;

		auto pipelineDepthOnly = make_shared<Pipeline>(device);
		pipelineDepthOnly->initGraphics(vertCode, sizeof(vertCode), fragCode, sizeof(fragCode), &piDepthOnly);

		VkGraphicsPipelineCreateInfo piDepthEqual = {};
		piDepthEqual.renderPass = fb->renderPass;
		piDepthEqual.pDepthStencilState = &dsDepthEqualState;
		piDepthEqual.pColorBlendState = &cbDepthEqualState;

		auto pipelineDepthEqual = make_shared<Pipeline>(device);
		pipelineDepthEqual->initGraphics(vertCode, sizeof(vertCode), fragCode, sizeof(fragCode), &piDepthEqual);

		// Create command buffer
		auto cmdb = make_shared<CommandBuffer>(device);
		cmdb->initPrimary();

		// Create index buffer
		auto idxBuff = make_shared<Buffer>(device);
		idxBuff->init(sizeof(uint32_t) * 3, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, memoryProperties);

		// Draw
		VkCommandBufferBeginInfo cbBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL,
			                                     VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL };
		MPD_ASSERT_RESULT(vkBeginCommandBuffer(cmdb->commandBuffer, &cbBeginInfo));

		VkClearValue clearValues[3];
		memset(clearValues, 0, sizeof(clearValues));

		VkRenderPassBeginInfo rbi = {};
		rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rbi.renderPass = fb->renderPass;
		rbi.framebuffer = fb->framebuffer;
		rbi.clearValueCount = 3;
		rbi.pClearValues = clearValues;

		VkViewport s;
		s.x = 0;
		s.y = 0;
		s.width = WIDTH;
		s.height = HEIGHT;
		s.minDepth = 0.0;
		s.maxDepth = 1.0;
		vkCmdSetViewport(cmdb->commandBuffer, 0, 1, &s);
		vkCmdBindIndexBuffer(cmdb->commandBuffer, idxBuff->buffer, 0, VK_INDEX_TYPE_UINT32);

		vkCmdBeginRenderPass(cmdb->commandBuffer, &rbi, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(cmdb->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineDepthOnly->pipeline);
		for (unsigned i = 0; i < 30; ++i)
			vkCmdDrawIndexed(cmdb->commandBuffer, 3, 10, 0, 0, 0);

		vkCmdBindPipeline(cmdb->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineDepthEqual->pipeline);
		for (unsigned i = 0; i < 30; ++i)
			vkCmdDrawIndexed(cmdb->commandBuffer, 3, 10, 0, 0, 0);
		vkCmdEndRenderPass(cmdb->commandBuffer);

		if (getCount(MESSAGE_CODE_DEPTH_PRE_PASS) != 0)
			return false;

		vkCmdBeginRenderPass(cmdb->commandBuffer, &rbi, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(cmdb->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineDepthOnly->pipeline);
		for (unsigned i = 0; i < 30; ++i)
			vkCmdDrawIndexed(cmdb->commandBuffer, 3, 1000, 0, 0, 0);

		vkCmdBindPipeline(cmdb->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineDepthEqual->pipeline);
		for (unsigned i = 0; i < 30; ++i)
			vkCmdDrawIndexed(cmdb->commandBuffer, 3, 1000, 0, 0, 0);
		vkCmdEndRenderPass(cmdb->commandBuffer);

		if (getCount(MESSAGE_CODE_DEPTH_PRE_PASS) != 1)
			return false;

		return true;
	}

	bool testIndexScanning()
	{
		const VkFormat FMT = VK_FORMAT_R8G8B8A8_UNORM;
		const uint32_t WIDTH = 64, HEIGHT = 64;

		// Create render target
		auto tex = make_shared<Texture>(device);
		tex->initRenderTarget2D(WIDTH, HEIGHT, FMT);

		// Create FB
		auto fb = make_shared<Framebuffer>(device);
		fb->initOnlyColor(tex);

		// Create shaders
		static const uint32_t vertCode[] =
#include "quad_no_attribs.vert.inc"
		    ;

		static const uint32_t fragCode[] =
#include "quad.frag.inc"
		    ;

		// Crete pipeline
		VkGraphicsPipelineCreateInfo pplineInf = {};
		VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
		ia.primitiveRestartEnable = false;
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		pplineInf.pInputAssemblyState = &ia;
		pplineInf.renderPass = fb->renderPass;

		auto pplineNoRestart = make_shared<Pipeline>(device);
		auto pplineRestart = make_shared<Pipeline>(device);
		pplineNoRestart->initGraphics(vertCode, sizeof(vertCode), fragCode, sizeof(fragCode), &pplineInf);
		ia.primitiveRestartEnable = true;
		pplineRestart->initGraphics(vertCode, sizeof(vertCode), fragCode, sizeof(fragCode), &pplineInf);

		// Try an index buffer which doesn't reuse any indices.
		vector<uint16_t> indices(cfg.indexBufferScanMinIndexCount);
		for (unsigned i = 0; i < cfg.indexBufferScanMinIndexCount; i++)
			indices[i] = i;

		auto idxBuffNoReuse = make_shared<Buffer>(device);
		idxBuffNoReuse->init(sizeof(uint16_t) * cfg.indexBufferScanMinIndexCount, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		                     memoryProperties, HOST_ACCESS_WRITE, indices.data());

		indices.back() = 0xffff;
		auto idxBuffNoReuseSparse = make_shared<Buffer>(device);
		idxBuffNoReuseSparse->init(sizeof(uint16_t) * cfg.indexBufferScanMinIndexCount,
		                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT, memoryProperties, HOST_ACCESS_WRITE,
		                           indices.data());

		// Cache-thrashing tests.
		const unsigned reuseFactor = 16;
		auto idxBuffThrash = make_shared<Buffer>(device);
		auto idxBuffNoThrash = make_shared<Buffer>(device);
		indices.resize(reuseFactor * cfg.indexBufferScanMinIndexCount);

		// Worst possible reuse.
		for (unsigned i = 0; i < reuseFactor; i++)
			for (unsigned j = 0; j < cfg.indexBufferScanMinIndexCount; j++)
				indices[j + i * cfg.indexBufferScanMinIndexCount] = j;

		idxBuffThrash->init(sizeof(uint16_t) * reuseFactor * cfg.indexBufferScanMinIndexCount,
		                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT, memoryProperties, HOST_ACCESS_WRITE, indices.data());

		// Best possible reuse.
		for (unsigned j = 0; j < cfg.indexBufferScanMinIndexCount; j++)
			for (unsigned i = 0; i < reuseFactor; i++)
				indices[j * reuseFactor + i] = j;

		idxBuffNoThrash->init(sizeof(uint16_t) * reuseFactor * cfg.indexBufferScanMinIndexCount,
		                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT, memoryProperties, HOST_ACCESS_WRITE, indices.data());

		const auto buildRenderPass = [&](const Buffer &buffer, unsigned count, bool primitiveRestart) {
			// Create command buffer
			auto cmdb = make_shared<CommandBuffer>(device);
			cmdb->initPrimary();

			// Draw
			VkCommandBufferBeginInfo cbBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL,
				                                     VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL };
			MPD_ASSERT_RESULT(vkBeginCommandBuffer(cmdb->commandBuffer, &cbBeginInfo));

			VkClearValue clearValues[3];
			memset(clearValues, 0, sizeof(clearValues));

			VkRenderPassBeginInfo rbi = {};
			rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			rbi.renderPass = fb->renderPass;
			rbi.framebuffer = fb->framebuffer;
			rbi.clearValueCount = 3;
			rbi.pClearValues = clearValues;

			VkViewport s;
			s.x = 0;
			s.y = 0;
			s.width = WIDTH;
			s.height = HEIGHT;
			s.minDepth = 0.0;
			s.maxDepth = 1.0;

			vkCmdBeginRenderPass(cmdb->commandBuffer, &rbi, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdSetViewport(cmdb->commandBuffer, 0, 1, &s);
			vkCmdBindPipeline(cmdb->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			                  primitiveRestart ? pplineRestart->pipeline : pplineNoRestart->pipeline);
			vkCmdBindIndexBuffer(cmdb->commandBuffer, buffer.buffer, 0, VK_INDEX_TYPE_UINT16);
			vkCmdDrawIndexed(cmdb->commandBuffer, count, 1, 0, 0, 0);
			vkCmdEndRenderPass(cmdb->commandBuffer);

			MPD_ASSERT_RESULT(vkEndCommandBuffer(cmdb->commandBuffer));

			VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
			submit.commandBufferCount = 1;
			submit.pCommandBuffers = &cmdb->commandBuffer;
			vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
			vkQueueWaitIdle(queue);
		};

		// One index is way off (primitive restart, but we aren't using primitive restart).
		resetCounts();
		buildRenderPass(*idxBuffNoReuseSparse, cfg.indexBufferScanMinIndexCount, false);
		if (getCount(MESSAGE_CODE_INDEX_BUFFER_SPARSE) != 1)
			return false;
		if (getCount(MESSAGE_CODE_INDEX_BUFFER_CACHE_THRASHING) != 0)
			return false;

		// One index is way off, but it's primitive restart, so it's okay.
		resetCounts();
		buildRenderPass(*idxBuffNoReuseSparse, cfg.indexBufferScanMinIndexCount, true);
		if (getCount(MESSAGE_CODE_INDEX_BUFFER_SPARSE) != 0)
			return false;
		if (getCount(MESSAGE_CODE_INDEX_BUFFER_CACHE_THRASHING) != 0)
			return false;

		// No reuse no sparse-ness.
		resetCounts();
		buildRenderPass(*idxBuffNoReuse, cfg.indexBufferScanMinIndexCount, false);
		if (getCount(MESSAGE_CODE_INDEX_BUFFER_SPARSE) != 0)
			return false;
		if (getCount(MESSAGE_CODE_INDEX_BUFFER_CACHE_THRASHING) != 0)
			return false;

		// Thrash test.
		resetCounts();
		buildRenderPass(*idxBuffThrash, reuseFactor * cfg.indexBufferScanMinIndexCount, false);
		if (getCount(MESSAGE_CODE_INDEX_BUFFER_SPARSE) != 0)
			return false;
		if (getCount(MESSAGE_CODE_INDEX_BUFFER_CACHE_THRASHING) != 1)
			return false;

		// No-thrash test.
		resetCounts();
		buildRenderPass(*idxBuffNoThrash, reuseFactor * cfg.indexBufferScanMinIndexCount, false);
		if (getCount(MESSAGE_CODE_INDEX_BUFFER_SPARSE) != 0)
			return false;
		if (getCount(MESSAGE_CODE_INDEX_BUFFER_CACHE_THRASHING) != 0)
			return false;

		return true;
	}

	bool testSmallIndexedDrawcalls()
	{
		resetCounts();
		const VkFormat FMT = VK_FORMAT_R8G8B8A8_UNORM;
		const uint32_t WIDTH = 64, HEIGHT = 64;

		// Create render target
		auto tex = make_shared<Texture>(device);
		tex->initRenderTarget2D(WIDTH, HEIGHT, FMT);

		// Create FB
		auto fb = make_shared<Framebuffer>(device);
		fb->initOnlyColor(tex);

		// Create shaders
		static const uint32_t vertCode[] =
#include "quad_no_attribs.vert.inc"
		    ;

		static const uint32_t fragCode[] =
#include "quad.frag.inc"
		    ;

		// Crete pipeline
		VkGraphicsPipelineCreateInfo pplineInf = {};
		pplineInf.renderPass = fb->renderPass;
		auto ppline = make_shared<Pipeline>(device);
		ppline->initGraphics(vertCode, sizeof(vertCode), fragCode, sizeof(fragCode), &pplineInf);

		// Create command buffer
		auto cmdb = make_shared<CommandBuffer>(device);
		cmdb->initPrimary();

		// Create index buffer
		auto idxBuff = make_shared<Buffer>(device);
		idxBuff->init(sizeof(uint32_t) * 3, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, memoryProperties);

		// Draw
		VkCommandBufferBeginInfo cbBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL,
			                                     VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL };
		MPD_ASSERT_RESULT(vkBeginCommandBuffer(cmdb->commandBuffer, &cbBeginInfo));

		VkClearValue clearValues[3];
		memset(clearValues, 0, sizeof(clearValues));

		VkRenderPassBeginInfo rbi = {};
		rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rbi.renderPass = fb->renderPass;
		rbi.framebuffer = fb->framebuffer;
		rbi.clearValueCount = 3;
		rbi.pClearValues = clearValues;

		vkCmdBeginRenderPass(cmdb->commandBuffer, &rbi, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport s;
		s.x = 0;
		s.y = 0;
		s.width = WIDTH;
		s.height = HEIGHT;
		s.minDepth = 0.0;
		s.maxDepth = 1.0;
		vkCmdSetViewport(cmdb->commandBuffer, 0, 1, &s);

		vkCmdBindPipeline(cmdb->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ppline->pipeline);

		vkCmdBindIndexBuffer(cmdb->commandBuffer, idxBuff->buffer, 0, VK_INDEX_TYPE_UINT32);

		for (unsigned i = 0; i < 5; ++i)
			vkCmdDrawIndexed(cmdb->commandBuffer, 3, 1, 0, 0, 0);

		if (getCount(MESSAGE_CODE_MANY_SMALL_INDEXED_DRAWCALLS) != 0)
			return false;

		for (unsigned i = 0; i < 10; ++i)
			vkCmdDrawIndexed(cmdb->commandBuffer, 3, 1, 0, 0, 0);

		vkCmdEndRenderPass(cmdb->commandBuffer);
		vkEndCommandBuffer(cmdb->commandBuffer);

		if (getCount(MESSAGE_CODE_MANY_SMALL_INDEXED_DRAWCALLS) != 1)
			return false;

		return true;
	}
};

VulkanTestHelper *MPD::createTest()
{
	return new CommandBufferTest;
}
