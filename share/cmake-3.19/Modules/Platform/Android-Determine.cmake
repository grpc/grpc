# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

# When CMAKE_SYSTEM_NAME is "Android", CMakeDetermineSystem loads this module.
# This module detects platform-wide information about the Android target
# in order to store it in "CMakeSystem.cmake".

# Support for NVIDIA Nsight Tegra Visual Studio Edition was previously
# implemented in the CMake VS IDE generators.  Avoid interfering with
# that functionality for now.
if(CMAKE_GENERATOR_PLATFORM STREQUAL "Tegra-Android")
  return()
endif()

# Commonly used Android toolchain files that pre-date CMake upstream support
# set CMAKE_SYSTEM_VERSION to 1.  Avoid interfering with them.
if(CMAKE_SYSTEM_VERSION EQUAL 1)
  return()
endif()

# Natively compiling on an Android host doesn't use the NDK cross-compilation
# tools.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Android")
  return()
endif()

cmake_policy(PUSH)
cmake_policy(SET CMP0057 NEW) # if IN_LIST

# If using Android tools for Visual Studio, compile a sample project to get the
# sysroot.
if(CMAKE_GENERATOR MATCHES "Visual Studio")
  if(NOT CMAKE_SYSROOT)
    set(vcx_platform ${CMAKE_GENERATOR_PLATFORM})
    if(CMAKE_GENERATOR MATCHES "Visual Studio 1[45]")
      set(vcx_sysroot_var "Sysroot")
    else()
      set(vcx_sysroot_var "SysrootLink")
    endif()
    if(CMAKE_GENERATOR MATCHES "Visual Studio 14")
      set(vcx_revision "2.0")
    elseif(CMAKE_GENERATOR MATCHES "Visual Studio 1[56]")
      set(vcx_revision "3.0")
    else()
      set(vcx_revision "")
    endif()
    configure_file(${CMAKE_ROOT}/Modules/Platform/Android/VCXProjInspect.vcxproj.in
      ${CMAKE_PLATFORM_INFO_DIR}/VCXProjInspect.vcxproj @ONLY)
    execute_process(
      COMMAND "${CMAKE_VS_MSBUILD_COMMAND}" "VCXProjInspect.vcxproj"
        "/p:Configuration=Debug" "/p:Platform=${vcx_platform}"
      WORKING_DIRECTORY ${CMAKE_PLATFORM_INFO_DIR}
      OUTPUT_VARIABLE VCXPROJ_INSPECT_OUTPUT
      ERROR_VARIABLE VCXPROJ_INSPECT_OUTPUT
      RESULT_VARIABLE VCXPROJ_INSPECT_RESULT
      )
    if(NOT CMAKE_SYSROOT AND VCXPROJ_INSPECT_OUTPUT MATCHES "CMAKE_SYSROOT=([^%\r\n]+)[\r\n]")
      # Strip VS diagnostic output from the end of the line.
      string(REGEX REPLACE " \\(TaskId:[0-9]*\\)$" "" _sysroot "${CMAKE_MATCH_1}")
      if(EXISTS "${_sysroot}")
        file(TO_CMAKE_PATH "${_sysroot}" CMAKE_SYSROOT)
      endif()
    endif()
    if(VCXPROJ_INSPECT_RESULT)
      file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
        "Determining the sysroot for the Android NDK failed.
The output was:
${VCXPROJ_INSPECT_RESULT}
${VCXPROJ_INSPECT_OUTPUT}

")
    else()
      file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
        "Determining the sysroot for the Android NDK succeeded.
The output was:
${VCXPROJ_INSPECT_RESULT}
${VCXPROJ_INSPECT_OUTPUT}

")
    endif()
  endif()
  if(NOT CMAKE_ANDROID_NDK_TOOLCHAIN_VERSION)
    set(CMAKE_ANDROID_NDK_TOOLCHAIN_VERSION "clang")
  endif()
endif()

# If the user provided CMAKE_SYSROOT for us, extract information from it.
set(_ANDROID_SYSROOT_NDK "")
set(_ANDROID_SYSROOT_API "")
set(_ANDROID_SYSROOT_ARCH "")
set(_ANDROID_SYSROOT_STANDALONE_TOOLCHAIN "")
if(CMAKE_SYSROOT)
  if(NOT IS_DIRECTORY "${CMAKE_SYSROOT}")
    message(FATAL_ERROR
      "Android: The specified CMAKE_SYSROOT:\n"
      "  ${CMAKE_SYSROOT}\n"
      "is not an existing directory."
      )
  endif()
  if(CMAKE_SYSROOT MATCHES "^([^\\\n]*)/platforms/android-([0-9]+)/arch-([a-z0-9_]+)$")
    set(_ANDROID_SYSROOT_NDK "${CMAKE_MATCH_1}")
    set(_ANDROID_SYSROOT_API "${CMAKE_MATCH_2}")
    set(_ANDROID_SYSROOT_ARCH "${CMAKE_MATCH_3}")
  elseif(CMAKE_SYSROOT MATCHES "^([^\\\n]*)/sysroot$")
    set(_ANDROID_SYSROOT_STANDALONE_TOOLCHAIN "${CMAKE_MATCH_1}")
  else()
    message(FATAL_ERROR
      "The value of CMAKE_SYSROOT:\n"
      "  ${CMAKE_SYSROOT}\n"
      "does not match any of the forms:\n"
      "  <ndk>/platforms/android-<api>/arch-<arch>\n"
      "  <standalone-toolchain>/sysroot\n"
      "where:\n"
      "  <ndk>  = Android NDK directory (with forward slashes)\n"
      "  <api>  = Android API version number (decimal digits)\n"
      "  <arch> = Android ARCH name (lower case)\n"
      "  <standalone-toolchain> = Path to standalone toolchain prefix\n"
      )
  endif()
endif()

# Find the Android NDK.
if(CMAKE_ANDROID_NDK)
  if(NOT IS_DIRECTORY "${CMAKE_ANDROID_NDK}")
    message(FATAL_ERROR
      "Android: The NDK root directory specified by CMAKE_ANDROID_NDK:\n"
      "  ${CMAKE_ANDROID_NDK}\n"
      "does not exist."
      )
  endif()
elseif(CMAKE_ANDROID_STANDALONE_TOOLCHAIN)
  if(NOT IS_DIRECTORY "${CMAKE_ANDROID_STANDALONE_TOOLCHAIN}")
    message(FATAL_ERROR
      "Android: The standalone toolchain directory specified by CMAKE_ANDROID_STANDALONE_TOOLCHAIN:\n"
      "  ${CMAKE_ANDROID_STANDALONE_TOOLCHAIN}\n"
      "does not exist."
      )
  endif()
  if(NOT EXISTS "${CMAKE_ANDROID_STANDALONE_TOOLCHAIN}/sysroot/usr/include/android/api-level.h")
    message(FATAL_ERROR
      "Android: The standalone toolchain directory specified by CMAKE_ANDROID_STANDALONE_TOOLCHAIN:\n"
      "  ${CMAKE_ANDROID_STANDALONE_TOOLCHAIN}\n"
      "does not contain a sysroot with a known layout.  The file:\n"
      "  ${CMAKE_ANDROID_STANDALONE_TOOLCHAIN}/sysroot/usr/include/android/api-level.h\n"
      "does not exist."
      )
  endif()
else()
  if(IS_DIRECTORY "${_ANDROID_SYSROOT_NDK}")
    set(CMAKE_ANDROID_NDK "${_ANDROID_SYSROOT_NDK}")
  elseif(IS_DIRECTORY "${_ANDROID_SYSROOT_STANDALONE_TOOLCHAIN}")
    set(CMAKE_ANDROID_STANDALONE_TOOLCHAIN "${_ANDROID_SYSROOT_STANDALONE_TOOLCHAIN}")
  elseif(IS_DIRECTORY "${ANDROID_NDK}")
    file(TO_CMAKE_PATH "${ANDROID_NDK}" CMAKE_ANDROID_NDK)
  elseif(IS_DIRECTORY "${ANDROID_STANDALONE_TOOLCHAIN}")
    file(TO_CMAKE_PATH "${ANDROID_STANDALONE_TOOLCHAIN}" CMAKE_ANDROID_STANDALONE_TOOLCHAIN)
  elseif(IS_DIRECTORY "$ENV{ANDROID_NDK_ROOT}")
    file(TO_CMAKE_PATH "$ENV{ANDROID_NDK_ROOT}" CMAKE_ANDROID_NDK)
  elseif(IS_DIRECTORY "$ENV{ANDROID_NDK}")
    file(TO_CMAKE_PATH "$ENV{ANDROID_NDK}" CMAKE_ANDROID_NDK)
  elseif(IS_DIRECTORY "$ENV{ANDROID_STANDALONE_TOOLCHAIN}")
    file(TO_CMAKE_PATH "$ENV{ANDROID_STANDALONE_TOOLCHAIN}" CMAKE_ANDROID_STANDALONE_TOOLCHAIN)
  endif()
  # TODO: Search harder for the NDK or standalone toolchain.
endif()

set(_ANDROID_STANDALONE_TOOLCHAIN_API "")
if(CMAKE_ANDROID_STANDALONE_TOOLCHAIN)
  # Try to read the API level from the toolchain launcher.
  if(EXISTS "${CMAKE_ANDROID_STANDALONE_TOOLCHAIN}/bin/clang")
    set(_ANDROID_API_LEVEL_CLANG_REGEX "__ANDROID_API__=([0-9]+)")
    file(STRINGS "${CMAKE_ANDROID_STANDALONE_TOOLCHAIN}/bin/clang" _ANDROID_STANDALONE_TOOLCHAIN_BIN_CLANG
      REGEX "${_ANDROID_API_LEVEL_CLANG_REGEX}" LIMIT_COUNT 1 LIMIT_INPUT 65536)
    if(_ANDROID_STANDALONE_TOOLCHAIN_BIN_CLANG MATCHES "${_ANDROID_API_LEVEL_CLANG_REGEX}")
      set(_ANDROID_STANDALONE_TOOLCHAIN_API "${CMAKE_MATCH_1}")
    endif()
    unset(_ANDROID_STANDALONE_TOOLCHAIN_BIN_CLANG)
    unset(_ANDROID_API_LEVEL_CLANG_REGEX)
  endif()
  if(NOT _ANDROID_STANDALONE_TOOLCHAIN_API)
    # The compiler launcher does not know __ANDROID_API__.  Assume this
    # is not unified headers and look for it in the api-level.h header.
    set(_ANDROID_API_LEVEL_H_REGEX "^[\t ]*#[\t ]*define[\t ]+__ANDROID_API__[\t ]+([0-9]+)")
    file(STRINGS "${CMAKE_ANDROID_STANDALONE_TOOLCHAIN}/sysroot/usr/include/android/api-level.h"
      _ANDROID_API_LEVEL_H_CONTENT REGEX "${_ANDROID_API_LEVEL_H_REGEX}")
    if(_ANDROID_API_LEVEL_H_CONTENT MATCHES "${_ANDROID_API_LEVEL_H_REGEX}")
      set(_ANDROID_STANDALONE_TOOLCHAIN_API "${CMAKE_MATCH_1}")
    endif()
  endif()
  if(NOT _ANDROID_STANDALONE_TOOLCHAIN_API)
    message(WARNING
      "Android: Did not detect API level from\n"
      "  ${CMAKE_ANDROID_STANDALONE_TOOLCHAIN}/bin/clang\n"
      "or\n"
      "  ${CMAKE_ANDROID_STANDALONE_TOOLCHAIN}/sysroot/usr/include/android/api-level.h\n"
      )
  endif()
endif()

if(NOT CMAKE_ANDROID_NDK AND NOT CMAKE_ANDROID_STANDALONE_TOOLCHAIN)
  message(FATAL_ERROR "Android: Neither the NDK or a standalone toolchain was found.")
endif()

if(CMAKE_ANDROID_NDK)
  # NDK >= 18 has platforms.cmake. It provides:
  #   NDK_MIN_PLATFORM_LEVEL
  #   NDK_MAX_PLATFORM_LEVEL
  include("${CMAKE_ANDROID_NDK}/build/cmake/platforms.cmake" OPTIONAL RESULT_VARIABLE _INCLUDED_PLATFORMS)
  # NDK >= 18 has abis.cmake. It provides:
  #   NDK_KNOWN_DEVICE_ABI32S
  #   NDK_KNOWN_DEVICE_ABI64S
  include("${CMAKE_ANDROID_NDK}/build/cmake/abis.cmake" OPTIONAL RESULT_VARIABLE _INCLUDED_ABIS)
endif()

if(CMAKE_ANDROID_NDK)
  # Identify the host platform.
  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "x86_64")
      set(CMAKE_ANDROID_NDK_TOOLCHAIN_HOST_TAG "darwin-x86_64")
    else()
      set(CMAKE_ANDROID_NDK_TOOLCHAIN_HOST_TAG "darwin-x86")
    endif()
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "x86_64")
      set(CMAKE_ANDROID_NDK_TOOLCHAIN_HOST_TAG "linux-x86_64")
    else()
      set(CMAKE_ANDROID_NDK_TOOLCHAIN_HOST_TAG "linux-x86")
    endif()
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "AMD64")
      set(CMAKE_ANDROID_NDK_TOOLCHAIN_HOST_TAG "windows-x86_64")
    else()
      set(CMAKE_ANDROID_NDK_TOOLCHAIN_HOST_TAG "windows")
    endif()
  else()
    message(FATAL_ERROR "Android: Builds hosted on '${CMAKE_HOST_SYSTEM_NAME}' not supported.")
  endif()

  # Look for a unified toolchain/sysroot provided with the NDK.
  set(CMAKE_ANDROID_NDK_TOOLCHAIN_UNIFIED "${CMAKE_ANDROID_NDK}/toolchains/llvm/prebuilt/${CMAKE_ANDROID_NDK_TOOLCHAIN_HOST_TAG}")
  if(NOT IS_DIRECTORY "${CMAKE_ANDROID_NDK_TOOLCHAIN_UNIFIED}/sysroot")
    set(CMAKE_ANDROID_NDK_TOOLCHAIN_UNIFIED "")
  endif()
else()
  set(CMAKE_ANDROID_NDK_TOOLCHAIN_HOST_TAG "")
  set(CMAKE_ANDROID_NDK_TOOLCHAIN_UNIFIED "")
endif()

if(_INCLUDED_ABIS)
  set(_ANDROID_KNOWN_ABIS ${NDK_KNOWN_DEVICE_ABI32S} ${NDK_KNOWN_DEVICE_ABI64S})
endif()

# https://developer.android.com/ndk/guides/abis.html

set(_ANDROID_ABI_arm64-v8a_PROC     "aarch64")
set(_ANDROID_ABI_arm64-v8a_ARCH     "arm64")
set(_ANDROID_ABI_arm64-v8a_TRIPLE   "aarch64-linux-android")
set(_ANDROID_ABI_armeabi-v7a_PROC   "armv7-a")
set(_ANDROID_ABI_armeabi-v7a_ARCH   "arm")
set(_ANDROID_ABI_armeabi-v7a_TRIPLE "arm-linux-androideabi")
set(_ANDROID_ABI_armeabi-v6_PROC    "armv6")
set(_ANDROID_ABI_armeabi-v6_ARCH    "arm")
set(_ANDROID_ABI_armeabi-v6_TRIPLE  "arm-linux-androideabi")
set(_ANDROID_ABI_armeabi_PROC       "armv5te")
set(_ANDROID_ABI_armeabi_ARCH       "arm")
set(_ANDROID_ABI_armeabi_TRIPLE     "arm-linux-androideabi")
set(_ANDROID_ABI_mips_PROC          "mips")
set(_ANDROID_ABI_mips_ARCH          "mips")
set(_ANDROID_ABI_mips_TRIPLE        "mipsel-linux-android")
set(_ANDROID_ABI_mips64_PROC        "mips64")
set(_ANDROID_ABI_mips64_ARCH        "mips64")
set(_ANDROID_ABI_mips64_TRIPLE      "mips64el-linux-android")
set(_ANDROID_ABI_x86_PROC           "i686")
set(_ANDROID_ABI_x86_ARCH           "x86")
set(_ANDROID_ABI_x86_TRIPLE         "i686-linux-android")
set(_ANDROID_ABI_x86_64_PROC        "x86_64")
set(_ANDROID_ABI_x86_64_ARCH        "x86_64")
set(_ANDROID_ABI_x86_64_TRIPLE      "x86_64-linux-android")

set(_ANDROID_PROC_aarch64_ARCH_ABI "arm64-v8a")
set(_ANDROID_PROC_armv7-a_ARCH_ABI "armeabi-v7a")
set(_ANDROID_PROC_armv6_ARCH_ABI   "armeabi-v6")
set(_ANDROID_PROC_armv5te_ARCH_ABI "armeabi")
set(_ANDROID_PROC_i686_ARCH_ABI    "x86")
set(_ANDROID_PROC_mips_ARCH_ABI    "mips")
set(_ANDROID_PROC_mips64_ARCH_ABI  "mips64")
set(_ANDROID_PROC_x86_64_ARCH_ABI  "x86_64")

set(_ANDROID_ARCH_arm64_ABI  "arm64-v8a")
set(_ANDROID_ARCH_arm_ABI    "armeabi")
set(_ANDROID_ARCH_mips_ABI   "mips")
set(_ANDROID_ARCH_mips64_ABI "mips64")
set(_ANDROID_ARCH_x86_ABI    "x86")
set(_ANDROID_ARCH_x86_64_ABI "x86_64")

# Validate inputs.
if(CMAKE_ANDROID_ARCH_ABI AND NOT DEFINED "_ANDROID_ABI_${CMAKE_ANDROID_ARCH_ABI}_PROC")
  message(FATAL_ERROR "Android: Unknown ABI CMAKE_ANDROID_ARCH_ABI='${CMAKE_ANDROID_ARCH_ABI}'.")
endif()
if(CMAKE_SYSTEM_PROCESSOR AND NOT DEFINED "_ANDROID_PROC_${CMAKE_SYSTEM_PROCESSOR}_ARCH_ABI")
  message(FATAL_ERROR "Android: Unknown processor CMAKE_SYSTEM_PROCESSOR='${CMAKE_SYSTEM_PROCESSOR}'.")
endif()
if(_ANDROID_SYSROOT_ARCH AND NOT DEFINED "_ANDROID_ARCH_${_ANDROID_SYSROOT_ARCH}_ABI")
  message(FATAL_ERROR
    "Android: Unknown architecture '${_ANDROID_SYSROOT_ARCH}' specified in CMAKE_SYSROOT:\n"
    "  ${CMAKE_SYSROOT}"
    )
endif()

# Select an ABI.
if(NOT CMAKE_ANDROID_ARCH_ABI)
  if(CMAKE_SYSTEM_PROCESSOR)
    set(CMAKE_ANDROID_ARCH_ABI "${_ANDROID_PROC_${CMAKE_SYSTEM_PROCESSOR}_ARCH_ABI}")
  elseif(_ANDROID_SYSROOT_ARCH)
    set(CMAKE_ANDROID_ARCH_ABI "${_ANDROID_ARCH_${_ANDROID_SYSROOT_ARCH}_ABI}")
  elseif(_INCLUDED_ABIS)
    # Default to the oldest ARM ABI.
    foreach(abi armeabi armeabi-v7a arm64-v8a)
      if("${abi}" IN_LIST _ANDROID_KNOWN_ABIS)
        set(CMAKE_ANDROID_ARCH_ABI "${abi}")
        break()
      endif()
    endforeach()
    if(NOT CMAKE_ANDROID_ARCH_ABI)
      message(FATAL_ERROR
        "Android: Can not determine the default ABI. Please set CMAKE_ANDROID_ARCH_ABI."
      )
    endif()
  else()
    # https://developer.android.com/ndk/guides/application_mk.html
    # Default is the oldest ARM ABI.

    # Lookup the available ABIs among all toolchains.
    set(_ANDROID_ABIS "")
    file(GLOB _ANDROID_CONFIG_MKS
      "${CMAKE_ANDROID_NDK}/build/core/toolchains/*/config.mk"
      "${CMAKE_ANDROID_NDK}/toolchains/*/config.mk"
      )
    foreach(config_mk IN LISTS _ANDROID_CONFIG_MKS)
      file(STRINGS "${config_mk}" _ANDROID_TOOL_ABIS REGEX "^TOOLCHAIN_ABIS :=")
      string(REPLACE "TOOLCHAIN_ABIS :=" "" _ANDROID_TOOL_ABIS "${_ANDROID_TOOL_ABIS}")
      separate_arguments(_ANDROID_TOOL_ABIS UNIX_COMMAND "${_ANDROID_TOOL_ABIS}")
      list(APPEND _ANDROID_ABIS ${_ANDROID_TOOL_ABIS})
      unset(_ANDROID_TOOL_ABIS)
    endforeach()
    unset(_ANDROID_CONFIG_MKS)

    # Choose the oldest among the available arm ABIs.
    if(_ANDROID_ABIS)
      list(REMOVE_DUPLICATES _ANDROID_ABIS)
      foreach(abi armeabi armeabi-v7a arm64-v8a)
        if("${abi}" IN_LIST _ANDROID_ABIS)
          set(CMAKE_ANDROID_ARCH_ABI "${abi}")
          break()
        endif()
      endforeach()
    endif()
    unset(_ANDROID_ABIS)

    if(NOT CMAKE_ANDROID_ARCH_ABI)
      set(CMAKE_ANDROID_ARCH_ABI "armeabi")
    endif()
  endif()
endif()
if(_INCLUDED_ABIS AND NOT CMAKE_ANDROID_ARCH_ABI IN_LIST _ANDROID_KNOWN_ABIS)
  message(FATAL_ERROR
    "Android: ABI '${CMAKE_ANDROID_ARCH_ABI}' is not supported by the NDK.\n"
    "Supported ABIS: ${_ANDROID_KNOWN_ABIS}."
  )
endif()
set(CMAKE_ANDROID_ARCH "${_ANDROID_ABI_${CMAKE_ANDROID_ARCH_ABI}_ARCH}")
if(_ANDROID_SYSROOT_ARCH AND NOT "x${_ANDROID_SYSROOT_ARCH}" STREQUAL "x${CMAKE_ANDROID_ARCH}")
  message(FATAL_ERROR
    "Android: Architecture '${_ANDROID_SYSROOT_ARCH}' specified in CMAKE_SYSROOT:\n"
    "  ${CMAKE_SYSROOT}\n"
    "does not match architecture '${CMAKE_ANDROID_ARCH}' for the ABI '${CMAKE_ANDROID_ARCH_ABI}'."
    )
endif()
set(CMAKE_ANDROID_ARCH_TRIPLE "${_ANDROID_ABI_${CMAKE_ANDROID_ARCH_ABI}_TRIPLE}")

# Select a processor.
if(NOT CMAKE_SYSTEM_PROCESSOR)
  set(CMAKE_SYSTEM_PROCESSOR "${_ANDROID_ABI_${CMAKE_ANDROID_ARCH_ABI}_PROC}")
endif()

# If the user specified both an ABI and a processor then they might not match.
if(NOT _ANDROID_ABI_${CMAKE_ANDROID_ARCH_ABI}_PROC STREQUAL CMAKE_SYSTEM_PROCESSOR)
  message(FATAL_ERROR "Android: The specified CMAKE_ANDROID_ARCH_ABI='${CMAKE_ANDROID_ARCH_ABI}' and CMAKE_SYSTEM_PROCESSOR='${CMAKE_SYSTEM_PROCESSOR}' is not a valid combination.")
endif()

# Select an API.
if(CMAKE_SYSTEM_VERSION)
  set(_ANDROID_API_VAR CMAKE_SYSTEM_VERSION)
elseif(CMAKE_ANDROID_API)
  set(CMAKE_SYSTEM_VERSION "${CMAKE_ANDROID_API}")
  set(_ANDROID_API_VAR CMAKE_ANDROID_API)
elseif(_ANDROID_SYSROOT_API)
  set(CMAKE_SYSTEM_VERSION "${_ANDROID_SYSROOT_API}")
  set(_ANDROID_API_VAR CMAKE_SYSROOT)
elseif(_ANDROID_STANDALONE_TOOLCHAIN_API)
  set(CMAKE_SYSTEM_VERSION "${_ANDROID_STANDALONE_TOOLCHAIN_API}")
endif()
if(CMAKE_SYSTEM_VERSION)
  if(CMAKE_ANDROID_API AND NOT "x${CMAKE_ANDROID_API}" STREQUAL "x${CMAKE_SYSTEM_VERSION}")
    message(FATAL_ERROR
      "Android: The API specified by CMAKE_ANDROID_API='${CMAKE_ANDROID_API}' is not consistent with CMAKE_SYSTEM_VERSION='${CMAKE_SYSTEM_VERSION}'."
      )
  endif()
  if(_ANDROID_SYSROOT_API)
    foreach(v CMAKE_ANDROID_API CMAKE_SYSTEM_VERSION)
      if(${v} AND NOT "x${_ANDROID_SYSROOT_API}" STREQUAL "x${${v}}")
        message(FATAL_ERROR
          "Android: The API specified by ${v}='${${v}}' is not consistent with CMAKE_SYSROOT:\n"
          "  ${CMAKE_SYSROOT}"
          )
      endif()
    endforeach()
  endif()
  if(CMAKE_ANDROID_NDK)
    if (_INCLUDED_PLATFORMS)
      if(CMAKE_SYSTEM_VERSION GREATER NDK_MAX_PLATFORM_LEVEL OR
         CMAKE_SYSTEM_VERSION LESS NDK_MIN_PLATFORM_LEVEL)
        message(FATAL_ERROR
          "Android: The API level ${CMAKE_SYSTEM_VERSION} is not supported by the NDK.\n"
          "Choose one in the range of [${NDK_MIN_PLATFORM_LEVEL}, ${NDK_MAX_PLATFORM_LEVEL}]."
        )
      endif()
    else()
      if(NOT IS_DIRECTORY "${CMAKE_ANDROID_NDK}/platforms/android-${CMAKE_SYSTEM_VERSION}")
        message(FATAL_ERROR
          "Android: The API specified by ${_ANDROID_API_VAR}='${${_ANDROID_API_VAR}}' does not exist in the NDK.  "
          "The directory:\n"
          "  ${CMAKE_ANDROID_NDK}/platforms/android-${CMAKE_SYSTEM_VERSION}\n"
          "does not exist."
          )
      endif()
    endif()
  endif()
elseif(CMAKE_ANDROID_NDK)
  if (_INCLUDED_PLATFORMS)
    set(CMAKE_SYSTEM_VERSION ${NDK_MIN_PLATFORM_LEVEL})
    # And for LP64 we need to pull up to 21. No diagnostic is provided here because
    # minSdkVersion < 21 is valid for the project even though it may not be for this
    # ABI.
    if(CMAKE_ANDROID_ARCH_ABI MATCHES "64(-v8a)?$" AND CMAKE_SYSTEM_VERSION LESS 21)
      set(CMAKE_SYSTEM_VERSION 21)
    endif()
  else()
    file(GLOB _ANDROID_APIS_1 RELATIVE "${CMAKE_ANDROID_NDK}/platforms" "${CMAKE_ANDROID_NDK}/platforms/android-[0-9]")
    file(GLOB _ANDROID_APIS_2 RELATIVE "${CMAKE_ANDROID_NDK}/platforms" "${CMAKE_ANDROID_NDK}/platforms/android-[0-9][0-9]")
    list(SORT _ANDROID_APIS_1)
    list(SORT _ANDROID_APIS_2)
    set(_ANDROID_APIS ${_ANDROID_APIS_1} ${_ANDROID_APIS_2})
    unset(_ANDROID_APIS_1)
    unset(_ANDROID_APIS_2)
    if(_ANDROID_APIS STREQUAL "")
      message(FATAL_ERROR
        "Android: No APIs found in the NDK.  No\n"
        "  ${CMAKE_ANDROID_NDK}/platforms/android-*\n"
        "directories exist."
        )
    endif()
    string(REPLACE "android-" "" _ANDROID_APIS "${_ANDROID_APIS}")
    list(REVERSE _ANDROID_APIS)
    list(GET _ANDROID_APIS 0 CMAKE_SYSTEM_VERSION)
    unset(_ANDROID_APIS)
  endif()
endif()
if(NOT CMAKE_SYSTEM_VERSION MATCHES "^[0-9]+$")
  message(FATAL_ERROR "Android: The API specified by CMAKE_SYSTEM_VERSION='${CMAKE_SYSTEM_VERSION}' is not an integer.")
endif()

if(CMAKE_ANDROID_NDK AND NOT DEFINED CMAKE_ANDROID_NDK_DEPRECATED_HEADERS)
  if(IS_DIRECTORY "${CMAKE_ANDROID_NDK}/sysroot/usr/include/${CMAKE_ANDROID_ARCH_TRIPLE}")
    # Unified headers exist so we use them by default.
    set(CMAKE_ANDROID_NDK_DEPRECATED_HEADERS 0)
  else()
    # Unified headers do not exist so use the deprecated headers.
    set(CMAKE_ANDROID_NDK_DEPRECATED_HEADERS 1)
  endif()
endif()

# Save the Android-specific information in CMakeSystem.cmake.
set(CMAKE_SYSTEM_CUSTOM_CODE "
set(CMAKE_ANDROID_NDK \"${CMAKE_ANDROID_NDK}\")
set(CMAKE_ANDROID_STANDALONE_TOOLCHAIN \"${CMAKE_ANDROID_STANDALONE_TOOLCHAIN}\")
set(CMAKE_ANDROID_ARCH \"${CMAKE_ANDROID_ARCH}\")
set(CMAKE_ANDROID_ARCH_ABI \"${CMAKE_ANDROID_ARCH_ABI}\")
")

if(CMAKE_ANDROID_NDK)
  string(APPEND CMAKE_SYSTEM_CUSTOM_CODE
    "set(CMAKE_ANDROID_ARCH_TRIPLE \"${CMAKE_ANDROID_ARCH_TRIPLE}\")\n"
    "set(CMAKE_ANDROID_NDK_DEPRECATED_HEADERS \"${CMAKE_ANDROID_NDK_DEPRECATED_HEADERS}\")\n"
    "set(CMAKE_ANDROID_NDK_TOOLCHAIN_HOST_TAG \"${CMAKE_ANDROID_NDK_TOOLCHAIN_HOST_TAG}\")\n"
    "set(CMAKE_ANDROID_NDK_TOOLCHAIN_UNIFIED \"${CMAKE_ANDROID_NDK_TOOLCHAIN_UNIFIED}\")\n"
    )
endif()

# Select an ARM variant.
if(CMAKE_ANDROID_ARCH_ABI MATCHES "^armeabi")
  if(CMAKE_ANDROID_ARM_MODE)
    set(CMAKE_ANDROID_ARM_MODE 1)
  else()
    set(CMAKE_ANDROID_ARM_MODE 0)
  endif()
  string(APPEND CMAKE_SYSTEM_CUSTOM_CODE
    "set(CMAKE_ANDROID_ARM_MODE \"${CMAKE_ANDROID_ARM_MODE}\")\n"
    )
elseif(DEFINED CMAKE_ANDROID_ARM_MODE)
  message(FATAL_ERROR "Android: CMAKE_ANDROID_ARM_MODE is set but is valid only for 'armeabi' architectures.")
endif()

if(CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
  if(CMAKE_ANDROID_ARM_NEON)
    set(CMAKE_ANDROID_ARM_NEON 1)
  else()
    set(CMAKE_ANDROID_ARM_NEON 0)
  endif()
  string(APPEND CMAKE_SYSTEM_CUSTOM_CODE
    "set(CMAKE_ANDROID_ARM_NEON \"${CMAKE_ANDROID_ARM_NEON}\")\n"
    )
elseif(DEFINED CMAKE_ANDROID_ARM_NEON)
  message(FATAL_ERROR "Android: CMAKE_ANDROID_ARM_NEON is set but is valid only for 'armeabi-v7a' architecture.")
endif()

# Report the chosen architecture.
message(STATUS "Android: Targeting API '${CMAKE_SYSTEM_VERSION}' with architecture '${CMAKE_ANDROID_ARCH}', ABI '${CMAKE_ANDROID_ARCH_ABI}', and processor '${CMAKE_SYSTEM_PROCESSOR}'")

cmake_policy(POP)
