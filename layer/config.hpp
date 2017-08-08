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

#include "perfdoc.hpp"
#include <cstring>
#include <string>
#include <unordered_map>

namespace MPD
{

struct ConfigOptionMapHasher
{
	size_t operator()(const char *name) const
	{
		return std::hash<std::string>()(name);
	}
};

struct ConfigOptionMapCompare
{
	bool operator()(const char *a, const char *b) const
	{
		return strcmp(a, b) == 0;
	}
};

template <typename T>
struct ConfigOption
{
	T *ptrToValue;
	const char *name;
	const char *description;
};

template <typename T>
using ConfigOptionMap =
    std::unordered_map<const char *, ConfigOption<T>, ConfigOptionMapHasher, ConfigOptionMapCompare>;

/// Dummy class that initializes the map with something.
template <typename T>
struct DummyInitializer
{
	DummyInitializer(const char *name, const char *descr, T *ptrToVal, ConfigOptionMap<T> &map)
	{
		map[name] = ConfigOption<T>{ ptrToVal, name, descr };
	}
};

#define MPD_DEFINE_CFG_OPTIONB(name, defaultVal, descr) \
	bool name = defaultVal;                             \
	DummyInitializer<bool> name##d = { #name, descr, &name, bools }

#define MPD_DEFINE_CFG_OPTIONI(name, defaultVal, descr) \
	int64_t name = defaultVal;                          \
	DummyInitializer<int64_t> name##d = { #name, descr, &name, ints }

#define MPD_DEFINE_CFG_OPTIONU(name, defaultVal, descr) \
	uint64_t name = defaultVal;                         \
	DummyInitializer<uint64_t> name##d = { #name, descr, &name, uints }

#define MPD_DEFINE_CFG_OPTIONF(name, defaultVal, descr) \
	double name = defaultVal;                           \
	DummyInitializer<double> name##d = { #name, descr, &name, floats }

#define MPD_DEFINE_CFG_OPTION_STRING(name, defaultVal, descr) \
	std::string name = defaultVal;                            \
	DummyInitializer<std::string> name##d = { #name, descr, &name, strings }

/// A collection of configuration variables.
///
/// The config file has the following format (example):
/// @code
/// # This is a comment
/// maxSmallIndexedDrawcalls 666
///
/// # This is another comment
/// smallIndexedDrawcallIndices 667
/// #endcode
class Config
{
private:
	ConfigOptionMap<bool> bools;
	ConfigOptionMap<int64_t> ints;
	ConfigOptionMap<uint64_t> uints;
	ConfigOptionMap<double> floats;
	ConfigOptionMap<std::string> strings;

public:
	MPD_DEFINE_CFG_OPTIONU(maxSmallIndexedDrawcalls, 10,
	                       "How many small indexed drawcalls in a command buffer before a warning is thrown");

	MPD_DEFINE_CFG_OPTIONU(smallIndexedDrawcallIndices, 10, "How many indices make a small indexed drawcall");

	MPD_DEFINE_CFG_OPTIONU(depthPrePassMinVertices, 500,
	                       "Minimum number of vertices to take into account when doing depth pre-pass checks");
	MPD_DEFINE_CFG_OPTIONU(depthPrePassMinIndices, 500,
	                       "Minimum number of indices to take into account when doing depth pre-pass checks");
	MPD_DEFINE_CFG_OPTIONU(depthPrePassNumDrawCalls, 20,
	                       "Minimum number of drawcalls in order to trigger depth pre-pass");

	MPD_DEFINE_CFG_OPTIONU(minDeviceAllocationSize, 256 * 1024, "Recomended allocation size for vkAllocateMemory");

	MPD_DEFINE_CFG_OPTIONU(
	    minDedicatedAllocationSize, 2 * 1024 * 1024,
	    "If a buffer or image is allocated and it consumes an entire VkDeviceMemory, it should at least be this large. "
	    "This is slightly different from minDeviceAllocationSize since the 256K buffer can still be sensibly "
	    "suballocated from. If we consume an entire allocation with one image or buffer, it should at least be for a "
	    "very large allocation");

	MPD_DEFINE_CFG_OPTIONU(maxEfficientSamples, 4,
	                       "Maximum sample count for full throughput");

	MPD_DEFINE_CFG_OPTIONF(unclampedMaxLod, 32.0f, "The minimum LOD level which is equivalent to unclamped maxLod");

	MPD_DEFINE_CFG_OPTIONU(indexBufferScanMinIndexCount, 128,
	                       "Skip index buffer scanning of drawcalls with less than this limit");

	MPD_DEFINE_CFG_OPTIONF(indexBufferUtilizationThreshold, 0.5,
	                       "Only report indexbuffer fragmentation warning if utilization is below this threshold");

	MPD_DEFINE_CFG_OPTIONF(indexBufferCacheHitThreshold, 0.5,
	                       "Only report cache hit performance warnings if cache hit is below this threshold");

	MPD_DEFINE_CFG_OPTIONU(indexBufferVertexPostTransformCache, 32,
	                       "Size of post-transform cache used for estimating index buffer cache hit-rate");

	MPD_DEFINE_CFG_OPTIONU(maxInstancedVertexBuffers, 1,
	                       "Maximum number of instanced vertex buffers which should be used");

	MPD_DEFINE_CFG_OPTIONU(
	    threadGroupSize, 4,
	    "On Midgard, compute threads are dispatched in groups. On Bifrost, threads run in lock-step.");

	MPD_DEFINE_CFG_OPTIONU(maxEfficientWorkGroupThreads, 64, "Maximum number of threads which can efficiently be part "
	                                                         "of a compute workgroup when using thread group barriers");

	MPD_DEFINE_CFG_OPTIONB(
	    indexBufferScanningEnable, true,
	    "If enabled, scans the index buffer for every draw call in an attempt to find inefficiencies. "
	    "This is fairly expensive, so it should be disabled once index buffers have been validated.");

	MPD_DEFINE_CFG_OPTIONB(
	    indexBufferScanningInPlace, false,
	    "If enabled, scans the index buffer in place on vkCmdDrawIndexed. "
	    "This is useful to narrow down exactly which draw call is causing the issue as you can backtrace the debug "
	    "callback, "
	    "but scanning indices here will only work if the index buffer is actually valid when calling this function. "
	    "If not enabled, indices will be scanned on vkQueueSubmit.");

	MPD_DEFINE_CFG_OPTION_STRING(loggingFilename, "",
	                             "This setting specifies where to log output from the layer.\n"
	                             "# The setting does not impact VK_EXT_debug_report which will always be supported.\n"
	                             "# This filename represents a path on the file system, but special values include:\n"
	                             "#  stdout\n"
	                             "#  stderr\n"
	                             "#  logcat (Android only)\n"
	                             "#  debug_output (OutputDebugString, Windows only).");

	bool tryToLoadFromFile(const std::string &fname);

	void dumpToFile(const std::string &fname) const;
};
}
