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

#include "libvulkan-stub.h"
#include "util.hpp"
#include "layer/perfdoc.hpp"
#include "spirv_cross.hpp"
#include <string.h>

using namespace std;
using namespace spirv_cross;

namespace MPD
{
void Shader::init(const uint32_t *pCode, size_t size)
{
	VkShaderModuleCreateInfo info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	info.pCode = pCode;
	info.codeSize = size;

	MPD_ASSERT_RESULT(vkCreateShaderModule(device, &info, nullptr, &module));
}

Pipeline::~Pipeline()
{
	if (pipeline)
		vkDestroyPipeline(device, pipeline, nullptr);

	if (pipelineLayout)
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

	if (descriptorSetLayout)
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
}

void Pipeline::initLayouts(const uint32_t **codes, size_t *sizes, unsigned shaderCount)
{
	const VkShaderStageFlags stageMask =
	    (shaderCount == 1) ? VK_SHADER_STAGE_COMPUTE_BIT : VK_SHADER_STAGE_ALL_GRAPHICS;

	vector<VkDescriptorSetLayoutBinding> bindings;
	VkPushConstantRange pushConstants = { stageMask, 0, 0 };

	for (unsigned i = 0; i < shaderCount; ++i)
	{
		vector<uint32_t> spirv(codes[i], codes[i] + sizes[i] / sizeof(uint32_t));
		Compiler comp(move(spirv));

		auto resources = comp.get_shader_resources();

		if (!resources.push_constant_buffers.empty())
		{
			pushConstants.size = max<size_t>(
			    pushConstants.size,
			    comp.get_declared_struct_size(comp.get_type(resources.push_constant_buffers.front().base_type_id)));
		}

		const auto addBinding = [&](const std::vector<Resource> &resources, VkDescriptorType type) {
			for (auto &res : resources)
			{
				auto set = comp.get_decoration(res.id, spv::DecorationDescriptorSet);
				MPD_ALWAYS_ASSERT(set == 0); // To make it simple, so we just have one descriptor set layout.
				auto binding = comp.get_decoration(res.id, spv::DecorationBinding);

				VkDescriptorSetLayoutBinding newBinding = { binding, type, 1, stageMask, nullptr };

				// Check if the binding exists (from a previous shader)
				for (const auto &b : bindings)
				{
					if (b.binding == binding)
					{
						MPD_ASSERT(memcmp(&b, &newBinding, sizeof(b)) == 0);
						return;
					}
				}

				bindings.push_back(newBinding);
			}
		};

		addBinding(resources.sampled_images, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		addBinding(resources.storage_images, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		addBinding(resources.uniform_buffers, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		addBinding(resources.storage_buffers, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	}

	VkDescriptorSetLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	layoutInfo.bindingCount = bindings.size();
	layoutInfo.pBindings = bindings.data();
	MPD_ASSERT_RESULT(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout));

	VkPipelineLayoutCreateInfo pipeLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pipeLayoutInfo.setLayoutCount = 1;
	pipeLayoutInfo.pSetLayouts = &descriptorSetLayout;

	if (pushConstants.size > 0)
	{
		pipeLayoutInfo.pushConstantRangeCount = 1;
		pipeLayoutInfo.pPushConstantRanges = &pushConstants;
	}

	MPD_ASSERT_RESULT(vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &pipelineLayout));
}

void Pipeline::initCompute(const uint32_t *code, size_t size)
{
	shaders[0].reset(new Shader(device));
	shaders[0]->init(code, size);

	initLayouts(&code, &size, 1);

	VkComputePipelineCreateInfo info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	info.stage.pName = "main";
	info.stage.module = shaders[0]->module;
	info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	info.layout = pipelineLayout;

	VkPipeline pipeline;
	MPD_ASSERT_RESULT(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline));
}

void Pipeline::initGraphics(const uint32_t *vertCode, size_t vertSize, const uint32_t *fragCode, size_t fragSize,
                            const VkGraphicsPipelineCreateInfo *createInfo)
{
	shaders[0].reset(new Shader(device));
	shaders[1].reset(new Shader(device));
	shaders[0]->init(vertCode, vertSize);
	shaders[1]->init(fragCode, fragSize);

	VkGraphicsPipelineCreateInfo inf = *createInfo;
	inf.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	inf.pNext = nullptr;
	inf.flags = 0;

	VkPipelineShaderStageCreateInfo stageInfos[2] = {};
	stageInfos[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageInfos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stageInfos[0].module = shaders[0]->module;
	stageInfos[0].pName = "main";
	stageInfos[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageInfos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stageInfos[1].module = shaders[1]->module;
	stageInfos[1].pName = "main";

	inf.stageCount = 2;
	inf.pStages = &stageInfos[0];

	VkPipelineVertexInputStateCreateInfo vertInfo = {};
	if (createInfo->pVertexInputState)
	{
		inf.pVertexInputState = createInfo->pVertexInputState;
	}
	else
	{
		// Leave all zeros
		vertInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		inf.pVertexInputState = &vertInfo;
	}

	VkPipelineInputAssemblyStateCreateInfo assInfo = {};
	if (createInfo->pInputAssemblyState)
	{
		inf.pInputAssemblyState = createInfo->pInputAssemblyState;
	}
	else
	{
		assInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		assInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		inf.pInputAssemblyState = &assInfo;
	}

	inf.pTessellationState = nullptr;

	VkPipelineViewportStateCreateInfo viewportInfo = {};
	if (createInfo->pViewportState)
	{
		inf.pViewportState = createInfo->pViewportState;
	}
	else
	{
		viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportInfo.viewportCount = 1;
		viewportInfo.scissorCount = 1;

		inf.pViewportState = &viewportInfo;
	}

	VkPipelineRasterizationStateCreateInfo rastInfo = {};
	if (createInfo->pRasterizationState)
	{
		inf.pRasterizationState = createInfo->pRasterizationState;
	}
	else
	{
		rastInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rastInfo.depthClampEnable = VK_FALSE;
		rastInfo.rasterizerDiscardEnable = VK_FALSE;
		rastInfo.polygonMode = VK_POLYGON_MODE_FILL;
		rastInfo.cullMode = VK_CULL_MODE_BACK_BIT;
		rastInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rastInfo.depthBiasEnable = VK_FALSE;
		rastInfo.lineWidth = 1.0;

		inf.pRasterizationState = &rastInfo;
	}

	VkPipelineMultisampleStateCreateInfo msInfo = {};
	if (createInfo->pMultisampleState)
	{
		inf.pMultisampleState = createInfo->pMultisampleState;
	}
	else
	{
		msInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		msInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		inf.pMultisampleState = &msInfo;
	}

	VkPipelineDepthStencilStateCreateInfo dsState = {};
	if (createInfo->pDepthStencilState)
	{
		inf.pDepthStencilState = createInfo->pDepthStencilState;
	}
	else
	{
		dsState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		// Leave everything to zero

		inf.pDepthStencilState = &dsState;
	}

	VkPipelineColorBlendStateCreateInfo colorState = {};
	VkPipelineColorBlendAttachmentState colorAtt = {};
	if (createInfo->pColorBlendState)
	{
		inf.pColorBlendState = createInfo->pColorBlendState;
	}
	else
	{
		colorAtt.colorWriteMask = ~0u;

		colorState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorState.attachmentCount = 1;
		colorState.pAttachments = &colorAtt;

		inf.pColorBlendState = &colorState;
	}

	VkPipelineDynamicStateCreateInfo dynState = {};
	static const VkDynamicState DYN[8] = { VK_DYNAMIC_STATE_VIEWPORT,           VK_DYNAMIC_STATE_SCISSOR,
		                                   VK_DYNAMIC_STATE_DEPTH_BIAS,         VK_DYNAMIC_STATE_BLEND_CONSTANTS,
		                                   VK_DYNAMIC_STATE_DEPTH_BOUNDS,       VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
		                                   VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, VK_DYNAMIC_STATE_STENCIL_REFERENCE };

	if (createInfo->pDynamicState)
	{
		inf.pDynamicState = createInfo->pDynamicState;
	}
	else
	{
		dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynState.dynamicStateCount = 8;
		dynState.pDynamicStates = &DYN[0];

		inf.pDynamicState = &dynState;
	}

	if (!createInfo->layout)
	{
		const uint32_t *code[2] = { vertCode, fragCode };
		size_t codeSizes[2] = { vertSize, fragSize };
		initLayouts(code, codeSizes, 2);

		inf.layout = pipelineLayout;
	}

	MPD_ASSERT_RESULT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &inf, nullptr, &pipeline));
}

void Framebuffer::initOnlyColor(const std::shared_ptr<Texture> &colorAttachment, VkAttachmentLoadOp load,
                                VkAttachmentStoreOp store, uint32_t subpassDependencies,
                                const VkSubpassDependency *pDependencies)
{
	colorAttachments[0] = colorAttachment;

	// Create renderpass
	VkAttachmentDescription attDesc = {};
	attDesc.format = colorAttachment->format;
	attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	attDesc.loadOp = load;
	attDesc.storeOp = store;
	attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attDesc.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkAttachmentReference attRef = {};
	attRef.attachment = 0;
	attRef.layout = VK_IMAGE_LAYOUT_GENERAL;

	VkSubpassDescription subpass = {};
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &attRef;

	VkRenderPassCreateInfo rpinf = {
		VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
	};
	rpinf.attachmentCount = 1;
	rpinf.pAttachments = &attDesc;
	rpinf.subpassCount = 1;
	rpinf.pSubpasses = &subpass;
	rpinf.dependencyCount = subpassDependencies;
	rpinf.pDependencies = pDependencies;

	MPD_ASSERT_RESULT(vkCreateRenderPass(device, &rpinf, nullptr, &renderPass));

	// Create framebuffer
	VkFramebufferCreateInfo fbinf = {
		VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
	};
	fbinf.renderPass = renderPass;
	fbinf.attachmentCount = 1;
	fbinf.pAttachments = &colorAttachment->view;
	fbinf.width = colorAttachment->width;
	fbinf.height = colorAttachment->height;
	fbinf.layers = 1;

	MPD_ASSERT_RESULT(vkCreateFramebuffer(device, &fbinf, nullptr, &framebuffer));
}

void Framebuffer::initDepthColor(const std::shared_ptr<Texture> &depthAttachment,
                                 const std::shared_ptr<Texture> &colorAttachment, VkAttachmentLoadOp depthLoad,
                                 VkAttachmentStoreOp depthStore, VkAttachmentLoadOp colorLoad,
                                 VkAttachmentStoreOp colorStore)
{
	colorAttachments[0] = colorAttachment;
	depthStencilAttachment = depthAttachment;

	// Create renderpass
	VkAttachmentDescription attDesc[2] = {};
	attDesc[0].format = colorAttachment->format;
	attDesc[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attDesc[0].loadOp = colorLoad;
	attDesc[0].storeOp = colorStore;
	attDesc[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attDesc[0].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
	attDesc[1].format = depthAttachment->format;
	attDesc[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attDesc[1].loadOp = depthLoad;
	attDesc[1].storeOp = depthStore;
	attDesc[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attDesc[1].finalLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkAttachmentReference attRef[2] = {};
	attRef[0].attachment = 0;
	attRef[0].layout = VK_IMAGE_LAYOUT_GENERAL;
	attRef[1].attachment = 1;
	attRef[1].layout = VK_IMAGE_LAYOUT_GENERAL;

	VkSubpassDescription subpass = {};
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &attRef[0];
	subpass.pDepthStencilAttachment = &attRef[1];

	VkRenderPassCreateInfo rpinf = {
		VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
	};
	rpinf.attachmentCount = 2;
	rpinf.pAttachments = attDesc;
	rpinf.subpassCount = 1;
	rpinf.pSubpasses = &subpass;

	MPD_ASSERT_RESULT(vkCreateRenderPass(device, &rpinf, nullptr, &renderPass));

	VkImageView images[] = {
		colorAttachment->view, depthAttachment->view,
	};

	// Create framebuffer
	VkFramebufferCreateInfo fbinf = {
		VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
	};
	fbinf.renderPass = renderPass;
	fbinf.attachmentCount = 2;
	fbinf.pAttachments = images;
	fbinf.width = colorAttachment->width;
	fbinf.height = colorAttachment->height;
	fbinf.layers = 1;

	MPD_ASSERT_RESULT(vkCreateFramebuffer(device, &fbinf, nullptr, &framebuffer));
}

void Texture::initRenderTarget2D(uint32_t w, uint32_t h, VkFormat fmt, bool transient)
{
	width = w;
	height = h;
	format = fmt;

	// Create image
	VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = fmt;
	info.extent.width = w;
	info.extent.height = h;
	info.extent.depth = 1;
	info.mipLevels = 1;
	info.arrayLayers = 1;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	if (transient)
	{
		info.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
		             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}
	else
	{
		info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		             VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	}
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	MPD_ASSERT_RESULT(vkCreateImage(device, &info, nullptr, &image));

	// Get mem requirements
	VkMemoryRequirements memReqs;
	vkGetImageMemoryRequirements(device, image, &memReqs);

	uint32_t memType = ~0u;
	for (uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++)
	{
		if (memReqs.memoryTypeBits & (1u << i))
		{
			memType = i;
			break;
		}
	}

	// Allocate memory
	VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	alloc.allocationSize = memReqs.size;
	alloc.memoryTypeIndex = memType;
	MPD_ASSERT_RESULT(vkAllocateMemory(device, &alloc, nullptr, &memory));

	// Bind
	vkBindImageMemory(device, image, memory, 0);

	// Create image view
	VkImageViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = fmt;
	viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
	viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
	viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
	viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.layerCount = 1;

	MPD_ASSERT_RESULT(vkCreateImageView(device, &viewInfo, nullptr, &view));
}

void Texture::initDepthStencil(uint32_t w, uint32_t h, VkFormat fmt, bool transient)
{
	width = w;
	height = h;
	format = fmt;

	// Create image
	VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = fmt;
	info.extent.width = w;
	info.extent.height = h;
	info.extent.depth = 1;
	info.mipLevels = 1;
	info.arrayLayers = 1;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	if (transient)
		info.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
		             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	else
		info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	MPD_ASSERT_RESULT(vkCreateImage(device, &info, nullptr, &image));

	// Get mem requirements
	VkMemoryRequirements memReqs;
	vkGetImageMemoryRequirements(device, image, &memReqs);

	uint32_t memType = ~0u;
	for (uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++)
	{
		if (memReqs.memoryTypeBits & (1u << i))
		{
			memType = i;
			break;
		}
	}

	// Allocate memory
	VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	alloc.allocationSize = memReqs.size;
	alloc.memoryTypeIndex = memType;
	MPD_ASSERT_RESULT(vkAllocateMemory(device, &alloc, nullptr, &memory));

	// Bind
	vkBindImageMemory(device, image, memory, 0);

	// Create image view
	VkImageViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = fmt;
	viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
	viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
	viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
	viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.layerCount = 1;

	MPD_ASSERT_RESULT(vkCreateImageView(device, &viewInfo, nullptr, &view));
}

void CommandBuffer::initPrimary()
{
	VkCommandPoolCreateInfo poolinf = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr,
		                                VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, 0 };

	MPD_ASSERT_RESULT(vkCreateCommandPool(device, &poolinf, nullptr, &pool));

	VkCommandBufferAllocateInfo cmdbinf = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
	};

	cmdbinf.commandPool = pool;
	cmdbinf.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmdbinf.commandBufferCount = 1;

	MPD_ASSERT_RESULT(vkAllocateCommandBuffers(device, &cmdbinf, &commandBuffer));
}

void Buffer::init(size_t size, VkBufferUsageFlags usage, const VkPhysicalDeviceMemoryProperties &memProps,
                  HostAccess hostAccess, void *data)
{
	// Create buffer
	VkBufferCreateInfo ci = {
		VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
	};
	ci.size = size;
	ci.usage = usage;
	ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	MPD_ASSERT_RESULT(vkCreateBuffer(device, &ci, nullptr, &buffer));

	// Decide on mem type
	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements(device, buffer, &memReqs);

	VkMemoryPropertyFlags desiredMemProps;
	if (hostAccess == HOST_ACCESS_WRITE)
	{
		desiredMemProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	}
	else if (hostAccess & HOST_ACCESS_READ)
	{
		desiredMemProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
	}
	else
	{
		desiredMemProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	}

	uint32_t memType = ~0u;
	uint32_t fallbackMemType = ~0u;
	for (unsigned i = 0; i < memProps.memoryTypeCount; i++)
	{
		if (memReqs.memoryTypeBits & (1u << i))
		{
			VkMemoryPropertyFlags flags = memProps.memoryTypes[i].propertyFlags;

			if ((flags & desiredMemProps) == desiredMemProps)
			{
				memType = i;
			}

			fallbackMemType = i;
		}
	}

	if (memType == ~0u)
		memType = fallbackMemType;

	MPD_ASSERT(memType != ~0u);

	// Allocate memory
	VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	alloc.allocationSize = memReqs.size;
	alloc.memoryTypeIndex = memType;
	MPD_ASSERT_RESULT(vkAllocateMemory(device, &alloc, nullptr, &memory));

	// Bind
	vkBindBufferMemory(device, buffer, memory, 0);

	// Set the data
	if (data && (hostAccess & HOST_ACCESS_READ_WRITE))
	{
		// Map it to write

		void *ptr = nullptr;
		MPD_ASSERT_RESULT(vkMapMemory(device, memory, 0, memReqs.size, 0, &ptr));

		memcpy(ptr, data, size);

		vkUnmapMemory(device, memory);
	}
	else if (data)
	{
		// Need to upload using a command buffer

		MPD_ASSERT(!"Not implemented ATM");
	}
}
}
