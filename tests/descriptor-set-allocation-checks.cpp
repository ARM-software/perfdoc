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

class DescriptorSetAllocationsTest : public VulkanTestHelper
{
	VkDescriptorPool pool;
	VkDescriptorSetLayout descriptorSetLayout;

	bool initialize()
	{
		if (!VulkanTestHelper::initialize())
			return false;

		// Create pool
		{
			VkDescriptorPoolSize poolSizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 100 } };

			VkDescriptorPoolCreateInfo inf = {};
			inf.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			inf.maxSets = 100;
			inf.poolSizeCount = sizeof(poolSizes) / sizeof(poolSizes[0]);
			inf.pPoolSizes = &poolSizes[0];

			MPD_ASSERT_RESULT(vkCreateDescriptorPool(device, &inf, nullptr, &pool));
		}

		// Create a layout
		{
			VkDescriptorSetLayoutBinding bindings[1] = {};
			bindings[0].binding = 0;
			bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
			bindings[0].descriptorCount = 1;
			bindings[0].stageFlags = ~0u;

			VkDescriptorSetLayoutCreateInfo inf = {};
			inf.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			inf.bindingCount = sizeof(bindings) / sizeof(bindings[0]);
			inf.pBindings = &bindings[0];

			MPD_ASSERT_RESULT(vkCreateDescriptorSetLayout(device, &inf, nullptr, &descriptorSetLayout));
		}

		return true;
	}

	bool runTest()
	{
		if (!testPositive())
			return false;

		if (!testPositiveResetPool())
			return false;

		if (!testNegative())
			return false;

		return true;
	}

	bool testPositive()
	{
		resetCounts();
		VkDescriptorSet set = VK_NULL_HANDLE;

		// Allocate a descriptor set
		VkDescriptorSetAllocateInfo inf = {};
		inf.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		inf.descriptorPool = pool;
		inf.descriptorSetCount = 1;
		inf.pSetLayouts = &descriptorSetLayout;
		MPD_ASSERT_RESULT(vkAllocateDescriptorSets(device, &inf, &set));

		// Free it
		MPD_ASSERT_RESULT(vkFreeDescriptorSets(device, pool, 1, &set));

		// Create again
		MPD_ASSERT_RESULT(vkAllocateDescriptorSets(device, &inf, &set));

		if (getCount(MESSAGE_CODE_DESCRIPTOR_SET_ALLOCATION_CHECKS) != 1)
			return false;

		return true;
	}

	bool testPositiveResetPool()
	{
		resetCounts();
		VkDescriptorSet set = VK_NULL_HANDLE;

		// Allocate a descriptor set
		VkDescriptorSetAllocateInfo inf = {};
		inf.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		inf.descriptorPool = pool;
		inf.descriptorSetCount = 1;
		inf.pSetLayouts = &descriptorSetLayout;
		MPD_ASSERT_RESULT(vkAllocateDescriptorSets(device, &inf, &set));

		// Free it by reseting the pool
		MPD_ASSERT_RESULT(vkResetDescriptorPool(device, pool, 0));

		// Create again
		MPD_ASSERT_RESULT(vkAllocateDescriptorSets(device, &inf, &set));
		if (getCount(MESSAGE_CODE_DESCRIPTOR_SET_ALLOCATION_CHECKS) != 1)
			return false;

		return true;
	}

	bool testNegative()
	{
		resetCounts();
		const unsigned COUNT = 2;
		VkDescriptorSet sets[COUNT] = {};
		VkDescriptorSetLayout layouts[COUNT] = { descriptorSetLayout, descriptorSetLayout };

		// Allocate a descriptor set
		VkDescriptorSetAllocateInfo inf = {};
		inf.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		inf.descriptorPool = pool;
		inf.descriptorSetCount = COUNT;
		inf.pSetLayouts = &layouts[0];
		MPD_ASSERT_RESULT(vkAllocateDescriptorSets(device, &inf, &sets[0]));

		// Free it
		MPD_ASSERT_RESULT(vkFreeDescriptorSets(device, pool, COUNT, &sets[0]));
		if (getCount(MESSAGE_CODE_DESCRIPTOR_SET_ALLOCATION_CHECKS) != 0)
			return false;

		return true;
	}
};

VulkanTestHelper *MPD::createTest()
{
	return new DescriptorSetAllocationsTest;
}
