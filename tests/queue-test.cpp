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
#include <functional>

using namespace MPD;
using namespace std;

class QueueTest : public VulkanTestHelper
{
	bool runTest()
	{
		if (!testBarriers())
			return false;

		return true;
	}

	bool testBarriers()
	{
		const VkFormat FMT = VK_FORMAT_R8G8B8A8_UNORM;
		const uint32_t WIDTH = 64, HEIGHT = 64;

		// Create render target
		auto tex = make_shared<Texture>(device);
		tex->initRenderTarget2D(WIDTH, HEIGHT, FMT);

		// Create FB
		auto fb = make_shared<Framebuffer>(device);
		fb->initOnlyColor(tex);

		VkSubpassDependency dependency = {};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
		dependency.dstStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
		auto fbBubble = make_shared<Framebuffer>(device);
		fbBubble->initOnlyColor(tex, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, 1, &dependency);

		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		auto fbNoBubble = make_shared<Framebuffer>(device);
		fbNoBubble->initOnlyColor(tex, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, 1, &dependency);

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

		const auto buildWork = [&](const std::function<void(VkCommandBuffer)> &work) {
			auto cmdb = make_shared<CommandBuffer>(device);
			cmdb->initPrimary();
			MPD_ASSERT_RESULT(vkBeginCommandBuffer(cmdb->commandBuffer, &cbBeginInfo));
			work(cmdb->commandBuffer);
			MPD_ASSERT_RESULT(vkEndCommandBuffer(cmdb->commandBuffer));

			VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
			submit.commandBufferCount = 1;
			submit.pCommandBuffers = &cmdb->commandBuffer;
			vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
			vkQueueWaitIdle(queue);
		};

		// We have a fragment -> vertex barrier at the start, but no work has been queued up in fragment,
		// so there should be no bubble reported.
		resetCounts();
		buildWork([&](VkCommandBuffer cmd) {
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0,
			                     nullptr, 0, nullptr, 0, nullptr);
			vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdEndRenderPass(cmd);
		});

		if (getCount(MESSAGE_CODE_PIPELINE_BUBBLE) != 0)
			return false;

		// This time there was some fragment work queued up, so we should get a bubble now.
		resetCounts();
		buildWork([&](VkCommandBuffer cmd) {
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0,
			                     nullptr, 0, nullptr, 0, nullptr);
			vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdEndRenderPass(cmd);
		});

		// GEOMETRY stage will see a bubble, as will FRAGMENT.
		if (getCount(MESSAGE_CODE_PIPELINE_BUBBLE) != 2)
			return false;

		resetCounts();
		buildWork([&](VkCommandBuffer cmd) {
			// Make a self-dependency via TRANSFER stage. We don't submit any work to TRANSFER here, so this is not a bubble.
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
			                     nullptr, 0, nullptr, 0, nullptr);
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
			                     nullptr, 0, nullptr, 0, nullptr);
			vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdEndRenderPass(cmd);
		});

		if (getCount(MESSAGE_CODE_PIPELINE_BUBBLE) != 0)
			return false;

		resetCounts();
		buildWork([&](VkCommandBuffer cmd) {
			// Make a self-dependency via TRANSFER stage. We submit some work to TRANSFER stage, so this is a bubble.
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
			                     nullptr, 0, nullptr, 0, nullptr);

			VkClearColorValue color = {};
			VkImageSubresourceRange range = {};
			range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			range.layerCount = 1;
			range.levelCount = 1;
			vkCmdClearColorImage(cmd, tex->image, VK_IMAGE_LAYOUT_GENERAL, &color, 1, &range);
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
			                     nullptr, 0, nullptr, 0, nullptr);
			vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdEndRenderPass(cmd);
		});

		if (getCount(MESSAGE_CODE_PIPELINE_BUBBLE) != 1)
			return false;

		resetCounts();
		VkEventCreateInfo eventInfo = { VK_STRUCTURE_TYPE_EVENT_CREATE_INFO };
		VkEvent event0, event1;
		vkCreateEvent(device, &eventInfo, nullptr, &event0);
		vkCreateEvent(device, &eventInfo, nullptr, &event1);

		buildWork([&](VkCommandBuffer cmd) {
			// Make a self-dependency via TRANSFER stage. We submit some work to TRANSFER stage, so this is a bubble.
			vkCmdSetEvent(cmd, event0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
			vkCmdWaitEvents(cmd, 1, &event0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			                nullptr, 0, nullptr, 0, nullptr);

			VkClearColorValue color = {};
			VkImageSubresourceRange range = {};
			range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			range.layerCount = 1;
			range.levelCount = 1;
			vkCmdClearColorImage(cmd, tex->image, VK_IMAGE_LAYOUT_GENERAL, &color, 1, &range);
			vkCmdSetEvent(cmd, event1, VK_PIPELINE_STAGE_TRANSFER_BIT);
			vkCmdWaitEvents(cmd, 1, &event1, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			                nullptr, 0, nullptr, 0, nullptr);
			vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdEndRenderPass(cmd);
		});
		vkDestroyEvent(device, event0, nullptr);
		vkDestroyEvent(device, event1, nullptr);

		if (getCount(MESSAGE_CODE_PIPELINE_BUBBLE) != 1)
			return false;

		// Get implicit bubble with render pass dependencies.
		resetCounts();
		buildWork([&](VkCommandBuffer cmd) {
			auto rbiTmp = rbi;
			rbiTmp.renderPass = fbBubble->renderPass;
			rbiTmp.framebuffer = fbBubble->framebuffer;
			vkCmdBeginRenderPass(cmd, &rbiTmp, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdEndRenderPass(cmd);
		});

		if (getCount(MESSAGE_CODE_PIPELINE_BUBBLE) != 2)
			return false;

		// Just use fragment -> fragment implicit barrier, shouldn't get warning here.
		resetCounts();
		buildWork([&](VkCommandBuffer cmd) {
			auto rbiTmp = rbi;
			rbiTmp.renderPass = fbNoBubble->renderPass;
			rbiTmp.framebuffer = fbNoBubble->framebuffer;
			vkCmdBeginRenderPass(cmd, &rbiTmp, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdEndRenderPass(cmd);
		});

		if (getCount(MESSAGE_CODE_PIPELINE_BUBBLE) != 0)
			return false;

		return true;
	}
};

VulkanTestHelper *MPD::createTest()
{
	return new QueueTest;
}
