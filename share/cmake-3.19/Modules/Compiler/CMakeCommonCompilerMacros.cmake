# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

# This module is shared by multiple languages and compilers; use include guard
if (__COMPILER_CMAKE_COMMON_COMPILER_MACROS)
  return()
endif ()
set(__COMPILER_CMAKE_COMMON_COMPILER_MACROS 1)


# Check that a compiler's language standard is properly detected
# Parameters:
#   lang   - Language to check
#   stdver1 - Minimum version to set a given default for
#   std1    - Default to use for compiler ver >= stdver1
#   stdverN - Minimum version to set a given default for
#   stdN    - Default to use for compiler ver >= stdverN
#
#   The order of stdverN stdN pairs passed as arguments is expected to be in
#   monotonically increasing version order.
#
# Note:
#   This macro can be called with multiple version / std pairs to convey that
#   newer compiler versions may use a newer standard default.
#
# Example:
#   To specify that compiler version 6.1 and newer defaults to C++11 while
#   4.8 <= ver < 6.1 default to C++98, you would call:
#
# __compiler_check_default_language_standard(CXX 4.8 98 6.1 11)
#
macro(__compiler_check_default_language_standard lang stdver1 std1)
  set(__std_ver_pairs "${stdver1};${std1};${ARGN}")
  string(REGEX REPLACE " *; *" " " __std_ver_pairs "${__std_ver_pairs}")
  string(REGEX MATCHALL "[^ ]+ [^ ]+" __std_ver_pairs "${__std_ver_pairs}")

  # If the compiler version is below the threshold of even having CMake
  # support for language standards, then don't bother.
  if (CMAKE_${lang}_COMPILER_VERSION VERSION_GREATER_EQUAL "${stdver1}")
    if (NOT CMAKE_${lang}_COMPILER_FORCED)
      if (NOT CMAKE_${lang}_STANDARD_COMPUTED_DEFAULT)
        message(FATAL_ERROR "CMAKE_${lang}_STANDARD_COMPUTED_DEFAULT should be set for ${CMAKE_${lang}_COMPILER_ID} (${CMAKE_${lang}_COMPILER}) version ${CMAKE_${lang}_COMPILER_VERSION}")
      endif ()
      set(CMAKE_${lang}_STANDARD_DEFAULT ${CMAKE_${lang}_STANDARD_COMPUTED_DEFAULT})
    else ()
      list(REVERSE __std_ver_pairs)
      foreach (__std_ver_pair IN LISTS __std_ver_pairs)
        string(REGEX MATCH "([^ ]+) (.+)" __std_ver_pair "${__std_ver_pair}")
        set(__stdver ${CMAKE_MATCH_1})
        set(__std ${CMAKE_MATCH_2})
        if (CMAKE_${lang}_COMPILER_VERSION VERSION_GREATER_EQUAL __stdver AND
          NOT DEFINED CMAKE_${lang}_STANDARD_DEFAULT)
          # Compiler id was forced so just guess the default standard level.
          set(CMAKE_${lang}_STANDARD_DEFAULT ${__std})
        endif ()
        unset(__std)
        unset(__stdver)
      endforeach ()
    endif ()
  endif ()
  unset(__std_ver_pairs)
endmacro()

# Define to allow compile features to be automatically determined
macro(cmake_record_c_compile_features)
  set(_result 0)
  if(_result EQUAL 0 AND DEFINED CMAKE_C11_STANDARD_COMPILE_OPTION)
    if(CMAKE_C11_STANDARD__HAS_FULL_SUPPORT)
      _has_compiler_features_c(11)
    else()
      _record_compiler_features_c(11)
    endif()
    unset(CMAKE_C11_STANDARD__HAS_FULL_SUPPORT)
  endif()
  if(_result EQUAL 0 AND DEFINED CMAKE_C99_STANDARD_COMPILE_OPTION)
    if(CMAKE_C99_STANDARD__HAS_FULL_SUPPORT)
      _has_compiler_features_c(99)
    else()
      _record_compiler_features_c(99)
    endif()
    unset(CMAKE_C99_STANDARD__HAS_FULL_SUPPORT)
  endif()
  if(_result EQUAL 0 AND DEFINED CMAKE_C90_STANDARD_COMPILE_OPTION)
    if(CMAKE_C90_STANDARD__HAS_FULL_SUPPORT)
      _has_compiler_features_c(90)
    else()
      _record_compiler_features_c(90)
    endif()
    unset(CMAKE_C90_STANDARD__HAS_FULL_SUPPORT)
  endif()
endmacro()

# Define to allow compile features to be automatically determined
macro(cmake_record_cxx_compile_features)
  set(_result 0)
  if(_result EQUAL 0 AND DEFINED CMAKE_CXX20_STANDARD_COMPILE_OPTION)
    if(CMAKE_CXX20_STANDARD__HAS_FULL_SUPPORT)
      _has_compiler_features_cxx(20)
    else()
      _record_compiler_features_cxx(20)
    endif()
    unset(CMAKE_CXX20_STANDARD__HAS_FULL_SUPPORT)
  endif()
  if(_result EQUAL 0 AND DEFINED CMAKE_CXX17_STANDARD_COMPILE_OPTION)
    if(CMAKE_CXX17_STANDARD__HAS_FULL_SUPPORT)
      _has_compiler_features_cxx(17)
    else()
      _record_compiler_features_cxx(17)
    endif()
    unset(CMAKE_CXX17_STANDARD__HAS_FULL_SUPPORT)
  endif()
  if(_result EQUAL 0 AND DEFINED CMAKE_CXX14_STANDARD_COMPILE_OPTION)
    if(CMAKE_CXX14_STANDARD__HAS_FULL_SUPPORT)
      _has_compiler_features_cxx(14)
    else()
      _record_compiler_features_cxx(14)
    endif()
    unset(CMAKE_CXX14_STANDARD__HAS_FULL_SUPPORT)
  endif()
  if(_result EQUAL 0 AND DEFINED CMAKE_CXX11_STANDARD_COMPILE_OPTION)
    if(CMAKE_CXX11_STANDARD__HAS_FULL_SUPPORT)
      _has_compiler_features_cxx(11)
    else()
      _record_compiler_features_cxx(11)
    endif()
    unset(CMAKE_CXX11_STANDARD__HAS_FULL_SUPPORT)
  endif()
  if(_result EQUAL 0 AND DEFINED CMAKE_CXX98_STANDARD_COMPILE_OPTION)
    if(CMAKE_CXX98_STANDARD__HAS_FULL_SUPPORT)
      _has_compiler_features_cxx(98)
    else()
      _record_compiler_features_cxx(98)
    endif()
    unset(CMAKE_CXX98_STANDARD__HAS_FULL_SUPPORT)
  endif()
endmacro()

macro(cmake_record_cuda_compile_features)
  set(_result 0)
  if(_result EQUAL 0 AND DEFINED CMAKE_CUDA20_STANDARD_COMPILE_OPTION)
    if(CMAKE_CUDA20_STANDARD__HAS_FULL_SUPPORT)
      _has_compiler_features_cuda(20)
    else()
      _record_compiler_features_cuda(20)
    endif()
    unset(CMAKE_CUDA20_STANDARD__HAS_FULL_SUPPORT)
  endif()
  if(_result EQUAL 0 AND DEFINED CMAKE_CUDA17_STANDARD_COMPILE_OPTION)
    if(CMAKE_CUDA17_STANDARD__HAS_FULL_SUPPORT)
      _has_compiler_features_cuda(17)
    else()
      _record_compiler_features_cuda(17)
    endif()
    unset(CMAKE_CUDA17_STANDARD__HAS_FULL_SUPPORT)
  endif()
  if(_result EQUAL 0 AND DEFINED CMAKE_CUDA14_STANDARD_COMPILE_OPTION)
    if(CMAKE_CUDA14_STANDARD__HAS_FULL_SUPPORT)
      _has_compiler_features_cuda(14)
    else()
      _record_compiler_features_cuda(14)
    endif()
    unset(CMAKE_CUDA14_STANDARD__HAS_FULL_SUPPORT)
  endif()
  if(_result EQUAL 0 AND DEFINED CMAKE_CUDA11_STANDARD_COMPILE_OPTION)
    if(CMAKE_CUDA11_STANDARD__HAS_FULL_SUPPORT)
      _has_compiler_features_cuda(11)
    else()
      _record_compiler_features_cuda(11)
    endif()
    unset(CMAKE_CUDA11_STANDARD__HAS_FULL_SUPPORT)
  endif()
  if(_result EQUAL 0 AND DEFINED CMAKE_CUDA03_STANDARD_COMPILE_OPTION)
    if(CMAKE_CUDA03_STANDARD__HAS_FULL_SUPPORT)
      _has_compiler_features_cuda(03)
    else()
      _record_compiler_features_cuda(03)
    endif()
    unset(CMAKE_CUDA03_STANDARD__HAS_FULL_SUPPORT)
  endif()
endmacro()
