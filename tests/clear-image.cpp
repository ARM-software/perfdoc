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
#include "util.hpp"
#include <functional>
#include <memory>

using namespace MPD;
using namespace std;

class ClearImage : public VulkanTestHelper
{
	bool runTest()
	{
		for (unsigned i = 0; i < 16; i++)
			if (!testClear(i & 1, i & 2, i & 4, i & 8))
				return false;

		if (!testRedundantStore(false, 0))
			return false;
		if (!testRedundantStore(false, 1))
			return false;
		if (!testRedundantStore(true, 0))
			return false;

		return true;
	}

	bool testRedundantStore(bool positiveTest, unsigned testVariant)
	{
		resetCounts();
		const VkFormat FMT = VK_FORMAT_R8G8B8A8_UNORM;
		const uint32_t WIDTH = 64, HEIGHT = 64;

		static const uint32_t vertCode[] =
#include "quad_no_attribs.vert.inc"
		    ;

		static const uint32_t fragCode[] =
#include "quad_sampler.frag.inc"
		    ;

		// Create render target
		auto tex = make_shared<Texture>(device);
		tex->initRenderTarget2D(WIDTH, HEIGHT, FMT);

		auto tex2 = make_shared<Texture>(device);
		tex2->initRenderTarget2D(WIDTH, HEIGHT, FMT);

		auto fb = make_shared<Framebuffer>(device);
		fb->initOnlyColor(tex, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, 0, nullptr);

		auto fb2 = make_shared<Framebuffer>(device);
		fb2->initOnlyColor(tex2, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE, 0, nullptr);

		VkGraphicsPipelineCreateInfo pi = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		pi.renderPass = fb2->renderPass;
		auto pipeline = make_shared<Pipeline>(device);
		pipeline->initGraphics(vertCode, sizeof(vertCode), fragCode, sizeof(fragCode), &pi);

		VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		VkDescriptorPoolSize poolSize = {};
		poolSize.descriptorCount = 1;
		poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
		VkDescriptorPool pool;
		MPD_ASSERT_RESULT(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool));

		VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		info.magFilter = VK_FILTER_NEAREST;
		info.minFilter = VK_FILTER_NEAREST;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

		VkSampler sampler;
		MPD_ASSERT_RESULT(vkCreateSampler(device, &info, nullptr, &sampler));

		VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		allocInfo.descriptorPool = pool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &pipeline->descriptorSetLayout;
		VkDescriptorSet descSet;
		MPD_ASSERT_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descSet));

		VkDescriptorImageInfo imageInfo = {};
		imageInfo.imageView = tex->view;
		imageInfo.sampler = sampler;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.descriptorCount = 1;
		write.dstBinding = 0;
		write.dstSet = descSet;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &imageInfo;
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

		VkCommandBufferBeginInfo cbBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL,
			                                     VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL };
		VkClearValue clearValues[3];
		memset(clearValues, 0, sizeof(clearValues));

		VkRenderPassBeginInfo rbi = {};
		rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rbi.renderPass = fb->renderPass;
		rbi.framebuffer = fb->framebuffer;
		rbi.clearValueCount = 3;
		rbi.pClearValues = clearValues;

		const auto buildWork = [&](const function<void(CommandBuffer & cmd)> &work) {
			auto cmdb = make_shared<CommandBuffer>(device);
			cmdb->initPrimary();
			MPD_ASSERT_RESULT(vkBeginCommandBuffer(cmdb->commandBuffer, &cbBeginInfo));
			work(*cmdb);
			MPD_ASSERT_RESULT(vkEndCommandBuffer(cmdb->commandBuffer));

			VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
			submit.commandBufferCount = 1;
			submit.pCommandBuffers = &cmdb->commandBuffer;
			vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
			vkQueueWaitIdle(queue);
		};

		const auto renderPassWork = [&](CommandBuffer &cmd) {
			auto vkcmd = cmd.commandBuffer;
			vkCmdBeginRenderPass(vkcmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdEndRenderPass(vkcmd);
		};

		buildWork(renderPassWork);

		// Use the image somehow.
		if (!positiveTest)
		{
			switch (testVariant)
			{
			case 0:
				// Copy to another texture.
				buildWork([&](CommandBuffer &cmd) {
					auto vkcmd = cmd.commandBuffer;
					VkImageCopy range = {};
					range.extent.width = WIDTH;
					range.extent.height = HEIGHT;
					range.srcSubresource.layerCount = 1;
					range.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					range.dstSubresource = range.srcSubresource;
					vkCmdCopyImage(vkcmd, tex->image, VK_IMAGE_LAYOUT_GENERAL, tex2->image, VK_IMAGE_LAYOUT_GENERAL, 1,
					               &range);
				});
				break;

			case 1:
				// Render to another render target with tex.
				buildWork([&](CommandBuffer &cmd) {
					auto vkcmd = cmd.commandBuffer;
					VkRenderPassBeginInfo rbi = {};
					rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
					rbi.renderPass = fb2->renderPass;
					rbi.framebuffer = fb2->framebuffer;
					rbi.clearValueCount = 3;
					rbi.pClearValues = clearValues;
					vkCmdBeginRenderPass(vkcmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
					vkCmdBindPipeline(vkcmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
					vkCmdBindDescriptorSets(vkcmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipelineLayout, 0, 1,
					                        &descSet, 0, nullptr);

					VkViewport s;
					s.x = 0;
					s.y = 0;
					s.width = WIDTH;
					s.height = HEIGHT;
					s.minDepth = 0.0;
					s.maxDepth = 1.0;
					vkCmdSetViewport(vkcmd, 0, 1, &s);
					vkCmdDraw(vkcmd, 3, 1, 0, 0);
					vkCmdEndRenderPass(vkcmd);
				});
				break;

			default:
				return false;
			}
		}

		buildWork(renderPassWork);

		if (positiveTest)
		{
			if (getCount(MESSAGE_CODE_REDUNDANT_RENDERPASS_STORE) != 1)
				return false;
		}
		else
		{
			if (getCount(MESSAGE_CODE_REDUNDANT_RENDERPASS_STORE) != 0)
				return false;
		}

		vkDestroyDescriptorPool(device, pool, nullptr);
		vkDestroySampler(device, sampler, nullptr);

		return true;
	}

	bool testClear(bool positiveLoadTest, bool positiveClearAttachmentTest, bool drawTest, bool clearImageTest)
	{
		resetCounts();

		// Create shaders
		static const uint32_t vertCode[] =
#include "quad_no_attribs.vert.inc"
		    ;

		static const uint32_t fragCode[] =
#include "quad.frag.inc"
		    ;

		const VkFormat FMT = VK_FORMAT_R8G8B8A8_UNORM;
		const uint32_t WIDTH = 64, HEIGHT = 64;

		// Create render target
		auto tex = make_shared<Texture>(device);
		tex->initRenderTarget2D(WIDTH, HEIGHT, FMT);

		auto fb = make_shared<Framebuffer>(device);
		fb->initOnlyColor(tex, positiveLoadTest ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
		                  VK_ATTACHMENT_STORE_OP_STORE, 0, nullptr);

		VkGraphicsPipelineCreateInfo pi = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		pi.renderPass = fb->renderPass;
		auto pipeline = make_shared<Pipeline>(device);
		pipeline->initGraphics(vertCode, sizeof(vertCode), fragCode, sizeof(fragCode), &pi);

		VkCommandBufferBeginInfo cbBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL,
			                                     VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL };
		VkClearValue clearValues[3];
		memset(clearValues, 0, sizeof(clearValues));

		VkRenderPassBeginInfo rbi = {};
		rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rbi.renderPass = fb->renderPass;
		rbi.framebuffer = fb->framebuffer;
		rbi.clearValueCount = 3;
		rbi.pClearValues = clearValues;

		const auto buildWork = [&](const function<void(CommandBuffer & cmd)> &work) {
			auto cmdb = make_shared<CommandBuffer>(device);
			cmdb->initPrimary();
			MPD_ASSERT_RESULT(vkBeginCommandBuffer(cmdb->commandBuffer, &cbBeginInfo));
			work(*cmdb);
			MPD_ASSERT_RESULT(vkEndCommandBuffer(cmdb->commandBuffer));

			VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
			submit.commandBufferCount = 1;
			submit.pCommandBuffers = &cmdb->commandBuffer;
			vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
			vkQueueWaitIdle(queue);
		};

		const auto work = [&](CommandBuffer &cmd) {
			auto vkcmd = cmd.commandBuffer;

			if (clearImageTest)
			{
				VkClearColorValue value = {};
				VkImageSubresourceRange range = {};
				range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				range.layerCount = VK_REMAINING_ARRAY_LAYERS;
				range.levelCount = VK_REMAINING_MIP_LEVELS;
				vkCmdClearColorImage(vkcmd, fb->colorAttachments[0]->image, VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
			}

			vkCmdBeginRenderPass(vkcmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);

			if (drawTest)
			{
				VkViewport s;
				s.x = 0;
				s.y = 0;
				s.width = WIDTH;
				s.height = HEIGHT;
				s.minDepth = 0.0;
				s.maxDepth = 1.0;
				vkCmdSetViewport(vkcmd, 0, 1, &s);
				vkCmdBindPipeline(vkcmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
				vkCmdDraw(vkcmd, 3, 1, 0, 0);
			}

			if (positiveClearAttachmentTest)
			{
				VkClearRect rect = {};
				rect.layerCount = 1;
				rect.rect.extent.width = WIDTH;
				rect.rect.extent.height = HEIGHT;

				VkClearAttachment att = {};
				att.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				att.colorAttachment = 0;
				vkCmdClearAttachments(vkcmd, 1, &att, 1, &rect);
			}

			vkCmdEndRenderPass(vkcmd);
		};

		buildWork(work);

		// vkCmdClearColorImage followed by LOAD_OP_LOAD is a very inefficient way to clear.
		if (clearImageTest && positiveLoadTest)
		{
			if (getCount(MESSAGE_CODE_INEFFICIENT_CLEAR) != 1)
				return false;
		}
		else
		{
			if (getCount(MESSAGE_CODE_INEFFICIENT_CLEAR) != 0)
				return false;
		}

		// vkCmdClearColorImage followed by LOAD_OP_CLEAR is a redundant way to clear.
		if (clearImageTest && !positiveLoadTest)
		{
			if (getCount(MESSAGE_CODE_REDUNDANT_IMAGE_CLEAR) != 1)
				return false;
		}
		else
		{
			if (getCount(MESSAGE_CODE_REDUNDANT_IMAGE_CLEAR) != 0)
				return false;
		}

		// LOAD_OP_LOAD should trigger this warning.
		if (positiveLoadTest)
		{
			if (getCount(MESSAGE_CODE_TILE_READBACK) != 1)
				return false;
		}
		else
		{
			if (getCount(MESSAGE_CODE_TILE_READBACK) != 0)
				return false;
		}

		// LOAD_OP_LOAD and calling CmdClearAttachments should trigger a warning.
		if (positiveClearAttachmentTest && positiveLoadTest)
		{
			if (getCount(MESSAGE_CODE_CLEAR_ATTACHMENTS_AFTER_LOAD) != 1)
				return false;
		}
		else
		{
			if (getCount(MESSAGE_CODE_CLEAR_ATTACHMENTS_AFTER_LOAD) != 0)
				return false;
		}

		// Calling vkCmdClearAttachments as the very first command should trigger a warning.
		if (!drawTest && positiveClearAttachmentTest)
		{
			if (getCount(MESSAGE_CODE_CLEAR_ATTACHMENTS_NO_DRAW_CALL) != 1)
				return false;
		}
		else
		{
			if (getCount(MESSAGE_CODE_CLEAR_ATTACHMENTS_NO_DRAW_CALL) != 0)
				return false;
		}

		return true;
	}
};

VulkanTestHelper *MPD::createTest()
{
	return new ClearImage;
}