# PerfDoc

![perfdocbanner](https://user-images.githubusercontent.com/11390552/31229971-5d89fc18-a9da-11e7-978b-9d806ef39450.png)


PerfDoc is a Vulkan layer which aims to validate applications against the
[Mali Application Developer Best Practices](https://developer.arm.com/graphics/developer-guides/mali-gpu-best-practices) document.

Just like the LunarG validation layers, this layer tracks your application and attempts to find API usage which is discouraged.
PerfDoc focuses on checks which can be done up-front, and checks which can portably run on all platforms which support Vulkan.

The intended use of PerfDoc is to be used during development to catch potential performance issues early.
The layer will run on any Vulkan implementation, so Mali-related optimizations can be found even when doing bringup on desktop platforms.
Just like Vulkan validation layers, errors are reported either through `VK_EXT_debug_report` to the application as callbacks, or via console/logcat if enabled.

Dynamic checking (i.e. profiling) of how an application is behaving in run-time is currently not in the scope of PerfDoc.

Some heuristics in PerfDoc are based on "arbitrary limits" in case where there is no obvious limit to use.
These values can be tweaked later via config files if needed.
Some checks which are CPU intensive (index scanning for example), can also be disabled by the config file.

## Features

Currently, the layer implements the checks below.
The layer uses this enumeration to pass down a message code to the application.
The debug callback has a message string with more description.

```
enum MessageCodes
{
	MESSAGE_CODE_COMMAND_BUFFER_RESET = 1,
	MESSAGE_CODE_COMMAND_BUFFER_SIMULTANEOUS_USE = 2,
	MESSAGE_CODE_SMALL_ALLOCATION = 3,
	MESSAGE_CODE_SMALL_DEDICATED_ALLOCATION = 4,
	MESSAGE_CODE_TOO_LARGE_SAMPLE_COUNT = 5,
	MESSAGE_CODE_NON_LAZY_MULTISAMPLED_IMAGE = 6,
	MESSAGE_CODE_NON_LAZY_TRANSIENT_IMAGE = 7,
	MESSAGE_CODE_MULTISAMPLED_IMAGE_REQUIRES_MEMORY = 8,
	MESSAGE_CODE_RESOLVE_IMAGE = 9,
	MESSAGE_CODE_FRAMEBUFFER_ATTACHMENT_SHOULD_BE_TRANSIENT = 10,
	MESSAGE_CODE_FRAMEBUFFER_ATTACHMENT_SHOULD_NOT_BE_TRANSIENT = 11,
	MESSAGE_CODE_INDEX_BUFFER_SPARSE = 12,
	MESSAGE_CODE_INDEX_BUFFER_CACHE_THRASHING = 13,
	MESSAGE_CODE_TOO_MANY_INSTANCED_VERTEX_BUFFERS = 14,
	MESSAGE_CODE_DISSIMILAR_WRAPPING = 15,
	MESSAGE_CODE_NO_PIPELINE_CACHE = 16,
	MESSAGE_CODE_DESCRIPTOR_SET_ALLOCATION_CHECKS = 17,
	MESSAGE_CODE_COMPUTE_NO_THREAD_GROUP_ALIGNMENT = 18,
	MESSAGE_CODE_COMPUTE_LARGE_WORK_GROUP = 19,
	MESSAGE_CODE_COMPUTE_POOR_SPATIAL_LOCALITY = 20,
	MESSAGE_CODE_POTENTIAL_PUSH_CONSTANT = 21,
	MESSAGE_CODE_MANY_SMALL_INDEXED_DRAWCALLS = 22,
	MESSAGE_CODE_DEPTH_PRE_PASS = 23,
	MESSAGE_CODE_PIPELINE_BUBBLE = 24,
	MESSAGE_CODE_NOT_FULL_THROUGHPUT_BLENDING = 25,
	MESSAGE_CODE_SAMPLER_LOD_CLAMPING = 26,
	MESSAGE_CODE_SAMPLER_LOD_BIAS = 27,
	MESSAGE_CODE_SAMPLER_BORDER_CLAMP_COLOR = 28,
	MESSAGE_CODE_SAMPLER_UNNORMALIZED_COORDS = 29,
	MESSAGE_CODE_SAMPLER_ANISOTROPY = 30,
	MESSAGE_CODE_TILE_READBACK = 31,
	MESSAGE_CODE_CLEAR_ATTACHMENTS_AFTER_LOAD = 32,
	MESSAGE_CODE_CLEAR_ATTACHMENTS_NO_DRAW_CALL = 33,
	MESSAGE_CODE_REDUNDANT_RENDERPASS_STORE = 34,
	MESSAGE_CODE_REDUNDANT_IMAGE_CLEAR = 35,
	MESSAGE_CODE_INEFFICIENT_CLEAR = 36,
	MESSAGE_CODE_LAZY_TRANSIENT_IMAGE_NOT_SUPPORTED = 37,

	MESSAGE_CODE_COUNT
};

```

The objectType reported by the layer matches the standard `VK_EXT_debug_report` object types.

## To build

See [BUILD.md](BUILD.md).

## Config file

**The config file is optional.**

There are certain values in PerfDoc which are used as thresholds for heuristics, which can flag potential issues in an application.
Sometimes, these thresholds are somewhat arbitrary and may cause unnecessary false positives for certain applications.
For these scenarios it is possible to provide a config file.
See the sections below for how to enable the config file for Linux/Windows and Android.

Some common options like logging can be overridden directly with environment variables or setprop on Android.
A config file should not be necessary in the common case.

The default config file can be found in `layer/perfdoc-default.cfg`.
This default config file contains all the options available to the layer.
The default config file contains all the default values which are used by the layer if a config file is not present.

## Enabling layers on Linux and Windows

The JSON and binary file must be in the same folder, which is the case after building.

To have the Vulkan loader find the layers, export the following environment variable:

```
VK_LAYER_PATH=/path/to/directory/with/json/and/binary
```

This allows the application to enumerate the layer manually and enable the debug callback from within the app.
The layer name is `VK_LAYER_ARM_mali_perf_doc`.
The layer should appear in `vkEnumerateInstanceLayerProperties` and `vkEnumerateDeviceLayerProperties`.

### Enabling layers outside the application

To force-enable PerfDoc outside the application, some environment variables are needed.

```
VK_LAYER_PATH=/path/to/directory/with/json/and/binary
VK_INSTANCE_LAYERS=VK_LAYER_ARM_mali_perf_doc
VK_DEVICE_LAYERS=VK_LAYER_ARM_mali_perf_doc
```

However, without a `VK_EXT_debug_report` debug callback,
you will not get any output, so to add logging to file or console:

```
# Either one of these
MALI_PERFDOC_LOG=stdout
MALI_PERFDOC_LOG=stderr
MALI_PERFDOC_LOG=/path/to/log.txt
MALI_PERFDOC_LOG=debug_output # OutputDebugString, Windows only
```

It is also possible to use a config file which supports more options as well as logging output:

```
MALI_PERFDOC_CONFIG=/tmp/path/to/config.cfg"
```

## Enabling layers on Android

### ABI (ARMv7 vs. AArch64)

The package contains both ARMv7 binaries and AArch64.
Make sure to use the right version which matches your application.

### Within application

The layer .so must be present in the APKs library directory.
The Android loader will find the layers when enumerating layers, just like the validation layers.

The PerfDoc layer must be enabled explicitly by the app in both `vkCreateInstance` and `vkCreateDevice`.
The layer name is `VK_LAYER_ARM_mali_perf_doc`.
The layer should appear in `vkEnumerateInstanceLayerProperties` and `vkEnumerateDeviceLayerProperties`.

### Outside the application

Vulkan layers can be placed in `/data/local/debug/vulkan` on any device.
Depending on your device, `/data/local/debug/` may be writeable without root access.
It is also possible to place the layer directly inside the application library folder in `/data/data`,
but this will certainly require root.

To force-enable the layer for all Vulkan applications:

```
setprop debug.vulkan.layers VK_LAYER_ARM_mali_perf_doc:
```

Here is an example for how to enable PerfDoc for any Vulkan application:
```
# For ARMv7-A
adb push build-android-armeabi-v7a/layer/libVkLayer_mali_perf_doc.so /data/local/debug/vulkan/
# For AArch64
adb push build-android-arm64-v8a/layer/libVkLayer_mali_perf_doc.so /data/local/debug/vulkan/

adb shell

setprop debug.mali.perfdoc.log logcat
setprop debug.vulkan.layers VK_LAYER_ARM_mali_perf_doc:

exit
adb logcat -c && adb logcat -s MaliPerfDoc
```

#### Enabling logcat/file logging

It is sometimes desirable to use PerfDoc from outside an application,
e.g. when debugging random APKs which do not have PerfDoc integrated.

There are two ways to enable external logging on Android.
Both of the methods described below can also be used when the layer is embedded in the APK (but not enabled by the app),
but they are most relevant when dealing with arbitrary Vulkan applications.

To filter logcat output, you can use:
```
adb logcat -s MaliPerfDoc
```

##### setprop method (Recommended)

To force-enable logging from outside an application, you can set an Android system property:
```
setprop debug.mali.perfdoc.log logcat
```

To log to a file, replace logcat with a filename. Be aware that system properties on Android
have a very limited number of characters available, so a long path might not be possible to represent.
```
setprop debug.mali.perfdoc.log /sdcard/path/to/log.txt
```

##### Config file method

An alternative to setprop is via the config file. This method is a bit more cumbersome than setprop,
but might be more convenient if you are already using a config file for other purposes.

Place a config file on the SD card looking like this:

```
loggingFilename logcat
```

or

```
loggingFilename /sdcard/path/to/log.txt
```

Then, point the layer to this config file by typing this into adb shell:

```
setprop debug.mali.perfdoc.config /sdcard/path/to/perfdoc.cfg
```

Be careful with permissions however. Not all paths on the SD card can be made visible to an application.

