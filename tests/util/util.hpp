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
#include "layer/config.hpp"
#include "layer/message_codes.hpp"
#include <array>
#include <memory>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace MPD
{
static inline uint32_t ctz(uint32_t v)
{
#ifdef _MSC_VER
	unsigned long result;
	if (_BitScanForward(&result, v))
		return result;
	else
		return 32;
#else
	return v ? __builtin_ctz(v) : 32;
#endif
}

struct TestObjectBase
{
	VkDevice device;

	TestObjectBase(VkDevice device_)
	    : device(device_)
	{
	}

	virtual ~TestObjectBase()
	{
	}
};

/// Shader module wrapper.
struct Shader : TestObjectBase
{
	VkShaderModule module = VK_NULL_HANDLE;

	Shader(VkDevice device)
	    : TestObjectBase(device)
	{
	}

	~Shader()
	{
		if (module)
			vkDestroyShaderModule(device, module, nullptr);
	}

	void init(const uint32_t *code, size_t size);
};

/// Pipeline wrapper.
struct Pipeline : TestObjectBase
{
	VkPipeline pipeline = VK_NULL_HANDLE;
	std::array<std::shared_ptr<Shader>, 5> shaders;

	VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

	Pipeline(VkDevice device)
	    : TestObjectBase(device)
	{
	}

	~Pipeline();

	void initCompute(const uint32_t *code, size_t size);

	// For those state structures are nullptr in createInfo this function will use some default state.
	void initGraphics(const uint32_t *vertCode, size_t vertSize, const uint32_t *fragCode, size_t fragSize,
	                  const VkGraphicsPipelineCreateInfo *createInfo);

private:
	void initLayouts(const uint32_t **codes, size_t *sizes, unsigned shaderCount);
};

/// Image and image view wrapper.
struct Texture : TestObjectBase
{
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	uint32_t width = 0, height = 0, depth = 0;
	VkFormat format = VK_FORMAT_MAX_ENUM;

	Texture(VkDevice device)
	    : TestObjectBase(device)
	{
	}

	~Texture()
	{
		if (view)
			vkDestroyImageView(device, view, nullptr);
		if (image)
			vkDestroyImage(device, image, nullptr);
		if (memory)
			vkFreeMemory(device, memory, nullptr);
	}

	/// Create a single level 2D texture that can be used as framebuffer attachment.
	void initRenderTarget2D(uint32_t w, uint32_t h, VkFormat fmt, bool transient = false);
	void initDepthStencil(uint32_t w, uint32_t h, VkFormat fmt, bool transient = false);
};

/// Framebuffer + renderpass wrapper.
struct Framebuffer : TestObjectBase
{
	VkRenderPass renderPass;
	VkFramebuffer framebuffer;
	std::array<std::shared_ptr<Texture>, 4> colorAttachments;
	std::shared_ptr<Texture> depthStencilAttachment;

	Framebuffer(VkDevice device)
	    : TestObjectBase(device)
	{
	}

	~Framebuffer()
	{
		if (framebuffer)
			vkDestroyFramebuffer(device, framebuffer, nullptr);
		if (renderPass)
			vkDestroyRenderPass(device, renderPass, nullptr);
	}

	void initOnlyColor(const std::shared_ptr<Texture> &colorAttachment,
	                   VkAttachmentLoadOp load = VK_ATTACHMENT_LOAD_OP_CLEAR,
	                   VkAttachmentStoreOp store = VK_ATTACHMENT_STORE_OP_STORE, uint32_t dependencyCount = 0,
	                   const VkSubpassDependency *pDependencies = nullptr);

	void initDepthColor(const std::shared_ptr<Texture> &depthAttachment,
	                    const std::shared_ptr<Texture> &colorAttachment,
	                    VkAttachmentLoadOp depthLoad = VK_ATTACHMENT_LOAD_OP_CLEAR,
	                    VkAttachmentStoreOp depthStore = VK_ATTACHMENT_STORE_OP_STORE,
	                    VkAttachmentLoadOp colorLoad = VK_ATTACHMENT_LOAD_OP_CLEAR,
	                    VkAttachmentStoreOp colorStore = VK_ATTACHMENT_STORE_OP_STORE);
};

/// Commandbuffer + command pool wrapper.
struct CommandBuffer : TestObjectBase
{
	VkCommandPool pool = VK_NULL_HANDLE;
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

	CommandBuffer(VkDevice device)
	    : TestObjectBase(device)
	{
	}

	~CommandBuffer()
	{
		if (pool)
			vkDestroyCommandPool(device, pool, nullptr);
	}

	void initPrimary();
};

enum HostAccess
{
	HOST_ACCESS_NONE = 0,
	HOST_ACCESS_READ = 1,
	HOST_ACCESS_WRITE = 2,
	HOST_ACCESS_READ_WRITE = HOST_ACCESS_READ | HOST_ACCESS_WRITE,
};

/// Buffer wrapper.
struct Buffer : TestObjectBase
{
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkBuffer buffer = VK_NULL_HANDLE;

	Buffer(VkDevice device)
	    : TestObjectBase(device)
	{
	}

	~Buffer()
	{
		if (buffer)
			vkDestroyBuffer(device, buffer, nullptr);
		if (memory)
			vkFreeMemory(device, memory, nullptr);
	}

	void init(size_t size, VkBufferUsageFlags usage, const VkPhysicalDeviceMemoryProperties &memProps,
	          HostAccess hostAccess = HOST_ACCESS_NONE, void *data = nullptr);
};
}
