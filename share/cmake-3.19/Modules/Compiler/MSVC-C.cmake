# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

include(Compiler/CMakeCommonCompilerMacros)

if(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 19.27)
  set(CMAKE_C90_STANDARD_COMPILE_OPTION "")
  set(CMAKE_C90_EXTENSION_COMPILE_OPTION "")
  set(CMAKE_C99_STANDARD_COMPILE_OPTION "")
  set(CMAKE_C99_EXTENSION_COMPILE_OPTION "")
  set(CMAKE_C11_STANDARD_COMPILE_OPTION "-std:c11")
  set(CMAKE_C11_EXTENSION_COMPILE_OPTION "-std:c11")

  __compiler_check_default_language_standard(C 19.27 99)
else()
  # MSVC has no specific options to set C language standards, but set them as
  # empty strings anyways so the feature test infrastructure can at least check
  # to see if they are defined.
  set(CMAKE_C90_STANDARD_COMPILE_OPTION "")
  set(CMAKE_C90_EXTENSION_COMPILE_OPTION "")
  set(CMAKE_C99_STANDARD_COMPILE_OPTION "")
  set(CMAKE_C99_EXTENSION_COMPILE_OPTION "")
  set(CMAKE_C11_STANDARD_COMPILE_OPTION "")
  set(CMAKE_C11_EXTENSION_COMPILE_OPTION "")

  # There is no meaningful default for this
  set(CMAKE_C_STANDARD_DEFAULT "")
endif()

set(CMAKE_C_CLANG_TIDY_DRIVER_MODE "cl")

# There are no C compiler modes so we hard-code the known compiler supported
# features. Override the default macro for this special case.  Pretend that
# all language standards are available so that at least compilation
# can be attempted.
macro(cmake_record_c_compile_features)
  list(APPEND CMAKE_C_COMPILE_FEATURES
    c_std_90
    c_std_99
    c_std_11
    c_function_prototypes
    )
  list(APPEND CMAKE_C90_COMPILE_FEATURES c_std_90 c_function_prototypes)
  list(APPEND CMAKE_C99_COMPILE_FEATURES c_std_99)
  list(APPEND CMAKE_C11_COMPILE_FEATURES c_std_11)
  if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 14.0)
    list(APPEND CMAKE_C_COMPILE_FEATURES c_variadic_macros)
    list(APPEND CMAKE_C99_COMPILE_FEATURES c_variadic_macros)
  endif()
  if(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 19.27)
    list(APPEND CMAKE_C_COMPILE_FEATURES c_restrict)
    list(APPEND CMAKE_C99_COMPILE_FEATURES c_restrict)
  endif()
  if(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 19.28)
    list(APPEND CMAKE_C_COMPILE_FEATURES c_static_assert)
    list(APPEND CMAKE_C11_COMPILE_FEATURES c_static_assert)
  endif()
  set(_result 0) # expected by cmake_determine_compile_features
endmacro()

# /JMC "Just My Code" is only supported by MSVC 19.05 onward.
if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 19.05)
  set(CMAKE_C_COMPILE_OPTIONS_JMC "-JMC")
endif()
