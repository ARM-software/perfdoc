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

#include "pipeline.hpp"
#include "device.hpp"
#include "format.hpp"
#include "message_codes.hpp"
#include "render_pass.hpp"
#include "shader_module.hpp"
#include "spirv_cross.hpp"
#include <algorithm>

using namespace spirv_cross;
using namespace std;

namespace MPD
{

void Pipeline::checkWorkGroupSize(const VkComputePipelineCreateInfo &createInfo)
{
	auto *module = baseDevice->get<ShaderModule>(createInfo.stage.module);

	try
	{
		Compiler comp(module->getCode());
		comp.set_entry_point(createInfo.stage.pName);

		// Get the workgroup size.
		uint32_t x = comp.get_execution_mode_argument(spv::ExecutionModeLocalSize, 0);
		uint32_t y = comp.get_execution_mode_argument(spv::ExecutionModeLocalSize, 1);
		uint32_t z = comp.get_execution_mode_argument(spv::ExecutionModeLocalSize, 2);
		MPD_ASSERT(x > 0);
		MPD_ASSERT(y > 0);
		MPD_ASSERT(z > 0);

		uint32_t numThreads = x * y * z;

		const uint32_t quadSize = baseDevice->getConfig().threadGroupSize;
		if (numThreads == 1 || ((x > 1) && (x & (quadSize - 1))) || ((y > 1) && (y & (quadSize - 1))) ||
		    ((z > 1) && (z & (quadSize - 1))))
		{
			log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_COMPUTE_NO_THREAD_GROUP_ALIGNMENT,
			    "The work group size (%u, %u, %u) has dimensions which are not aligned to %u threads. "
			    "Not aligning work group sizes to %u may leave threads idle on the shader core.",
			    x, y, z, quadSize, quadSize);
		}

		if ((x * y * z) > baseDevice->getConfig().maxEfficientWorkGroupThreads)
		{
			log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_COMPUTE_LARGE_WORK_GROUP,
			    "The work group size (%u, %u, %u) (%u threads) has more threads than advised. "
			    "It is advised to not use more than %u threads per work group, especially when using barrier() and/or "
			    "shared memory.",
			    x, y, z, x * y * z, baseDevice->getConfig().maxEfficientWorkGroupThreads);
		}

		// Make some basic advice about compute work group sizes based on active resource types.
		auto activeVariables = comp.get_active_interface_variables();
		auto resources = comp.get_shader_resources(activeVariables);

		unsigned dimensions = 0;
		if (x > 1)
			dimensions++;
		if (y > 1)
			dimensions++;
		if (z > 1)
			dimensions++;
		// Here the dimension will really depend on the dispatch grid, but assume it's 1D.
		dimensions = max(dimensions, 1u);

		// If we're accessing images, we almost certainly want to have a 2D workgroup for cache reasons.
		// There are some false positives here. We could simply have a shader that does this within a 1D grid,
		// or we may have a linearly tiled image, but these cases are quite unlikely in practice.
		bool accesses_2d = false;
		const auto check_image = [&](const Resource &resource) {
			auto &type = comp.get_type(resource.base_type_id);
			switch (type.image.dim)
			{
			// These are 1D, so don't count these images.
			case spv::Dim1D:
			case spv::DimBuffer:
				break;

			default:
				accesses_2d = true;
				break;
			}
		};
		for (auto &image : resources.storage_images)
			check_image(image);
		for (auto &image : resources.sampled_images)
			check_image(image);
		for (auto &image : resources.separate_images)
			check_image(image);

		if (accesses_2d && dimensions < 2)
		{
			log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_COMPUTE_POOR_SPATIAL_LOCALITY,
			    "The compute shader has a work group size of (%u, %u, %u), which suggests a 1D dispatch, "
			    "but the shader is accessing 2D or 3D images. There might be poor spatial locality in this shader.",
			    x, y, z);
		}
	}
	catch (const CompilerError &error)
	{
		log(VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
		    "SPIRV-Cross failed to analyze shader: %s. No checks for this pipeline will be performed.", error.what());
	}
}

static bool accessChainIsStaticallyAddressable(const Compiler &comp, const SPIRType &type)
{
	// For any non-struct type, if there are no arrays, there is no access chain except for OpVectorExtractDynamic or similar
	// which is fine, Vulkan spec only prohibits divergent array accesses into push constant space.
	if (type.basetype != SPIRType::Struct)
		return type.array.empty();

	// For structs, recurse through our members.
	for (auto &memb : type.member_types)
		if (!accessChainIsStaticallyAddressable(comp, comp.get_type(memb)))
			return false;
	return true;
}

void Pipeline::checkPushConstantsForStage(const VkPipelineShaderStageCreateInfo &stage)
{
	auto *module = baseDevice->get<ShaderModule>(stage.module);

	try
	{
		Compiler comp(module->getCode());
		comp.set_entry_point(stage.pName);

		// Heuristic:
		// If a shader accesses at least one uniform buffer on a member which is not an array type and
		// The shader does not use any push constant blocks, suggest that the shader could use push constants.
		// Arrays are not considered as they are generally needed for any kind of instancing/batching,
		// and push constants aren't possible there.
		auto activeVariables = comp.get_active_interface_variables();
		auto resources = comp.get_shader_resources(activeVariables);

		// If we have a push constant block, nothing to warn about.
		if (!resources.push_constant_buffers.empty())
			return;

		struct PotentialPushConstant
		{
			string blockName;
			string memberName;
			uint32_t uboID;
			uint32_t index;
			size_t offset;
			size_t range;
		};
		vector<PotentialPushConstant> potentials;
		uint32_t totalPushConstantSize = 0;

		// See if we find any access to UBO members which are not arrayed.
		for (auto &ubo : resources.uniform_buffers)
		{
			auto &type = comp.get_type(ubo.type_id);

			// Array of UBOs, not a push constant candidate.
			if (!type.array.empty())
				continue;

			// Type of the basic struct.
			auto &baseType = comp.get_type(ubo.base_type_id);

			auto ranges = comp.get_active_buffer_ranges(ubo.id);
			for (auto &range : ranges)
			{
				auto &memberType = comp.get_type(baseType.member_types[range.index]);

				// If a nested variant of this type can be statically addressed, (no dynamic accesses anywhere),
				// this is a push constant candidate.
				if (accessChainIsStaticallyAddressable(comp, memberType))
				{
					auto &blockName = ubo.name;
					auto &memberName = comp.get_member_name(ubo.base_type_id, range.index);

					potentials.push_back({ blockName.empty() ? "<stripped>" : blockName,
					                       memberName.empty() ? "<stripped>" : memberName, ubo.id, range.index,
					                       range.offset, range.range });
					totalPushConstantSize += range.range;
				}
			}
		}

		for (auto &potential : potentials)
		{
			module->log(
			    VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_POTENTIAL_PUSH_CONSTANT,
			    "Identified static access to a UBO block (%s, ID: %u) member (%s, index: %u, offset: %u, range: %u). "
			    "This data should be considered for a push constant block which would enable more efficient access to "
			    "this data.",
			    potential.blockName.c_str(), potential.uboID, potential.memberName.c_str(), potential.index,
			    potential.offset, potential.range);
		}

		if (totalPushConstantSize)
		{
			module->log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_POTENTIAL_PUSH_CONSTANT,
			            "Identified a total of %u bytes of UBO data which could potentially be push constant.",
			            totalPushConstantSize);
		}
	}
	catch (const CompilerError &error)
	{
		log(VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
		    "SPIRV-Cross failed to analyze shader: %s. No checks for this pipeline will be performed.", error.what());
	}
}

VkResult Pipeline::initCompute(VkPipeline pipeline_, const VkComputePipelineCreateInfo &createInfo)
{
	pipeline = pipeline_;
	type = Type::Compute;

	layout = baseDevice->get<PipelineLayout>(createInfo.layout);

	checkWorkGroupSize(createInfo);
	checkPushConstantsForStage(createInfo.stage);
	return VK_SUCCESS;
}

void Pipeline::checkInstancedVertexBuffer(const VkGraphicsPipelineCreateInfo &createInfo)
{
	auto &vertexInput = *createInfo.pVertexInputState;
	uint32_t count = 0;
	for (uint32_t i = 0; i < vertexInput.vertexBindingDescriptionCount; i++)
		if (vertexInput.pVertexBindingDescriptions[i].inputRate == VK_VERTEX_INPUT_RATE_INSTANCE)
			count++;

	if (count > baseDevice->getConfig().maxInstancedVertexBuffers)
	{
		log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_TOO_MANY_INSTANCED_VERTEX_BUFFERS,
		    "The pipeline is using %u instanced vertex buffers (current limit: %u), but this can be inefficient on the "
		    "GPU. "
		    "If using instanced vertex attributes prefer interleaving them in a single buffer.",
		    count, baseDevice->getConfig().maxInstancedVertexBuffers);
	}
}

void Pipeline::checkMultisampledBlending(const VkGraphicsPipelineCreateInfo &createInfo)
{
	if (!createInfo.pColorBlendState || !createInfo.pMultisampleState)
		return;

	if (createInfo.pMultisampleState->rasterizationSamples == VK_SAMPLE_COUNT_1_BIT)
		return;

	// For per-sample shading, we don't expect 1x shading rate anyways, so per-sample
	// blending is not really a problem.
	if (createInfo.pMultisampleState->sampleShadingEnable)
		return;

	auto *renderPass = baseDevice->get<RenderPass>(createInfo.renderPass);
	auto &info = renderPass->getCreateInfo();

	MPD_ASSERT(createInfo.subpass < info.subpassCount);
	auto &subpass = info.pSubpasses[createInfo.subpass];

	for (uint32_t i = 0; i < createInfo.pColorBlendState->attachmentCount; i++)
	{
		auto &att = createInfo.pColorBlendState->pAttachments[i];
		MPD_ASSERT(i < subpass.colorAttachmentCount);
		uint32_t attachment = subpass.pColorAttachments[i].attachment;
		if (attachment != VK_ATTACHMENT_UNUSED && att.blendEnable && att.colorWriteMask)
		{
			MPD_ASSERT(attachment < info.attachmentCount);
			if (!formatHasFullThroughputBlending(info.pAttachments[attachment].format))
			{
				log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_NOT_FULL_THROUGHPUT_BLENDING,
				    "Pipeline is multisampled and color attachment #%u makes use of a format which cannot be blended "
				    "at full throughput "
				    "when using MSAA.",
				    i);
			}
		}
	}
}

VkResult Pipeline::initGraphics(VkPipeline pipeline_, const VkGraphicsPipelineCreateInfo &createInfo)
{
	pipeline = pipeline_;
	type = Type::Graphics;

	layout = baseDevice->get<PipelineLayout>(createInfo.layout);

	this->createInfo.graphics = createInfo;

	depthStencilState = *createInfo.pDepthStencilState;
	inputAssemblyState = *createInfo.pInputAssemblyState;
	this->createInfo.graphics.pDepthStencilState = &depthStencilState;
	this->createInfo.graphics.pInputAssemblyState = &inputAssemblyState;

	if (createInfo.pColorBlendState)
	{
		colorBlendState = *createInfo.pColorBlendState;
		colorBlendAttachmentState.clear();
		for (uint32_t i = 0; i < colorBlendState.attachmentCount; i++)
			colorBlendAttachmentState.push_back(createInfo.pColorBlendState->pAttachments[i]);

		if (colorBlendAttachmentState.empty())
			colorBlendState.pAttachments = nullptr;
		else
			colorBlendState.pAttachments = colorBlendAttachmentState.data();

		this->createInfo.graphics.pColorBlendState = &colorBlendState;
	}

	checkInstancedVertexBuffer(createInfo);
	checkMultisampledBlending(createInfo);
	for (uint32_t i = 0; i < createInfo.stageCount; i++)
		checkPushConstantsForStage(createInfo.pStages[i]);
	return VK_SUCCESS;
}
}
