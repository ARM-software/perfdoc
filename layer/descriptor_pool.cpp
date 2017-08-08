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

#include "descriptor_pool.hpp"
#include "descriptor_set.hpp"
#include "device.hpp"
#include "message_codes.hpp"

namespace MPD
{

DescriptorPool::~DescriptorPool()
{
	baseDevice->freeDescriptorSets(this);
}

void DescriptorPool::descriptorSetCreated(DescriptorSet *dset)
{
	MPD_ASSERT(dset);

	auto it = layoutInfos.find(dset->getLayoutUuid());
	if (it != layoutInfos.end())
	{
		DescriptorSetLayoutInfo &inf = it->second;

		if (inf.descriptorSetsFreedCount > 0)
		{
			inf.descriptorSetsFreedCount = 0;
			log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_DESCRIPTOR_SET_ALLOCATION_CHECKS,
			    "It appears that some redundant descriptor set allocations happened. "
			    "Consider recycling descriptor sets.");
		}
	}
	else
	{
		DescriptorSetLayoutInfo inf = {
			0,
		};
		layoutInfos[dset->getLayoutUuid()] = inf;
	}
}

void DescriptorPool::descriptorSetDeleted(DescriptorSet *dset)
{
	MPD_ASSERT(dset);

	auto it = layoutInfos.find(dset->getLayoutUuid());
	MPD_ASSERT(it != layoutInfos.end());
	++it->second.descriptorSetsFreedCount;
}

void DescriptorPool::reset()
{
	baseDevice->freeDescriptorSets(this);
}
}
