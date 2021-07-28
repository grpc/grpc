# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
CheckIncludeFiles
-----------------

Provides a macro to check if a list of one or more header files can
be included together.

.. command:: CHECK_INCLUDE_FILES

  .. code-block:: cmake

    CHECK_INCLUDE_FILES("<includes>" <variable> [LANGUAGE <language>])

  Check if the given ``<includes>`` list may be included together
  in a source file and store the result in an internal cache
  entry named ``<variable>``.  Specify the ``<includes>`` argument
  as a :ref:`;-list <CMake Language Lists>` of header file names.

  If ``LANGUAGE`` is set, the specified compiler will be used to perform the
  check. Acceptable values are ``C`` and ``CXX``. If not set, the C compiler
  will be used if enabled. If the C compiler is not enabled, the C++
  compiler will be used if enabled.

The following variables may be set before calling this macro to modify
the way the check is run:

``CMAKE_REQUIRED_FLAGS``
  string of compile command line flags.
``CMAKE_REQUIRED_DEFINITIONS``
  a :ref:`;-list <CMake Language Lists>` of macros to define (-DFOO=bar).
``CMAKE_REQUIRED_INCLUDES``
  a :ref:`;-list <CMake Language Lists>` of header search paths to pass to
  the compiler.
``CMAKE_REQUIRED_LINK_OPTIONS``
  a :ref:`;-list <CMake Language Lists>` of options to add to the link command.
``CMAKE_REQUIRED_LIBRARIES``
  a :ref:`;-list <CMake Language Lists>` of libraries to add to the link
  command. See policy :policy:`CMP0075`.
``CMAKE_REQUIRED_QUIET``
  execute quietly without messages.

See modules :module:`CheckIncludeFile` and :module:`CheckIncludeFileCXX`
to check for a single header file in ``C`` or ``CXX`` languages.
#]=======================================================================]

include_guard(GLOBAL)

macro(CHECK_INCLUDE_FILES INCLUDE VARIABLE)
  if(NOT DEFINED "${VARIABLE}")
    set(CMAKE_CONFIGURABLE_FILE_CONTENT "/* */\n")

    if("x${ARGN}" STREQUAL "x")
       if(CMAKE_C_COMPILER_LOADED)
         set(_lang C)
       elseif(CMAKE_CXX_COMPILER_LOADED)
         set(_lang CXX)
       else()
         message(FATAL_ERROR "CHECK_INCLUDE_FILES needs either C or CXX language enabled.\n")
       endif()
    elseif("x${ARGN}" MATCHES "^xLANGUAGE;([a-zA-Z]+)$")
      set(_lang "${CMAKE_MATCH_1}")
    elseif("x${ARGN}" MATCHES "^xLANGUAGE$")
      message(FATAL_ERROR "No languages listed for LANGUAGE option.\nSupported languages: C, CXX.\n")
    else()
      message(FATAL_ERROR "Unknown arguments:\n  ${ARGN}\n")
    endif()

    if(_lang STREQUAL "C")
      set(src ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CheckIncludeFiles/${VARIABLE}.c)
    elseif(_lang STREQUAL "CXX")
      set(src ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CheckIncludeFiles/${VARIABLE}.cpp)
    else()
      message(FATAL_ERROR "Unknown language:\n  ${_lang}\nSupported languages: C, CXX.\n")
    endif()

    if(CMAKE_REQUIRED_INCLUDES)
      set(CHECK_INCLUDE_FILES_INCLUDE_DIRS "-DINCLUDE_DIRECTORIES=${CMAKE_REQUIRED_INCLUDES}")
    else()
      set(CHECK_INCLUDE_FILES_INCLUDE_DIRS)
    endif()
    set(CHECK_INCLUDE_FILES_CONTENT "/* */\n")
    set(MACRO_CHECK_INCLUDE_FILES_FLAGS ${CMAKE_REQUIRED_FLAGS})
    foreach(FILE ${INCLUDE})
      string(APPEND CMAKE_CONFIGURABLE_FILE_CONTENT
        "#include <${FILE}>\n")
    endforeach()
    string(APPEND CMAKE_CONFIGURABLE_FILE_CONTENT
      "\n\nint main(void){return 0;}\n")
    configure_file("${CMAKE_ROOT}/Modules/CMakeConfigurableFile.in"
      "${src}" @ONLY)

    set(_INCLUDE ${INCLUDE}) # remove empty elements
    if("${_INCLUDE}" MATCHES "^([^;]+);.+;([^;]+)$")
      list(LENGTH _INCLUDE _INCLUDE_LEN)
      set(_description "${_INCLUDE_LEN} include files ${CMAKE_MATCH_1}, ..., ${CMAKE_MATCH_2}")
    elseif("${_INCLUDE}" MATCHES "^([^;]+);([^;]+)$")
      set(_description "include files ${CMAKE_MATCH_1}, ${CMAKE_MATCH_2}")
    else()
      set(_description "include file ${_INCLUDE}")
    endif()

    set(_CIF_LINK_OPTIONS)
    if(CMAKE_REQUIRED_LINK_OPTIONS)
      set(_CIF_LINK_OPTIONS LINK_OPTIONS ${CMAKE_REQUIRED_LINK_OPTIONS})
    endif()

    set(_CIF_LINK_LIBRARIES "")
    if(CMAKE_REQUIRED_LIBRARIES)
      cmake_policy(GET CMP0075 _CIF_CMP0075
        PARENT_SCOPE # undocumented, do not use outside of CMake
        )
      if("x${_CIF_CMP0075}x" STREQUAL "xNEWx")
        set(_CIF_LINK_LIBRARIES LINK_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES})
      elseif("x${_CIF_CMP0075}x" STREQUAL "xOLDx")
      elseif(NOT _CIF_CMP0075_WARNED)
        set(_CIF_CMP0075_WARNED 1)
        message(AUTHOR_WARNING
          "Policy CMP0075 is not set: Include file check macros honor CMAKE_REQUIRED_LIBRARIES.  "
          "Run \"cmake --help-policy CMP0075\" for policy details.  "
          "Use the cmake_policy command to set the policy and suppress this warning."
          "\n"
          "CMAKE_REQUIRED_LIBRARIES is set to:\n"
          "  ${CMAKE_REQUIRED_LIBRARIES}\n"
          "For compatibility with CMake 3.11 and below this check is ignoring it."
          )
      endif()
      unset(_CIF_CMP0075)
    endif()

    if(NOT CMAKE_REQUIRED_QUIET)
      message(CHECK_START "Looking for ${_description}")
    endif()
    try_compile(${VARIABLE}
      ${CMAKE_BINARY_DIR}
      ${src}
      COMPILE_DEFINITIONS ${CMAKE_REQUIRED_DEFINITIONS}
      ${_CIF_LINK_OPTIONS}
      ${_CIF_LINK_LIBRARIES}
      CMAKE_FLAGS
      -DCOMPILE_DEFINITIONS:STRING=${MACRO_CHECK_INCLUDE_FILES_FLAGS}
      "${CHECK_INCLUDE_FILES_INCLUDE_DIRS}"
      OUTPUT_VARIABLE OUTPUT)
    unset(_CIF_LINK_OPTIONS)
    unset(_CIF_LINK_LIBRARIES)
    if(${VARIABLE})
      if(NOT CMAKE_REQUIRED_QUIET)
        message(CHECK_PASS "found")
      endif()
      set(${VARIABLE} 1 CACHE INTERNAL "Have include ${INCLUDE}")
      file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
        "Determining if files ${INCLUDE} "
        "exist passed with the following output:\n"
        "${OUTPUT}\n\n")
    else()
      if(NOT CMAKE_REQUIRED_QUIET)
        message(CHECK_FAIL "not found")
      endif()
      set(${VARIABLE} "" CACHE INTERNAL "Have includes ${INCLUDE}")
      file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
        "Determining if files ${INCLUDE} "
        "exist failed with the following output:\n"
        "${OUTPUT}\nSource:\n${CMAKE_CONFIGURABLE_FILE_CONTENT}\n")
    endif()
  endif()
endmacro()
