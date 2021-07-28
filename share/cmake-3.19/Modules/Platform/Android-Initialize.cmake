# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

# When CMAKE_SYSTEM_NAME is "Android", CMakeSystemSpecificInitialize loads this
# module.

# Support for NVIDIA Nsight Tegra Visual Studio Edition was previously
# implemented in the CMake VS IDE generators.  Avoid interfering with
# that functionality for now.
if(CMAKE_VS_PLATFORM_NAME STREQUAL "Tegra-Android")
  return()
endif()

# Commonly used Android toolchain files that pre-date CMake upstream support
# set CMAKE_SYSTEM_VERSION to 1.  Avoid interfering with them.
if(CMAKE_SYSTEM_VERSION EQUAL 1)
  return()
endif()

set(CMAKE_BUILD_TYPE_INIT Debug)

# Skip sysroot selection if the NDK has a unified toolchain.
if(CMAKE_ANDROID_NDK_TOOLCHAIN_UNIFIED)
  return()
endif()

# Natively compiling on an Android host doesn't use the NDK cross-compilation
# tools.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Android")
  return()
endif()

if(NOT CMAKE_SYSROOT)
  if(CMAKE_ANDROID_NDK)
    set(CMAKE_SYSROOT "${CMAKE_ANDROID_NDK}/platforms/android-${CMAKE_SYSTEM_VERSION}/arch-${CMAKE_ANDROID_ARCH}")
    if(NOT CMAKE_ANDROID_NDK_DEPRECATED_HEADERS)
      set(CMAKE_SYSROOT_COMPILE "${CMAKE_ANDROID_NDK}/sysroot")
    endif()
  elseif(CMAKE_ANDROID_STANDALONE_TOOLCHAIN)
    set(CMAKE_SYSROOT "${CMAKE_ANDROID_STANDALONE_TOOLCHAIN}/sysroot")
  endif()
endif()

if(CMAKE_SYSROOT)
  if(NOT IS_DIRECTORY "${CMAKE_SYSROOT}")
    message(FATAL_ERROR
      "Android: The system root directory needed for the selected Android version and architecture does not exist:\n"
      "  ${CMAKE_SYSROOT}\n"
      )
  endif()
else()
  message(FATAL_ERROR
    "Android: No CMAKE_SYSROOT was selected."
    )
endif()
