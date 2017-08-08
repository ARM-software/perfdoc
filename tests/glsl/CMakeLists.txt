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

add_custom_target(shaders)

set(GLSL_FLAGS -mfmt=c)
function(add_shader OUTPUT INPUT)
   file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/glsl)
   add_custom_command(
      OUTPUT ${CMAKE_BINARY_DIR}/glsl/${OUTPUT}.inc 
      COMMAND glslc ${ARGN} -o ${CMAKE_BINARY_DIR}/glsl/${OUTPUT}.inc ${GLSL_FLAGS} ${CMAKE_CURRENT_SOURCE_DIR}/${INPUT}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${INPUT}
      IMPLICIT_DEPENDS CXX ${CMAKE_CURRENT_SOURCE_DIR}/${INPUT}
      )
   add_custom_target(${OUTPUT} DEPENDS ${CMAKE_BINARY_DIR}/glsl/${OUTPUT}.inc)
   set_source_files_properties(${CMAKE_BINARY_DIR}/glsl/${OUTPUT}.inc PROPERTIES GENERATED TRUE)
   add_dependencies(shaders ${OUTPUT})
endfunction(add_shader)

add_shader(quad.vert quad.vert)
add_shader(quad.frag quad.frag)
add_shader(quad_sampler.frag quad_sampler.frag)
add_shader(quad_no_attribs.vert quad_no_attribs.vert)

add_shader(compute.wg.4.1.1.comp basic.comp -DWG_X=4 -DWG_Y=1 -DWG_Z=1)
add_shader(compute.wg.4.1.3.comp basic.comp -DWG_X=4 -DWG_Y=1 -DWG_Z=3)
add_shader(compute.wg.16.8.1.comp basic.comp -DWG_X=16 -DWG_Y=8 -DWG_Z=1)

add_shader(compute.sampler.2d.8.8.1.comp sampler.comp -DWG_X=8 -DWG_Y=8 -DWG_Z=1 -DSAMPLER_2D)
add_shader(compute.sampler.1d.64.1.1.comp sampler.comp -DWG_X=64 -DWG_Y=1 -DWG_Z=1 -DSAMPLER_1D)
add_shader(compute.sampler.2d.64.1.1.comp sampler.comp -DWG_X=64 -DWG_Y=1 -DWG_Z=1 -DSAMPLER_2D)

add_shader(push_constant.push.comp push_constant.comp -DPUSH_CONSTANT)
add_shader(push_constant.nopush.comp push_constant.comp)

