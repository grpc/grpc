# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

include_guard(GLOBAL)

cmake_policy(PUSH)
cmake_policy(SET CMP0054 NEW) # if() quoted variables not dereferenced
cmake_policy(SET CMP0057 NEW) # if() supports IN_LIST

function(CMAKE_CHECK_SOURCE_COMPILES _lang _source _var)
  if(NOT DEFINED "${_var}")

    if(_lang STREQUAL "C")
      set(_lang_textual "C")
      set(_lang_ext "c")
    elseif(_lang STREQUAL "CXX")
      set(_lang_textual "C++")
      set(_lang_ext "cxx")
    elseif(_lang STREQUAL "CUDA")
      set(_lang_textual "CUDA")
      set(_lang_ext "cu")
    elseif(_lang STREQUAL "Fortran")
      set(_lang_textual "Fortran")
      set(_lang_ext "F90")
    elseif(_lang STREQUAL "ISPC")
      set(_lang_textual "ISPC")
      set(_lang_ext "ispc")
    elseif(_lang STREQUAL "OBJC")
      set(_lang_textual "Objective-C")
      set(_lang_ext "m")
    elseif(_lang STREQUAL "OBJCXX")
      set(_lang_textual "Objective-C++")
      set(_lang_ext "mm")
    else()
      message (SEND_ERROR "check_source_compiles: ${_lang}: unknown language.")
      return()
    endif()

    get_property (_supported_languages GLOBAL PROPERTY ENABLED_LANGUAGES)
    if (NOT _lang IN_LIST _supported_languages)
      message (SEND_ERROR "check_source_compiles: ${_lang}: needs to be enabled before use.")
      return()
    endif()

    set(_FAIL_REGEX)
    set(_SRC_EXT)
    set(_key)
    foreach(arg ${ARGN})
      if("${arg}" MATCHES "^(FAIL_REGEX|SRC_EXT)$")
        set(_key "${arg}")
      elseif(_key STREQUAL "FAIL_REGEX")
        list(APPEND _FAIL_REGEX "${arg}")
      elseif(_key STREQUAL "SRC_EXT")
        set(_SRC_EXT "${arg}")
        set(_key "")
      else()
        message(FATAL_ERROR "Unknown argument:\n  ${arg}\n")
      endif()
    endforeach()

    if(NOT _SRC_EXT)
      set(_SRC_EXT ${_lang_ext})
    endif()

    if(CMAKE_REQUIRED_LINK_OPTIONS)
      set(CHECK_${LANG}_SOURCE_COMPILES_ADD_LINK_OPTIONS
        LINK_OPTIONS ${CMAKE_REQUIRED_LINK_OPTIONS})
    else()
      set(CHECK_${LANG}_SOURCE_COMPILES_ADD_LINK_OPTIONS)
    endif()
    if(CMAKE_REQUIRED_LIBRARIES)
      set(CHECK_${LANG}_SOURCE_COMPILES_ADD_LIBRARIES
        LINK_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES})
    else()
      set(CHECK_${LANG}_SOURCE_COMPILES_ADD_LIBRARIES)
    endif()
    if(CMAKE_REQUIRED_INCLUDES)
      set(CHECK_${LANG}_SOURCE_COMPILES_ADD_INCLUDES
        "-DINCLUDE_DIRECTORIES:STRING=${CMAKE_REQUIRED_INCLUDES}")
    else()
      set(CHECK_${LANG}_SOURCE_COMPILES_ADD_INCLUDES)
    endif()
    file(WRITE "${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp/src.${_SRC_EXT}"
      "${_source}\n")

    if(NOT CMAKE_REQUIRED_QUIET)
      message(CHECK_START "Performing Test ${_var}")
    endif()
    try_compile(${_var}
      ${CMAKE_BINARY_DIR}
      ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp/src.${_SRC_EXT}
      COMPILE_DEFINITIONS -D${_var} ${CMAKE_REQUIRED_DEFINITIONS}
      ${CHECK_${LANG}_SOURCE_COMPILES_ADD_LINK_OPTIONS}
      ${CHECK_${LANG}_SOURCE_COMPILES_ADD_LIBRARIES}
      CMAKE_FLAGS -DCOMPILE_DEFINITIONS:STRING=${CMAKE_REQUIRED_FLAGS}
      "${CHECK_${LANG}_SOURCE_COMPILES_ADD_INCLUDES}"
      OUTPUT_VARIABLE OUTPUT)

    foreach(_regex ${_FAIL_REGEX})
      if("${OUTPUT}" MATCHES "${_regex}")
        set(${_var} 0)
      endif()
    endforeach()

    if(${_var})
      set(${_var} 1 CACHE INTERNAL "Test ${_var}")
      if(NOT CMAKE_REQUIRED_QUIET)
        message(CHECK_PASS "Success")
      endif()
      file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
        "Performing ${_lang_textual} SOURCE FILE Test ${_var} succeeded with the following output:\n"
        "${OUTPUT}\n"
        "Source file was:\n${_source}\n")
    else()
      if(NOT CMAKE_REQUIRED_QUIET)
        message(CHECK_FAIL "Failed")
      endif()
      set(${_var} "" CACHE INTERNAL "Test ${_var}")
      file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
        "Performing ${_lang_textual} SOURCE FILE Test ${_var} failed with the following output:\n"
        "${OUTPUT}\n"
        "Source file was:\n${_source}\n")
    endif()
  endif()
endfunction()

cmake_policy(POP)
