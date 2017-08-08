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

option(UNIT_TESTS "Build unit tests" ON)

function(add_layer_test TARGET SOURCES)
        add_executable(${TARGET} ${SOURCES})
        add_test(NAME ${TARGET} COMMAND $<TARGET_FILE:${TARGET}>)
        target_compile_options(${TARGET} PUBLIC ${PERFDOC_CXX_FLAGS})
        target_link_libraries(${TARGET} test-util)
        target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../layer ${CMAKE_BINARY_DIR}/glsl)
        add_dependencies(${TARGET} shaders)
endfunction()

if (UNIT_TESTS)
	set(CTEST_ENVIRONMENT "VK_LAYER_PATH=${CMAKE_BINARY_DIR}/layer" "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/layer:${LD_LIBRARY_PATH}")
	add_subdirectory(glsl)
	add_subdirectory(util)
	add_layer_test(stub-layer-perfdoc stub-layer-test.cpp)
	add_layer_test(commandbuffer-perfdoc commandbuffer-test.cpp)
	add_layer_test(allocation-size-perfdoc allocation-size-test.cpp)
	add_layer_test(multisampling-perfdoc multisampling.cpp)
	add_layer_test(transient-perfdoc transient.cpp)
	add_layer_test(sampler-perfdoc samplers.cpp)
	add_layer_test(descriptor-set-allocation-checks descriptor-set-allocation-checks.cpp)
	add_layer_test(compute-perfdoc compute-test.cpp)
	add_layer_test(push-constant-perfdoc push-constant.cpp)
	add_layer_test(queue-perfdoc queue-test.cpp)
	add_layer_test(clear-image-perfdoc clear-image.cpp)
endif()
