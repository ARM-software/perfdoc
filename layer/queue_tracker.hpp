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
#include "dispatch_helper.hpp"
#include "perfdoc.hpp"

namespace MPD
{
class Queue;
class Event;
class QueueTracker
{
public:
	QueueTracker(Queue &queue);

	enum StageFlagBits
	{
		STAGE_COMPUTE_BIT = 1 << 0,
		STAGE_GEOMETRY_BIT = 1 << 1,
		STAGE_FRAGMENT_BIT = 1 << 2,
		STAGE_TRANSFER_BIT = 1 << 3,
		STAGE_ALL_BITS = STAGE_COMPUTE_BIT | STAGE_GEOMETRY_BIT | STAGE_FRAGMENT_BIT | STAGE_TRANSFER_BIT
	};
	using StageFlags = uint32_t;

	enum Stage
	{
		STAGE_COMPUTE = 0,
		STAGE_GEOMETRY = 1,
		STAGE_FRAGMENT = 2,
		STAGE_TRANSFER = 3,
		STAGE_COUNT
	};

	void pushWork(Stage stage);
	void pipelineBarrier(StageFlags srcStages, StageFlags dstStages);
	void waitEvent(const Event &event, StageFlags dstStages);
	void signalEvent(Event &event, StageFlags srcStages);

	Queue &getQueue()
	{
		return queue;
	}

private:
	Queue &queue;

	struct StageStatus
	{
		// Waits for work associated with an index to complete in other stages.
		uint64_t waitList[STAGE_COUNT] = {};

		// The number of work items pushed to this pipeline stage so far.
		uint64_t index = 0;

		// The index when this stage was last used as a dstStageMask.
		uint64_t lastDstStageIndex[STAGE_COUNT] = {};
	};
	StageStatus stages[STAGE_COUNT];

	void barrier(StageFlags srcStages, Stage dstStage);
};
}