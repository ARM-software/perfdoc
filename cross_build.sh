#!/bin/bash

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

mode=$1
if [ -z $1 ]; then
	mode=Release
fi

njobs=`nproc`

# Desktop Linux
mkdir -p build-linux-x64
cd build-linux-x64
cmake .. -DPERFDOC_TESTS=ON -DCMAKE_BUILD_TYPE=$mode
make -j$njobs
cd ..

# ARM Linux (ARMv7 hard-float)
mkdir -p build-linux-armv7-hf
cd build-linux-armv7-hf
cmake .. -DPERFDOC_TESTS=ON -DCMAKE_BUILD_TYPE=$mode -DCMAKE_TOOLCHAIN_FILE=../toolchains/armhf.cmake
make -j$njobs
cd ..

# Android
if [ -z $ANDROID_SDK ]; then
	ANDROID_SDK="$HOME/Android/Sdk"
fi

# Android armeabi-v7a
mkdir -p build-android-armeabi-v7a
cd build-android-armeabi-v7a

ANDROID_ABI=armeabi-v7a
"$ANDROID_SDK"/cmake/*/bin/cmake \
	.. \
	-DANDROID_STL=c++_static \
	-DANDROID_TOOLCHAIN=clang \
	-DCMAKE_TOOLCHAIN_FILE="$ANDROID_SDK/ndk-bundle/build/cmake/android.toolchain.cmake" \
	-DANDROID_ABI=$ANDROID_ABI \
	-DANDROID_CPP_FEATURES=exceptions \
	-DANDROID_ARM_MODE=arm \
	-DCMAKE_BUILD_TYPE=$mode

make -j$njobs
cd ..

# Android arm64-v8a
mkdir -p build-android-arm64-v8a
cd build-android-arm64-v8a

ANDROID_ABI=arm64-v8a
"$ANDROID_SDK"/cmake/*/bin/cmake \
	.. \
	-DANDROID_STL=c++_static \
	-DANDROID_TOOLCHAIN=clang \
	-DCMAKE_TOOLCHAIN_FILE="$ANDROID_SDK/ndk-bundle/build/cmake/android.toolchain.cmake" \
	-DANDROID_ABI=$ANDROID_ABI \
	-DANDROID_CPP_FEATURES=exceptions \
	-DANDROID_ARM_MODE=arm \
	-DCMAKE_BUILD_TYPE=$mode

make -j$njobs
cd ..

# MinGW i686
mkdir -p build-windows-x86
cd build-windows-x86
i686-w64-mingw32-cmake .. -DPERFDOC_TESTS=ON -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=$mode
make -j$njobs
cd ..

# MinGW x86_64
mkdir -p build-windows-x64
cd build-windows-x64
x86_64-w64-mingw32-cmake .. -DPERFDOC_TESTS=ON -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=$mode
make -j$njobs
cd ..

day=$(date "+%Y-%m-%d")
commit=$(git rev-parse HEAD | cut -b 1-10)
zip mali-perfdoc-$day-$commit.zip \
	README.md \
	layer/perfdoc-default.cfg \
	build-linux-x64/layer/*.{so,json} \
	build-linux-armv7-hf/layer/*.{so,json} \
	build-android-armeabi-v7a/layer/*.so \
	build-android-arm64-v8a/layer/*.so \
	build-windows-x86/layer/*.{dll,json} \
	build-windows-x64/layer/*.{dll,json}

