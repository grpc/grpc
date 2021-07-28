# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

include_guard(GLOBAL)

cmake_policy(PUSH)
cmake_policy(SET CMP0054 NEW) # if() quoted variables not dereferenced
cmake_policy(SET CMP0057 NEW) # if() supports IN_LIST

function(CMAKE_CHECK_SOURCE_RUNS _lang _source _var)
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
    elseif(_lang STREQUAL "OBJC")
      set(_lang_textual "Objective-C")
      set(_lang_ext "m")
    elseif(_lang STREQUAL "OBJCXX")
      set(_lang_textual "Objective-C++")
      set(_lang_ext "mm")
    else()
      message (SEND_ERROR "check_source_runs: ${_lang}: unknown language.")
      return()
    endif()

    get_property (_supported_languages GLOBAL PROPERTY ENABLED_LANGUAGES)
    if (NOT _lang IN_LIST _supported_languages)
      message (SEND_ERROR "check_source_runs: ${_lang}: needs to be enabled before use.")
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
        set(message_type FATAL_ERROR)
        if (_CheckSourceRuns_old_signature)
          set(message_type AUTHOR_WARNING)
        endif ()
        message("${message_type}" "Unknown argument:\n  ${arg}\n")
        unset(message_type)
      endif()
    endforeach()

    if(NOT _SRC_EXT)
      set(_SRC_EXT ${_lang_ext})
    endif()

    if(CMAKE_REQUIRED_LINK_OPTIONS)
      set(CHECK_${_lang}_SOURCE_COMPILES_ADD_LINK_OPTIONS
        LINK_OPTIONS ${CMAKE_REQUIRED_LINK_OPTIONS})
    else()
      set(CHECK_${_lang}_SOURCE_COMPILES_ADD_LINK_OPTIONS)
    endif()
    if(CMAKE_REQUIRED_LIBRARIES)
      set(CHECK_${_lang}_SOURCE_COMPILES_ADD_LIBRARIES
        LINK_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES})
    else()
      set(CHECK_${_lang}_SOURCE_COMPILES_ADD_LIBRARIES)
    endif()
    if(CMAKE_REQUIRED_INCLUDES)
      set(CHECK_${_lang}_SOURCE_COMPILES_ADD_INCLUDES
        "-DINCLUDE_DIRECTORIES:STRING=${CMAKE_REQUIRED_INCLUDES}")
    else()
      set(CHECK_${_lang}_SOURCE_COMPILES_ADD_INCLUDES)
    endif()
    file(WRITE "${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp/src.${_SRC_EXT}"
      "${_source}\n")

    if(NOT CMAKE_REQUIRED_QUIET)
      message(CHECK_START "Performing Test ${_var}")
    endif()
    try_run(${_var}_EXITCODE ${_var}_COMPILED
      ${CMAKE_BINARY_DIR}
      ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp/src.${_SRC_EXT}
      COMPILE_DEFINITIONS -D${_var} ${CMAKE_REQUIRED_DEFINITIONS}
      ${CHECK_${_lang}_SOURCE_COMPILES_ADD_LINK_OPTIONS}
      ${CHECK_${_lang}_SOURCE_COMPILES_ADD_LIBRARIES}
      CMAKE_FLAGS -DCOMPILE_DEFINITIONS:STRING=${CMAKE_REQUIRED_FLAGS}
      -DCMAKE_SKIP_RPATH:BOOL=${CMAKE_SKIP_RPATH}
      "${CHECK_${_lang}_SOURCE_COMPILES_ADD_INCLUDES}"
      COMPILE_OUTPUT_VARIABLE OUTPUT
      RUN_OUTPUT_VARIABLE RUN_OUTPUT)
    # if it did not compile make the return value fail code of 1
    if(NOT ${_var}_COMPILED)
      set(${_var}_EXITCODE 1)
      set(${_var}_EXITCODE 1 PARENT_SCOPE)
    endif()
    # if the return value was 0 then it worked
    if("${${_var}_EXITCODE}" EQUAL 0)
      set(${_var} 1 CACHE INTERNAL "Test ${_var}")
      if(NOT CMAKE_REQUIRED_QUIET)
        message(CHECK_PASS "Success")
      endif()
      file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
        "Performing ${_lang_textual} SOURCE FILE Test ${_var} succeeded with the following compile output:\n"
        "${OUTPUT}\n"
        "...and run output:\n"
        "${RUN_OUTPUT}\n"
        "Return value: ${${_var}}\n"
        "Source file was:\n${_source}\n")
    else()
      if(CMAKE_CROSSCOMPILING AND "${${_var}_EXITCODE}" MATCHES  "FAILED_TO_RUN")
        set(${_var} "${${_var}_EXITCODE}" PARENT_SCOPE)
      else()
        set(${_var} "" CACHE INTERNAL "Test ${_var}")
      endif()

      if(NOT CMAKE_REQUIRED_QUIET)
        message(CHECK_FAIL "Failed")
      endif()
      file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
        "Performing ${_lang_textual} SOURCE FILE Test ${_var} failed with the following compile output:\n"
        "${OUTPUT}\n"
        "...and run output:\n"
        "${RUN_OUTPUT}\n"
        "Return value: ${${_var}_EXITCODE}\n"
        "Source file was:\n${_source}\n")

    endif()
  endif()
endfunction()

cmake_policy(POP)
