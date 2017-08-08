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

#include "queue_tracker.hpp"
#include "event.hpp"
#include "message_codes.hpp"
#include "queue.hpp"
#include <algorithm>

using namespace std;

namespace MPD
{
QueueTracker::QueueTracker(Queue &queue)
    : queue(queue)
{
}

void QueueTracker::pushWork(Stage dstStage)
{
	static const char *stageNames[STAGE_COUNT] = {
		"COMPUTE", "GEOMETRY", "FRAGMENT", "TRANSFER",
	};

	for (unsigned i = 0; i < STAGE_COUNT; i++)
	{
		if (dstStage == i)
			continue;

		// If we're waiting for all submitted work for a stage, there might be a bubble.
		if (stages[dstStage].waitList[i] != stages[i].index)
			continue;

		// If no work has been submitted to this stage yet, there's nothing which can cause bubbles.
		if (!stages[dstStage].index)
			continue;

		// GEOMETRY and COMPUTE do not run concurrently in Vulkan, so bubbles between them don't matter.
		if (((1 << i) | (1 << dstStage)) == (STAGE_GEOMETRY_BIT | STAGE_COMPUTE_BIT))
			continue;

		// If the stage we depend on depends on the last work we submitted to this stage (a cycle), we have a bubble, because
		// our stage must go idle before we can begin executing the dependency we're waiting on here.
		// Only consider this is a bubble if work has been submitted to the stage which might cause our bubble.

		// For example:
		// FRAGMENT -> TRANSFER barrier.
		// TRANSFER -> FRAGMENT barrier,
		// is effectively just a FRAGMENT -> FRAGMENT barrier,
		// no bubble.

		// FRAGMENT -> TRANSFER barrier (lastDstStageIndex is latched here),
		// TRANSFER work,
		// TRANSFER -> FRAGMENT,
		// is a bubble.
		if (stages[i].waitList[dstStage] == stages[dstStage].index &&
		    stages[i].index != stages[i].lastDstStageIndex[dstStage])
		{
			queue.log(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT, MESSAGE_CODE_PIPELINE_BUBBLE,
			          "Pipeline bubble detected in stage %s. Work in stage %s will block execution in stage %s.",
			          stageNames[dstStage], stageNames[i], stageNames[dstStage]);
		}
	}

	stages[dstStage].index++;
}

void QueueTracker::pipelineBarrier(StageFlags srcStages, StageFlags dstStages)
{
	for (unsigned i = 0; i < STAGE_COUNT; i++)
	{
		if (!(dstStages & (1u << i)))
			continue;

		barrier(srcStages, static_cast<Stage>(i));
	}
}

void QueueTracker::barrier(StageFlags srcStages, Stage dstStage)
{
	if (srcStages == 0)
		return;

	for (unsigned srcStage = 0; srcStage < STAGE_COUNT; srcStage++)
	{
		if (!(srcStages & (1u << srcStage)))
			continue;

		// If we're waiting for new work from a stage, store the current work index for our stage.
		// This way, we can track if the dependency ends up purely transitive or if it's a true bubble.
		if (stages[srcStage].index > stages[dstStage].waitList[srcStage])
		{
			stages[dstStage].waitList[srcStage] = stages[srcStage].index;
			stages[dstStage].lastDstStageIndex[srcStage] = stages[dstStage].index;
		}

		// Inherit dependencies from our srcStages.
		for (unsigned stage = 0; stage < STAGE_COUNT; stage++)
		{
			// If we're waiting for new work from a stage, store the current work index for our stage.
			// This way, we can track if the dependency ends up purely transitive or if it's a true bubble.
			if (stages[srcStage].waitList[stage] > stages[dstStage].waitList[stage])
			{
				stages[dstStage].waitList[stage] = stages[srcStage].waitList[stage];
				stages[dstStage].lastDstStageIndex[stage] = stages[dstStage].index;
			}
		}
	}
}

void QueueTracker::signalEvent(Event &event, StageFlags srcStages)
{
	// Already signalled.
	if (event.getSignalStatus())
		return;

	auto *waitList = event.getWaitList();
	memset(waitList, 0, sizeof(QueueTracker::STAGE_COUNT) * sizeof(*waitList));

	for (unsigned srcStage = 0; srcStage < STAGE_COUNT; srcStage++)
	{
		if (!(srcStages & (1u << srcStage)))
			continue;

		waitList[srcStage] = std::max(stages[srcStage].index, waitList[srcStage]);

		// Inherit dependencies from our srcStages.
		for (unsigned stage = 0; stage < STAGE_COUNT; stage++)
			waitList[srcStage] = std::max(stages[srcStage].waitList[stage], waitList[srcStage]);
	}

	// No need to know which queue signalled the event, events can only be used within a single queue.
	event.signal();
}

void QueueTracker::waitEvent(const Event &event, StageFlags dstStages)
{
	// Event must have been signalled here.
	// If not, assume this is a host-signalled event, which should not be counted.
	if (!event.getSignalStatus())
		return;

	const auto *waitList = event.getWaitList();

	for (unsigned dstStage = 0; dstStage < STAGE_COUNT; dstStage++)
	{
		if (!(dstStages & (1u << dstStage)))
			continue;

		// Inherit dependencies from our events.
		for (unsigned stage = 0; stage < STAGE_COUNT; stage++)
		{
			// If we're waiting for new work from a stage, store the current work index for our stage.
			// This way, we can track if the dependency ends up purely transitive or if it's a true bubble.
			if (waitList[stage] > stages[dstStage].waitList[stage])
			{
				stages[dstStage].waitList[stage] = waitList[stage];
				stages[dstStage].lastDstStageIndex[stage] = stages[dstStage].index;
			}
		}
	}
}
}