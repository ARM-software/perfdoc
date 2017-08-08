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

#include "framebuffer.hpp"
#include "device.hpp"
#include "format.hpp"
#include "image.hpp"
#include "message_codes.hpp"
#include "render_pass.hpp"
#include <algorithm>
#include <iterator>

namespace MPD
{
VkResult Framebuffer::init(VkFramebuffer framebuffer_, const VkFramebufferCreateInfo &createInfo_)
{
	framebuffer = framebuffer_;
	createInfo = createInfo_;

	if (createInfo.attachmentCount)
	{
		copy(createInfo.pAttachments, createInfo.pAttachments + createInfo.attachmentCount, back_inserter(imageViews));
		createInfo.pAttachments = imageViews.empty() ? nullptr : imageViews.data();
	}

	checkPotentiallyTransient();

	return VK_SUCCESS;
}

void Framebuffer::checkPotentiallyTransient()
{
	auto *renderPass = baseDevice->get<RenderPass>(createInfo.renderPass);
	for (uint32_t i = 0; i < createInfo.attachmentCount; i++)
	{
		if (createInfo.pAttachments[i] == VK_NULL_HANDLE)
			continue;
		if (i >= renderPass->getCreateInfo().attachmentCount)
			continue;

		auto *view = baseDevice->get<ImageView>(createInfo.pAttachments[i]);
		MPD_ASSERT(view);
		MPD_ASSERT(view->getCreateInfo().image);
		auto *baseImage = baseDevice->get<Image>(view->getCreateInfo().image);
		MPD_ASSERT(baseImage);
		bool imageIsTransient = (baseImage->getCreateInfo().usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) != 0;

		auto &attachment = renderPass->getCreateInfo().pAttachments[i];
		bool attachmentShouldBeTransient =
		    attachment.loadOp != VK_ATTACHMENT_LOAD_OP_LOAD && attachment.storeOp != VK_ATTACHMENT_STORE_OP_STORE;
		if (formatIsStencilOnly(attachment.format) || formatIsDepthStencil(attachment.format))
			attachmentShouldBeTransient &= attachment.stencilLoadOp != VK_ATTACHMENT_LOAD_OP_LOAD &&
			                               attachment.stencilStoreOp != VK_ATTACHMENT_STORE_OP_STORE;

		if (attachmentShouldBeTransient && !imageIsTransient)
		{
			log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_FRAMEBUFFER_ATTACHMENT_SHOULD_BE_TRANSIENT,
			    "Attachment %u in VkFramebuffer uses loadOp/storeOps which never have to be backed by physical memory, "
			    "but the image backing the image view does not have VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT set. "
			    "You can save physical memory by using transient attachment backed by lazily allocated memory here.",
			    i);
		}
		else if (!attachmentShouldBeTransient && imageIsTransient)
		{
			log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT,
			    MESSAGE_CODE_FRAMEBUFFER_ATTACHMENT_SHOULD_NOT_BE_TRANSIENT,
			    "Attachment %u in VkFramebuffer uses loadOp/storeOps which need to access physical memory, "
			    "but the image backing the image view has VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT set. "
			    "Physical memory will need to be backed lazily to this image, potentially causing stalls.",
			    i);
		}
	}
}
}
