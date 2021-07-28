# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


# This module is shared by multiple languages; use include blocker.
if(__COMPILER_CLANG)
  return()
endif()
set(__COMPILER_CLANG 1)

include(Compiler/CMakeCommonCompilerMacros)

set(__pch_header_C "c-header")
set(__pch_header_CXX "c++-header")
set(__pch_header_OBJC "objective-c-header")
set(__pch_header_OBJCXX "objective-c++-header")

if("x${CMAKE_C_SIMULATE_ID}" STREQUAL "xMSVC"
    OR "x${CMAKE_CXX_SIMULATE_ID}" STREQUAL "xMSVC"
    OR "x${CMAKE_Fortran_SIMULATE_ID}" STREQUAL "xMSVC")
  macro(__compiler_clang lang)
  endmacro()
else()
  include(Compiler/GNU)

  macro(__compiler_clang lang)
    __compiler_gnu(${lang})
    set(CMAKE_${lang}_COMPILE_OPTIONS_PIE "-fPIE")
    # Link options for PIE are already set in 'Compiler/GNU.cmake'
    # but clang may require alternate syntax on some platforms
    if (APPLE)
      set(CMAKE_${lang}_LINK_OPTIONS_PIE ${CMAKE_${lang}_COMPILE_OPTIONS_PIE} -Xlinker -pie)
      set(CMAKE_${lang}_LINK_OPTIONS_NO_PIE -Xlinker -no_pie)
    endif()
    set(CMAKE_INCLUDE_SYSTEM_FLAG_${lang} "-isystem ")
    set(CMAKE_${lang}_COMPILE_OPTIONS_VISIBILITY "-fvisibility=")
    if(CMAKE_${lang}_COMPILER_VERSION VERSION_LESS 3.4.0)
      set(CMAKE_${lang}_COMPILE_OPTIONS_TARGET "-target ")
      set(CMAKE_${lang}_COMPILE_OPTIONS_EXTERNAL_TOOLCHAIN "-gcc-toolchain ")
    else()
      set(CMAKE_${lang}_COMPILE_OPTIONS_TARGET "--target=")
      set(CMAKE_${lang}_COMPILE_OPTIONS_EXTERNAL_TOOLCHAIN "--gcc-toolchain=")
    endif()
    set(CMAKE_${lang}_LINKER_WRAPPER_FLAG "-Xlinker" " ")
    set(CMAKE_${lang}_LINKER_WRAPPER_FLAG_SEP)

    if(CMAKE_${lang}_COMPILER_TARGET)
      if(CMAKE_${lang}_COMPILER_VERSION VERSION_LESS 3.4.0)
        list(APPEND CMAKE_${lang}_COMPILER_PREDEFINES_COMMAND "-target" "${CMAKE_${lang}_COMPILER_TARGET}")
      else()
        list(APPEND CMAKE_${lang}_COMPILER_PREDEFINES_COMMAND "--target=${CMAKE_${lang}_COMPILER_TARGET}")
      endif()
    endif()

    set(_CMAKE_${lang}_IPO_SUPPORTED_BY_CMAKE YES)
    set(_CMAKE_${lang}_IPO_MAY_BE_SUPPORTED_BY_COMPILER YES)

    string(COMPARE EQUAL "${CMAKE_${lang}_COMPILER_ID}" "AppleClang" __is_apple_clang)

    # '-flto=thin' available since Clang 3.9 and Xcode 8
    # * http://clang.llvm.org/docs/ThinLTO.html#clang-llvm
    # * https://trac.macports.org/wiki/XcodeVersionInfo
    set(_CMAKE_LTO_THIN TRUE)
    if(__is_apple_clang)
      if(CMAKE_${lang}_COMPILER_VERSION VERSION_LESS 8.0)
        set(_CMAKE_LTO_THIN FALSE)
      endif()
    else()
      if(CMAKE_${lang}_COMPILER_VERSION VERSION_LESS 3.9)
        set(_CMAKE_LTO_THIN FALSE)
      endif()
    endif()

    if(_CMAKE_LTO_THIN)
      set(CMAKE_${lang}_COMPILE_OPTIONS_IPO "-flto=thin")
    else()
      set(CMAKE_${lang}_COMPILE_OPTIONS_IPO "-flto")
    endif()

    if(ANDROID)
      # https://github.com/android-ndk/ndk/issues/242
      set(CMAKE_${lang}_LINK_OPTIONS_IPO "-fuse-ld=gold")
    endif()

    if(ANDROID OR __is_apple_clang)
      set(__ar "${CMAKE_AR}")
      set(__ranlib "${CMAKE_RANLIB}")
    else()
      set(__ar "${CMAKE_${lang}_COMPILER_AR}")
      set(__ranlib "${CMAKE_${lang}_COMPILER_RANLIB}")
    endif()

    set(CMAKE_${lang}_ARCHIVE_CREATE_IPO
      "\"${__ar}\" cr <TARGET> <LINK_FLAGS> <OBJECTS>"
    )

    set(CMAKE_${lang}_ARCHIVE_APPEND_IPO
      "\"${__ar}\" r <TARGET> <LINK_FLAGS> <OBJECTS>"
    )

    set(CMAKE_${lang}_ARCHIVE_FINISH_IPO
      "\"${__ranlib}\" <TARGET>"
    )

    set(CMAKE_PCH_EXTENSION .pch)
    if (NOT CMAKE_GENERATOR MATCHES "Xcode")
      set(CMAKE_PCH_PROLOGUE "#pragma clang system_header")
    endif()
    if(CMAKE_${lang}_COMPILER_VERSION VERSION_GREATER_EQUAL 11.0.0 AND NOT __is_apple_clang)
      set(CMAKE_${lang}_COMPILE_OPTIONS_INSTANTIATE_TEMPLATES_PCH -fpch-instantiate-templates)
    endif()
    set(CMAKE_${lang}_COMPILE_OPTIONS_USE_PCH -Xclang -include-pch -Xclang <PCH_FILE> -Xclang -include -Xclang <PCH_HEADER>)
    set(CMAKE_${lang}_COMPILE_OPTIONS_CREATE_PCH -Xclang -emit-pch -Xclang -include -Xclang <PCH_HEADER> -x ${__pch_header_${lang}})
  endmacro()
endif()

macro(__compiler_clang_cxx_standards lang)
  if("x${CMAKE_${lang}_COMPILER_FRONTEND_VARIANT}" STREQUAL "xGNU")
    if(NOT CMAKE_${lang}_COMPILER_VERSION VERSION_LESS 2.1)
      set(CMAKE_${lang}98_STANDARD_COMPILE_OPTION "-std=c++98")
      set(CMAKE_${lang}98_EXTENSION_COMPILE_OPTION "-std=gnu++98")
    endif()

    if(NOT CMAKE_${lang}_COMPILER_VERSION VERSION_LESS 3.1)
      set(CMAKE_${lang}98_STANDARD__HAS_FULL_SUPPORT ON)
      set(CMAKE_${lang}11_STANDARD_COMPILE_OPTION "-std=c++11")
      set(CMAKE_${lang}11_EXTENSION_COMPILE_OPTION "-std=gnu++11")
      set(CMAKE_${lang}11_STANDARD__HAS_FULL_SUPPORT ON)
    elseif(NOT CMAKE_${lang}_COMPILER_VERSION VERSION_LESS 2.1)
      set(CMAKE_${lang}11_STANDARD_COMPILE_OPTION "-std=c++0x")
      set(CMAKE_${lang}11_EXTENSION_COMPILE_OPTION "-std=gnu++0x")
    endif()

    if(NOT CMAKE_${lang}_COMPILER_VERSION VERSION_LESS 3.5)
      set(CMAKE_${lang}14_STANDARD_COMPILE_OPTION "-std=c++14")
      set(CMAKE_${lang}14_EXTENSION_COMPILE_OPTION "-std=gnu++14")
      set(CMAKE_${lang}14_STANDARD__HAS_FULL_SUPPORT ON)
    elseif(NOT CMAKE_${lang}_COMPILER_VERSION VERSION_LESS 3.4)
      set(CMAKE_${lang}14_STANDARD_COMPILE_OPTION "-std=c++1y")
      set(CMAKE_${lang}14_EXTENSION_COMPILE_OPTION "-std=gnu++1y")
      set(CMAKE_${lang}14_STANDARD__HAS_FULL_SUPPORT ON)
    endif()

    set(_clang_version_std17 5.0)
    if(CMAKE_SYSTEM_NAME STREQUAL "Android")
      set(_clang_version_std17 6.0)
    endif()

    if (NOT CMAKE_${lang}_COMPILER_VERSION VERSION_LESS "${_clang_version_std17}")
      set(CMAKE_${lang}17_STANDARD_COMPILE_OPTION "-std=c++17")
      set(CMAKE_${lang}17_EXTENSION_COMPILE_OPTION "-std=gnu++17")
    elseif (NOT CMAKE_${lang}_COMPILER_VERSION VERSION_LESS 3.5)
      set(CMAKE_${lang}17_STANDARD_COMPILE_OPTION "-std=c++1z")
      set(CMAKE_${lang}17_EXTENSION_COMPILE_OPTION "-std=gnu++1z")
    endif()

    if(NOT CMAKE_${lang}_COMPILER_VERSION VERSION_LESS 6.0)
      set(CMAKE_${lang}17_STANDARD__HAS_FULL_SUPPORT ON)
    endif()

    if(NOT CMAKE_${lang}_COMPILER_VERSION VERSION_LESS 11.0)
      set(CMAKE_${lang}20_STANDARD_COMPILE_OPTION "-std=c++20")
      set(CMAKE_${lang}20_EXTENSION_COMPILE_OPTION "-std=gnu++20")
    elseif(NOT CMAKE_${lang}_COMPILER_VERSION VERSION_LESS "${_clang_version_std17}")
      set(CMAKE_${lang}20_STANDARD_COMPILE_OPTION "-std=c++2a")
      set(CMAKE_${lang}20_EXTENSION_COMPILE_OPTION "-std=gnu++2a")
    endif()

    unset(_clang_version_std17)

    if("x${CMAKE_${lang}_SIMULATE_ID}" STREQUAL "xMSVC")
      # The MSVC standard library requires C++14, and MSVC itself has no
      # notion of operating in a mode not aware of at least that standard.
      set(CMAKE_${lang}98_STANDARD_COMPILE_OPTION "-std=c++14")
      set(CMAKE_${lang}98_EXTENSION_COMPILE_OPTION "-std=gnu++14")
      set(CMAKE_${lang}11_STANDARD_COMPILE_OPTION "-std=c++14")
      set(CMAKE_${lang}11_EXTENSION_COMPILE_OPTION "-std=gnu++14")

      # This clang++ is missing some features because of MSVC compatibility.
      unset(CMAKE_${lang}11_STANDARD__HAS_FULL_SUPPORT)
      unset(CMAKE_${lang}14_STANDARD__HAS_FULL_SUPPORT)
      unset(CMAKE_${lang}17_STANDARD__HAS_FULL_SUPPORT)
      unset(CMAKE_${lang}20_STANDARD__HAS_FULL_SUPPORT)
    endif()

    __compiler_check_default_language_standard(${lang} 2.1 98)
  elseif(CMAKE_${lang}_COMPILER_VERSION VERSION_GREATER_EQUAL 3.9
      AND CMAKE_${lang}_SIMULATE_VERSION VERSION_GREATER_EQUAL 19.0)
    # This version of clang-cl and the MSVC version it simulates have
    # support for -std: flags.
    set(CMAKE_${lang}98_STANDARD_COMPILE_OPTION "")
    set(CMAKE_${lang}98_EXTENSION_COMPILE_OPTION "")
    set(CMAKE_${lang}98_STANDARD__HAS_FULL_SUPPORT ON)
    set(CMAKE_${lang}11_STANDARD_COMPILE_OPTION "")
    set(CMAKE_${lang}11_EXTENSION_COMPILE_OPTION "")
    set(CMAKE_${lang}14_STANDARD_COMPILE_OPTION "-std:c++14")
    set(CMAKE_${lang}14_EXTENSION_COMPILE_OPTION "-std:c++14")
    if (CMAKE_${lang}_COMPILER_VERSION VERSION_GREATER_EQUAL 6.0)
      set(CMAKE_${lang}17_STANDARD_COMPILE_OPTION "-std:c++17")
      set(CMAKE_${lang}17_EXTENSION_COMPILE_OPTION "-std:c++17")
      set(CMAKE_${lang}20_STANDARD_COMPILE_OPTION "-std:c++latest")
      set(CMAKE_${lang}20_EXTENSION_COMPILE_OPTION "-std:c++latest")
    else()
      set(CMAKE_${lang}17_STANDARD_COMPILE_OPTION "-std:c++latest")
      set(CMAKE_${lang}17_EXTENSION_COMPILE_OPTION "-std:c++latest")
    endif()

    __compiler_check_default_language_standard(${lang} 3.9 14)
  else()
    # This version of clang-cl, or the MSVC version it simulates, does not have
    # language standards.  Set these options as empty strings so the feature
    # test infrastructure can at least check to see if they are defined.
    set(CMAKE_${lang}98_STANDARD_COMPILE_OPTION "")
    set(CMAKE_${lang}98_EXTENSION_COMPILE_OPTION "")
    set(CMAKE_${lang}11_STANDARD_COMPILE_OPTION "")
    set(CMAKE_${lang}11_EXTENSION_COMPILE_OPTION "")
    set(CMAKE_${lang}14_STANDARD_COMPILE_OPTION "")
    set(CMAKE_${lang}14_EXTENSION_COMPILE_OPTION "")
    set(CMAKE_${lang}17_STANDARD_COMPILE_OPTION "")
    set(CMAKE_${lang}17_EXTENSION_COMPILE_OPTION "")
    set(CMAKE_${lang}20_STANDARD_COMPILE_OPTION "")
    set(CMAKE_${lang}20_EXTENSION_COMPILE_OPTION "")

    # There is no meaningful default for this
    set(CMAKE_${lang}_STANDARD_DEFAULT "")

    # There are no compiler modes so we only need to test features once.
    # Override the default macro for this special case.  Pretend that
    # all language standards are available so that at least compilation
    # can be attempted.
    macro(cmake_record_${lang}_compile_features)
      list(APPEND CMAKE_${lang}_COMPILE_FEATURES
        cxx_std_98
        cxx_std_11
        cxx_std_14
        cxx_std_17
        cxx_std_20
        )
      _record_compiler_features(${lang} "" CMAKE_${lang}_COMPILE_FEATURES)
    endmacro()
  endif()
endmacro()
