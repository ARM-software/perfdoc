## Building

```
git submodule init
git submodule update
```

### Building layer
```
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug # For some reason the CMAKE_BUILD_TYPE must be set, or include path issues will happen.
make -j8 # If using Makefile target in CMake.
```

### Building and running tests
```
# To run tests
# glslc must be in $PATH.
cmake .. -DCMAKE_BUILD_TYPE=Debug -DPERFDOC_TESTS=ON
make -j8 # If using Makefile target in CMake.
../run_tests.sh -C <Config> # -C <Config> is required on MSVC.
```

### Android

The layer can be built using bundled CMake and NDK from Android Studio

```
mkdir build-android
cd build-android

export ANDROID_SDK=$HOME/Android/Sdk # Typical, but depends where Android Studio installed the SDK.
export ANDROID_ABI=arm64-v8a # For aarch64, armeabi-v7a for ARMv7a.
$ANDROID_SDK/cmake/3.6.3155560/bin/cmake \
	.. \
	-DANDROID_STL=c++_static \
	-DANDROID_TOOLCHAIN=clang \
	-DCMAKE_TOOLCHAIN_FILE=$ANDROID_SDK/ndk-bundle/build/cmake/android.toolchain.cmake \
	-DANDROID_ABI=$ANDROID_ABI \
	-DANDROID_CPP_FEATURES=exceptions \
	-DANDROID_ARM_MODE=arm

make -j8
```

### Windows

#### MinGW-w64

```
mkdir build-windows
cd build-windows
x86_64-w64-mingw-cmake ..
make -j8
```

#### MSVC

Build has been tested with MSVC 2017, but may work on earlier versions.

```
mkdir build-windows-msvc
cd build-windows-msvc
cmake .. -G "Visual Studio 15 2017 Win64"
cmake --build . --config Release # Or, open the solution in MSVC and build there.
```

