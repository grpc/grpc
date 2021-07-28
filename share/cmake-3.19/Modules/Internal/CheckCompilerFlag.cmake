# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

include_guard(GLOBAL)
include(Internal/CheckSourceCompiles)
include(CMakeCheckCompilerFlagCommonPatterns)

cmake_policy(PUSH)
cmake_policy(SET CMP0054 NEW) # if() quoted variables not dereferenced
cmake_policy(SET CMP0057 NEW) # if() supports IN_LIST

function(CMAKE_CHECK_COMPILER_FLAG _lang _flag _var)

  if(_lang STREQUAL "C")
    set(_lang_src "int main(void) { return 0; }")
    set(_lang_fail_regex FAIL_REGEX "command[ -]line option .* is valid for .* but not for C")
  elseif(_lang STREQUAL "CXX")
    set(_lang_src "int main() { return 0; }")
    set(_lang_fail_regex FAIL_REGEX "command[ -]line option .* is valid for .* but not for C\\+\\+")
  elseif(_lang STREQUAL "CUDA")
    set(_lang_src "__host__ int main() { return 0; }")
    set(_lang_fail_regex FAIL_REGEX "command[ -]line option .* is valid for .* but not for C\\+\\+" # Host GNU
                         FAIL_REGEX "argument unused during compilation: .*") # Clang
  elseif(_lang STREQUAL "Fortran")
    set(_lang_src "       program test\n       stop\n       end program")
    set(_lang_fail_regex FAIL_REGEX "command[ -]line option .* is valid for .* but not for Fortran")
  elseif(_lang STREQUAL "OBJC")
    set(_lang_src [=[
#ifndef __OBJC__
#  error "Not an Objective-C compiler"
#endif
int main(void) { return 0; }]=])
    set(_lang_fail_regex FAIL_REGEX "command[ -]line option .* is valid for .* but not for Objective-C" # GNU
                         FAIL_REGEX "argument unused during compilation: .*") # Clang
  elseif(_lang STREQUAL "OBJCXX")
    set(_lang_src [=[
#ifndef __OBJC__
#  error "Not an Objective-C++ compiler"
#endif
int main(void) { return 0; }]=])
    set(_lang_fail_regex FAIL_REGEX "command[ -]line option .* is valid for .* but not for Objective-C\\+\\+" # GNU
                         FAIL_REGEX "argument unused during compilation: .*") # Clang
  elseif(_lang STREQUAL "ISPC")
    set(_lang_src "float func(uniform int32, float a) { return a / 2.25; }")
  else()
    message (SEND_ERROR "check_compiler_flag: ${_lang}: unknown language.")
    return()
  endif()

  get_property (_supported_languages GLOBAL PROPERTY ENABLED_LANGUAGES)
  if (NOT _lang IN_LIST _supported_languages)
    message (SEND_ERROR "check_compiler_flag: ${_lang}: needs to be enabled before use.")
    return()
  endif()

  set(CMAKE_REQUIRED_DEFINITIONS ${_flag})

  # Normalize locale during test compilation.
  set(_locale_vars LC_ALL LC_MESSAGES LANG)
  foreach(v IN LISTS _locale_vars)
    set(_locale_vars_saved_${v} "$ENV{${v}}")
    set(ENV{${v}} C)
  endforeach()

  check_compiler_flag_common_patterns(_common_patterns)
  cmake_check_source_compiles(${_lang}
    "${_lang_src}"
    ${_var}
    ${_lang_fail_regex}
    ${_common_patterns}
    )

  foreach(v IN LISTS _locale_vars)
    set(ENV{${v}} ${_locale_vars_saved_${v}})
  endforeach()
  set(${_var} "${${_var}}" PARENT_SCOPE)
endfunction ()

cmake_policy(POP)
