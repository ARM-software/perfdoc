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
#include "pipeline_layout.hpp"

#include <vector>

namespace MPD
{
class Pipeline : public BaseObject
{
public:
	using VulkanType = VkPipeline;
	static const VkDebugReportObjectTypeEXT VULKAN_OBJECT_TYPE = VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT;

	Pipeline(Device *device_, uint64_t objHandle_)
	    : BaseObject(device_, objHandle_, VULKAN_OBJECT_TYPE)
	{
	}

	enum class Type
	{
		Compute,
		Graphics
	};

	Type getPipelineType() const
	{
		return type;
	}

	VkResult initGraphics(VkPipeline pipeline, const VkGraphicsPipelineCreateInfo &createInfo);
	VkResult initCompute(VkPipeline pipeline, const VkComputePipelineCreateInfo &createInfo);

	const VkGraphicsPipelineCreateInfo &getGraphicsCreateInfo() const
	{
		MPD_ASSERT(type == Type::Graphics);
		return createInfo.graphics;
	}

	const VkComputePipelineCreateInfo &getComputeCreateInfo() const
	{
		MPD_ASSERT(type == Type::Compute);
		return createInfo.compute;
	}

	const PipelineLayout *getPipelineLayout() const
	{
		return layout;
	}

private:
	VkPipeline pipeline = VK_NULL_HANDLE;
	const PipelineLayout *layout = nullptr;
	union {
		VkGraphicsPipelineCreateInfo graphics;
		VkComputePipelineCreateInfo compute;
	} createInfo;

	VkPipelineDepthStencilStateCreateInfo depthStencilState;
	VkPipelineColorBlendStateCreateInfo colorBlendState;
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState;
	std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachmentState;

	void checkInstancedVertexBuffer(const VkGraphicsPipelineCreateInfo &createInfo);
	void checkMultisampledBlending(const VkGraphicsPipelineCreateInfo &createInfo);

	Type type;
	void checkWorkGroupSize(const VkComputePipelineCreateInfo &createInfo);
	void checkPushConstantsForStage(const VkPipelineShaderStageCreateInfo &stage);
};
}
