# Copyright (c) 2017, ARM Limited and Contributors
#
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge,
# to any person obtaining a copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation the rights to
# use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
# and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
# WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

if (WIN32)
	set(export-file exports.def)
else()
	set(export-file )
endif()

add_library(VkLayer_mali_perf_doc SHARED
		logger.cpp
		config.cpp
		base_object.cpp
		dispatch.cpp
		dispatch_helper.cpp
		instance.cpp
		device.cpp
		commandbuffer.cpp
		buffer.cpp
		image.cpp
		device_memory.cpp
		render_pass.cpp
		framebuffer.cpp
		image_view.cpp
		pipeline.cpp
		pipeline_layout.cpp
		queue.cpp
		queue_tracker.cpp
		event.cpp
		sampler.cpp
		commandpool.cpp
		descriptor_pool.cpp
		shader_module.cpp
		descriptor_set.cpp
		descriptor_set_layout.cpp
		swapchain.cpp
		heuristic.cpp
		${export-file})
target_include_directories(VkLayer_mali_perf_doc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_options(VkLayer_mali_perf_doc PUBLIC ${PERFDOC_CXX_FLAGS})

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/layer)

if (WIN32)
	add_custom_command(TARGET VkLayer_mali_perf_doc POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/VkLayer_mali_perf_doc_win32.json ${CMAKE_BINARY_DIR}/layer/VkLayer_mali_perf_doc.json)
	set_target_properties(VkLayer_mali_perf_doc PROPERTIES PREFIX "" LIBRARY_OUTPUT_NAME VkLayer_mali_perf_doc)
	if (CMAKE_COMPILER_IS_GNUCXX)
		if (${CMAKE_BUILD_TYPE} MATCHES "Release")
			set_target_properties(VkLayer_mali_perf_doc PROPERTIES LINK_FLAGS "-Wl,--no-undefined -static -s")
		else()
			set_target_properties(VkLayer_mali_perf_doc PROPERTIES LINK_FLAGS "-Wl,--no-undefined -static")
		endif()
	endif()
else()
	add_custom_command(TARGET VkLayer_mali_perf_doc POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/VkLayer_mali_perf_doc.json ${CMAKE_BINARY_DIR}/layer/VkLayer_mali_perf_doc.json)
	if (${CMAKE_BUILD_TYPE} MATCHES "Release")
		set_target_properties(VkLayer_mali_perf_doc PROPERTIES LINK_FLAGS "-Wl,--no-undefined -s")
	else()
		set_target_properties(VkLayer_mali_perf_doc PROPERTIES LINK_FLAGS "-Wl,--no-undefined")
	endif()
endif()

set_target_properties(VkLayer_mali_perf_doc PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/layer")
set_target_properties(VkLayer_mali_perf_doc PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/layer")
set_target_properties(VkLayer_mali_perf_doc PROPERTIES RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/layer")
set_target_properties(VkLayer_mali_perf_doc PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/layer")

add_subdirectory(SPIRV-Cross EXCLUDE_FROM_ALL)
set_property(TARGET spirv-cross-core PROPERTY POSITION_INDEPENDENT_CODE TRUE)
target_link_libraries(VkLayer_mali_perf_doc spirv-cross-core)

if (ANDROID)
	target_link_libraries(VkLayer_mali_perf_doc log)
endif()
