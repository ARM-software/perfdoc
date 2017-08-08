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
using namespace MPD;

class Multisampling : public VulkanTestHelper
{
	bool runTest()
	{
		if (!testTooLargeSampleCountAndTransient())
			return false;
		if (!testMultisampledImageRequiresMemory())
			return false;
		if (!testCmdResolve())
			return false;
		if (!testMultisampledBlending())
			return false;
		return true;
	}

	bool testMultisampledBlending()
	{
		resetCounts();

		VkRenderPassCreateInfo info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
		info.attachmentCount = 1;
		info.subpassCount = 1;

		VkAttachmentDescription attachments[2] = {};
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].samples = VK_SAMPLE_COUNT_4_BIT;
		attachments[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		attachments[1] = attachments[0];
		attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;

		const VkAttachmentReference msAttachment = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpass = {};
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &msAttachment;
		info.pSubpasses = &subpass;

		VkRenderPass renderPassMSAA;
		VkRenderPass renderPass;
		info.pAttachments = &attachments[0];
		MPD_ASSERT_RESULT(vkCreateRenderPass(device, &info, nullptr, &renderPassMSAA));
		info.pAttachments = &attachments[1];
		MPD_ASSERT_RESULT(vkCreateRenderPass(device, &info, nullptr, &renderPass));

		// Create shaders
		static const uint32_t vertCode[] =
#include "quad_no_attribs.vert.inc"
		    ;

		static const uint32_t fragCode[] =
#include "quad.frag.inc"
		    ;

		VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		VkGraphicsPipelineCreateInfo createInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		createInfo.pMultisampleState = &ms;

		VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
		VkPipelineColorBlendAttachmentState att = {};
		att.blendEnable = true;
		att.colorWriteMask = 0xf;
		att.colorBlendOp = VK_BLEND_OP_ADD;
		att.alphaBlendOp = VK_BLEND_OP_ADD;
		att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blend.attachmentCount = 1;
		blend.pAttachments = &att;
		createInfo.pColorBlendState = &blend;

		resetCounts();
		Pipeline msaaPipe(device);
		ms.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;
		createInfo.renderPass = renderPassMSAA;
		msaaPipe.initGraphics(vertCode, sizeof(vertCode), fragCode, sizeof(fragCode), &createInfo);

		if (getCount(MESSAGE_CODE_NOT_FULL_THROUGHPUT_BLENDING) != 1)
			return false;

		resetCounts();
		Pipeline nomsaaPipe(device);
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		createInfo.renderPass = renderPass;
		nomsaaPipe.initGraphics(vertCode, sizeof(vertCode), fragCode, sizeof(fragCode), &createInfo);

		if (getCount(MESSAGE_CODE_NOT_FULL_THROUGHPUT_BLENDING) != 0)
			return false;

		vkDestroyRenderPass(device, renderPassMSAA, nullptr);
		vkDestroyRenderPass(device, renderPass, nullptr);
		return true;
	}

	bool testCmdResolve()
	{
		resetCounts();

		VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		info.format = VK_FORMAT_R8G8B8A8_UNORM;
		info.samples = VK_SAMPLE_COUNT_4_BIT;
		info.arrayLayers = 1;
		info.mipLevels = 1;
		info.imageType = VK_IMAGE_TYPE_2D;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.usage =
		    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		info.extent.width = 64;
		info.extent.height = 64;
		info.extent.depth = 1;

		VkImage msImage;
		VkImage image;
		MPD_ASSERT_RESULT(vkCreateImage(device, &info, nullptr, &msImage));

		VkMemoryRequirements memoryRequirements;
		vkGetImageMemoryRequirements(device, msImage, &memoryRequirements);
		VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		allocInfo.allocationSize = memoryRequirements.size;
		allocInfo.memoryTypeIndex = ctz(memoryRequirements.memoryTypeBits);
		VkDeviceMemory msMemory;
		MPD_ASSERT_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &msMemory));
		MPD_ASSERT_RESULT(vkBindImageMemory(device, msImage, msMemory, 0));

		info.samples = VK_SAMPLE_COUNT_1_BIT;
		MPD_ASSERT_RESULT(vkCreateImage(device, &info, nullptr, &image));

		vkGetImageMemoryRequirements(device, image, &memoryRequirements);
		allocInfo.allocationSize = memoryRequirements.size;
		allocInfo.memoryTypeIndex = ctz(memoryRequirements.memoryTypeBits);
		VkDeviceMemory memory;
		MPD_ASSERT_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &memory));
		MPD_ASSERT_RESULT(vkBindImageMemory(device, image, memory, 0));

		CommandBuffer cmd(device);
		cmd.initPrimary();

		VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		vkBeginCommandBuffer(cmd.commandBuffer, &beginInfo);

		VkImageResolve region = {};
		region.extent = info.extent;
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.dstSubresource.layerCount = 1;
		region.srcSubresource = region.dstSubresource;
		vkCmdResolveImage(cmd.commandBuffer, msImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
		                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		if (getCount(MESSAGE_CODE_RESOLVE_IMAGE) != 1)
			return false;

		MPD_ASSERT_RESULT(vkEndCommandBuffer(cmd.commandBuffer));
		vkDestroyImage(device, image, nullptr);
		vkDestroyImage(device, msImage, nullptr);
		vkFreeMemory(device, msMemory, nullptr);
		vkFreeMemory(device, memory, nullptr);
		return true;
	}

	bool testMultisampledImageRequiresMemory(bool load, bool store)
	{
		resetCounts();
		VkRenderPassCreateInfo info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
		info.attachmentCount = 2;
		info.subpassCount = 1;

		VkAttachmentDescription attachments[2] = {};
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachments[0].loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[0].storeOp = store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].samples = VK_SAMPLE_COUNT_4_BIT;
		attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		attachments[1] = attachments[0];
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;

		const VkAttachmentReference msAttachment = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		const VkAttachmentReference attachment = { 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpass = {};
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &msAttachment;
		subpass.pResolveAttachments = &attachment;
		info.pSubpasses = &subpass;
		info.pAttachments = attachments;

		VkRenderPass renderPass;
		MPD_ASSERT_RESULT(vkCreateRenderPass(device, &info, nullptr, &renderPass));

		if (load || store)
		{
			if (getCount(MESSAGE_CODE_MULTISAMPLED_IMAGE_REQUIRES_MEMORY) != 1)
				return false;
		}
		else
		{
			if (getCount(MESSAGE_CODE_MULTISAMPLED_IMAGE_REQUIRES_MEMORY) != 0)
				return false;
		}

		if (getCount(MESSAGE_CODE_RESOLVE_IMAGE) != 0)
			return false;

		vkDestroyRenderPass(device, renderPass, nullptr);
		return true;
	}

	bool testMultisampledImageRequiresMemory()
	{
		if (!testMultisampledImageRequiresMemory(false, false))
			return false;
		if (!testMultisampledImageRequiresMemory(true, false))
			return false;
		if (!testMultisampledImageRequiresMemory(false, true))
			return false;
		if (!testMultisampledImageRequiresMemory(true, true))
			return false;
		return true;
	}

	bool testTooLargeSampleCountAndTransient(VkSampleCountFlagBits samples, bool tooLarge, bool transient)
	{
		resetCounts();
		VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		info.format = VK_FORMAT_R8G8B8A8_UNORM;
		info.samples = samples;
		info.arrayLayers = 1;
		info.mipLevels = 1;
		info.imageType = VK_IMAGE_TYPE_2D;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (transient ? VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT : 0);
		info.extent.width = 64;
		info.extent.height = 64;
		info.extent.depth = 1;

		VkImageFormatProperties properties;
		vkGetPhysicalDeviceImageFormatProperties(gpu, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D,
		                                         VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0,
		                                         &properties);
		MPD_ALWAYS_ASSERT(properties.sampleCounts & samples);

		VkImage image;
		MPD_ASSERT_RESULT(vkCreateImage(device, &info, nullptr, &image));

		if (tooLarge)
		{
			if (getCount(MESSAGE_CODE_TOO_LARGE_SAMPLE_COUNT) != 1)
				return false;
		}
		else
		{
			if (getCount(MESSAGE_CODE_TOO_LARGE_SAMPLE_COUNT) != 0)
				return false;
		}

		if (transient)
		{
			if (getCount(MESSAGE_CODE_NON_LAZY_MULTISAMPLED_IMAGE) != 0)
				return false;
		}
		else
		{
			if (getCount(MESSAGE_CODE_NON_LAZY_MULTISAMPLED_IMAGE) != 1)
				return false;
		}

		if (getCount(MESSAGE_CODE_RESOLVE_IMAGE) != 0)
			return false;

		vkDestroyImage(device, image, nullptr);
		return true;
	}

	bool testTooLargeSampleCountAndTransient()
	{
		if (!testTooLargeSampleCountAndTransient(
		        static_cast<VkSampleCountFlagBits>(getConfig().maxEfficientSamples << 1), true, false))
			return false;
		if (!testTooLargeSampleCountAndTransient(static_cast<VkSampleCountFlagBits>(getConfig().maxEfficientSamples),
		                                         false, false))
			return false;
		if (!testTooLargeSampleCountAndTransient(
		        static_cast<VkSampleCountFlagBits>(getConfig().maxEfficientSamples << 1), true, true))
			return false;
		if (!testTooLargeSampleCountAndTransient(static_cast<VkSampleCountFlagBits>(getConfig().maxEfficientSamples),
		                                         false, true))
			return false;

		return true;
	};
};

VulkanTestHelper *MPD::createTest()
{
	return new Multisampling;
}
