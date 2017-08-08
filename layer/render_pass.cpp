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

#include "render_pass.hpp"
#include "device.hpp"
#include "format.hpp"
#include "message_codes.hpp"
#include <algorithm>
#include <iterator>

using namespace std;

namespace MPD
{

void RenderPass::checkMultisampling()
{
	// Check that if we are using multisampled images, they have appropriate load/store ops, i.e.,
	// they can be transient.
	for (uint32_t i = 0; i < createInfo.attachmentCount; i++)
	{
		auto &attachment = createInfo.pAttachments[i];
		if (attachment.samples == VK_SAMPLE_COUNT_1_BIT)
			continue;

		bool accessRequiresMemory = attachment.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD;
		accessRequiresMemory |= attachment.storeOp == VK_ATTACHMENT_STORE_OP_STORE;
		if (formatIsStencilOnly(attachment.format) || formatIsDepthStencil(attachment.format))
		{
			accessRequiresMemory |= attachment.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD;
			accessRequiresMemory |= attachment.stencilStoreOp == VK_ATTACHMENT_STORE_OP_STORE;
		}

		if (accessRequiresMemory)
		{
			log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_MULTISAMPLED_IMAGE_REQUIRES_MEMORY,
			    "Attachment %u in the VkRenderPass is a multisampled image with %u samples, but it uses loadOp/storeOp "
			    "which "
			    "require accessing data from memory. Multisampled images should always be loadOp = CLEAR or DONT_CARE, "
			    "storeOp = DONT_CARE. "
			    "This allows the implementation to use lazily allocated memory effectively.",
			    i, static_cast<uint32_t>(attachment.samples));
		}
	}
}

bool RenderPass::renderPassUsesAttachmentAsImageOnly(uint32_t attachment) const
{
	if (renderPassUsesAttachmentOnTile(attachment))
		return false;

	for (uint32_t subpass = 0; subpass < createInfo.subpassCount; subpass++)
	{
		auto &subpassInfo = createInfo.pSubpasses[subpass];

		for (uint32_t i = 0; i < subpassInfo.inputAttachmentCount; i++)
			if (subpassInfo.pInputAttachments[i].attachment == attachment)
				return true;
	}

	return false;
}

bool RenderPass::renderPassUsesAttachmentOnTile(uint32_t attachment) const
{
	for (uint32_t subpass = 0; subpass < createInfo.subpassCount; subpass++)
	{
		auto &subpassInfo = createInfo.pSubpasses[subpass];

		// If an attachment is ever used as a color attachment,
		// resolve attachment or depth stencil attachment,
		// it needs to exist on tile at some point.

		for (uint32_t i = 0; i < subpassInfo.colorAttachmentCount; i++)
			if (subpassInfo.pColorAttachments[i].attachment == attachment)
				return true;

		if (subpassInfo.pResolveAttachments)
		{
			for (uint32_t i = 0; i < subpassInfo.colorAttachmentCount; i++)
				if (subpassInfo.pResolveAttachments[i].attachment == attachment)
					return true;
		}

		if (subpassInfo.pDepthStencilAttachment && subpassInfo.pDepthStencilAttachment->attachment == attachment)
			return true;
	}

	return false;
}

VkResult RenderPass::init(VkRenderPass renderPass_, const VkRenderPassCreateInfo &createInfo_)
{
	renderPass = renderPass_;
	createInfo = createInfo_;

	// Important to resize here ahead of time, so we don't invalidate pointers while copy the create info structures.
	subpasses.resize(createInfo.subpassCount);
	subpassDescriptions.reserve(createInfo.subpassCount);
	for (uint32_t i = 0; i < createInfo.subpassCount; i++)
	{
		auto &subpass = subpasses[i];
		auto &desc = createInfo.pSubpasses[i];
		// Redirect pointers to temporary data to our own data structures.
		VkSubpassDescription copyDesc = desc;

		if (desc.colorAttachmentCount)
		{
			copy(desc.pColorAttachments, desc.pColorAttachments + desc.colorAttachmentCount,
			     back_inserter(subpass.colorAttachments));
			copyDesc.pColorAttachments = subpass.colorAttachments.data();
		}

		if (desc.inputAttachmentCount)
		{
			copy(desc.pInputAttachments, desc.pInputAttachments + desc.inputAttachmentCount,
			     back_inserter(subpass.inputAttachments));
			copyDesc.pInputAttachments = subpass.inputAttachments.data();
		}

		if (desc.preserveAttachmentCount)
		{
			copy(desc.pPreserveAttachments, desc.pPreserveAttachments + desc.preserveAttachmentCount,
			     back_inserter(subpass.preserveAttachments));
			copyDesc.pPreserveAttachments = subpass.preserveAttachments.data();
		}

		if (desc.colorAttachmentCount && desc.pResolveAttachments)
		{
			copy(desc.pResolveAttachments, desc.pResolveAttachments + desc.colorAttachmentCount,
			     back_inserter(subpass.resolveAttachments));
			copyDesc.pResolveAttachments = subpass.resolveAttachments.data();
		}

		subpass.depthStencilAttachment = desc.pDepthStencilAttachment ?
		                                     *desc.pDepthStencilAttachment :
		                                     VkAttachmentReference{ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED };
		if (copyDesc.pDepthStencilAttachment)
			copyDesc.pDepthStencilAttachment = &subpass.depthStencilAttachment;

		subpassDescriptions.push_back(copyDesc);
	};

	// Redirect pointers to temporary data to our own allocated data structures.
	if (createInfo.dependencyCount)
	{
		copy(createInfo.pDependencies, createInfo.pDependencies + createInfo.dependencyCount,
		     back_inserter(subpassDependencies));
		createInfo.pDependencies = subpassDependencies.data();
	}

	if (createInfo.attachmentCount)
	{
		copy(createInfo.pAttachments, createInfo.pAttachments + createInfo.attachmentCount, back_inserter(attachments));
		createInfo.pAttachments = attachments.data();
	}

	if (createInfo.subpassCount)
		createInfo.pSubpasses = subpassDescriptions.data();

	checkMultisampling();

	return VK_SUCCESS;
}
}
