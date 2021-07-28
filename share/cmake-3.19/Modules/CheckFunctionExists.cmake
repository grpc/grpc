# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
CheckFunctionExists
-------------------

Check if a C function can be linked

.. command:: check_function_exists

  .. code-block:: cmake

    check_function_exists(<function> <variable>)

  Checks that the ``<function>`` is provided by libraries on the system and store
  the result in a ``<variable>``, which will be created as an internal
  cache variable.

The following variables may be set before calling this macro to modify the
way the check is run:

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

.. note::

  Prefer using :Module:`CheckSymbolExists` instead of this module,
  for the following reasons:

  * ``check_function_exists()`` can't detect functions that are inlined
    in headers or specified as a macro.

  * ``check_function_exists()`` can't detect anything in the 32-bit
    versions of the Win32 API, because of a mismatch in calling conventions.

  * ``check_function_exists()`` only verifies linking, it does not verify
    that the function is declared in system headers.
#]=======================================================================]

include_guard(GLOBAL)

macro(CHECK_FUNCTION_EXISTS FUNCTION VARIABLE)
  if(NOT DEFINED "${VARIABLE}" OR "x${${VARIABLE}}" STREQUAL "x${VARIABLE}")
    set(MACRO_CHECK_FUNCTION_DEFINITIONS
      "-DCHECK_FUNCTION_EXISTS=${FUNCTION} ${CMAKE_REQUIRED_FLAGS}")
    if(NOT CMAKE_REQUIRED_QUIET)
      message(CHECK_START "Looking for ${FUNCTION}")
    endif()
    if(CMAKE_REQUIRED_LINK_OPTIONS)
      set(CHECK_FUNCTION_EXISTS_ADD_LINK_OPTIONS
        LINK_OPTIONS ${CMAKE_REQUIRED_LINK_OPTIONS})
    else()
      set(CHECK_FUNCTION_EXISTS_ADD_LINK_OPTIONS)
    endif()
    if(CMAKE_REQUIRED_LIBRARIES)
      set(CHECK_FUNCTION_EXISTS_ADD_LIBRARIES
        LINK_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES})
    else()
      set(CHECK_FUNCTION_EXISTS_ADD_LIBRARIES)
    endif()
    if(CMAKE_REQUIRED_INCLUDES)
      set(CHECK_FUNCTION_EXISTS_ADD_INCLUDES
        "-DINCLUDE_DIRECTORIES:STRING=${CMAKE_REQUIRED_INCLUDES}")
    else()
      set(CHECK_FUNCTION_EXISTS_ADD_INCLUDES)
    endif()

    if(CMAKE_C_COMPILER_LOADED)
      set(_cfe_source ${CMAKE_ROOT}/Modules/CheckFunctionExists.c)
    elseif(CMAKE_CXX_COMPILER_LOADED)
      set(_cfe_source ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CheckFunctionExists/CheckFunctionExists.cxx)
      configure_file(${CMAKE_ROOT}/Modules/CheckFunctionExists.c "${_cfe_source}" COPYONLY)
    else()
      message(FATAL_ERROR "CHECK_FUNCTION_EXISTS needs either C or CXX language enabled")
    endif()

    try_compile(${VARIABLE}
      ${CMAKE_BINARY_DIR}
      ${_cfe_source}
      COMPILE_DEFINITIONS ${CMAKE_REQUIRED_DEFINITIONS}
      ${CHECK_FUNCTION_EXISTS_ADD_LINK_OPTIONS}
      ${CHECK_FUNCTION_EXISTS_ADD_LIBRARIES}
      CMAKE_FLAGS -DCOMPILE_DEFINITIONS:STRING=${MACRO_CHECK_FUNCTION_DEFINITIONS}
      "${CHECK_FUNCTION_EXISTS_ADD_INCLUDES}"
      OUTPUT_VARIABLE OUTPUT)
    unset(_cfe_source)

    if(${VARIABLE})
      set(${VARIABLE} 1 CACHE INTERNAL "Have function ${FUNCTION}")
      if(NOT CMAKE_REQUIRED_QUIET)
        message(CHECK_PASS "found")
      endif()
      file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
        "Determining if the function ${FUNCTION} exists passed with the following output:\n"
        "${OUTPUT}\n\n")
    else()
      if(NOT CMAKE_REQUIRED_QUIET)
        message(CHECK_FAIL "not found")
      endif()
      set(${VARIABLE} "" CACHE INTERNAL "Have function ${FUNCTION}")
      file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
        "Determining if the function ${FUNCTION} exists failed with the following output:\n"
        "${OUTPUT}\n\n")
    endif()
  endif()
endmacro()
