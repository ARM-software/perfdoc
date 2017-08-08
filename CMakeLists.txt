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

cmake_minimum_required(VERSION 3.5)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD 99)

if (CMAKE_COMPILER_IS_GNUCXX OR (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang"))
    set(PERFDOC_CXX_FLAGS -Wall -Wextra -Wno-missing-field-initializers)
elseif(MSVC)
    set(PERFDOC_CXX_FLAGS /wd4244 /wd4996 /wd4800 /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX)
endif()

project(Mali-PerfDoc LANGUAGES CXX C)

enable_testing()
set(PLATFORM png)
add_subdirectory(layer)

option(PERFDOC_TESTS "Enable unit tests." OFF)
if (PERFDOC_TESTS)
if (NOT ANDROID)
    add_subdirectory(tests)
endif()
endif()
