# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
CheckVariableExists
-------------------

Check if the variable exists.

.. command:: CHECK_VARIABLE_EXISTS

  .. code-block:: cmake

    CHECK_VARIABLE_EXISTS(VAR VARIABLE)


  ::

    VAR      - the name of the variable
    VARIABLE - variable to store the result
               Will be created as an internal cache variable.


  This macro is only for ``C`` variables.

The following variables may be set before calling this macro to modify
the way the check is run:

::

  CMAKE_REQUIRED_FLAGS = string of compile command line flags
  CMAKE_REQUIRED_DEFINITIONS = list of macros to define (-DFOO=bar)
  CMAKE_REQUIRED_LINK_OPTIONS = list of options to pass to link command
  CMAKE_REQUIRED_LIBRARIES = list of libraries to link
  CMAKE_REQUIRED_QUIET = execute quietly without messages
#]=======================================================================]

include_guard(GLOBAL)

macro(CHECK_VARIABLE_EXISTS VAR VARIABLE)
  if(NOT DEFINED "${VARIABLE}")
    set(MACRO_CHECK_VARIABLE_DEFINITIONS
      "-DCHECK_VARIABLE_EXISTS=${VAR} ${CMAKE_REQUIRED_FLAGS}")
    if(NOT CMAKE_REQUIRED_QUIET)
      message(CHECK_START "Looking for ${VAR}")
    endif()
    if(CMAKE_REQUIRED_LINK_OPTIONS)
      set(CHECK_VARIABLE_EXISTS_ADD_LINK_OPTIONS
        LINK_OPTIONS ${CMAKE_REQUIRED_LINK_OPTIONS})
    else()
      set(CHECK_VARIABLE_EXISTS_ADD_LINK_OPTIONS)
    endif()
    if(CMAKE_REQUIRED_LIBRARIES)
      set(CHECK_VARIABLE_EXISTS_ADD_LIBRARIES
        LINK_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES})
    else()
      set(CHECK_VARIABLE_EXISTS_ADD_LIBRARIES)
    endif()
    try_compile(${VARIABLE}
      ${CMAKE_BINARY_DIR}
      ${CMAKE_ROOT}/Modules/CheckVariableExists.c
      COMPILE_DEFINITIONS ${CMAKE_REQUIRED_DEFINITIONS}
      ${CHECK_VARIABLE_EXISTS_ADD_LINK_OPTIONS}
      ${CHECK_VARIABLE_EXISTS_ADD_LIBRARIES}
      CMAKE_FLAGS -DCOMPILE_DEFINITIONS:STRING=${MACRO_CHECK_VARIABLE_DEFINITIONS}
      OUTPUT_VARIABLE OUTPUT)
    if(${VARIABLE})
      set(${VARIABLE} 1 CACHE INTERNAL "Have variable ${VAR}")
      if(NOT CMAKE_REQUIRED_QUIET)
        message(CHECK_PASS "found")
      endif()
      file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
        "Determining if the variable ${VAR} exists passed with the following output:\n"
        "${OUTPUT}\n\n")
    else()
      set(${VARIABLE} "" CACHE INTERNAL "Have variable ${VAR}")
      if(NOT CMAKE_REQUIRED_QUIET)
        message(CHECK_FAIL "not found")
      endif()
      file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
        "Determining if the variable ${VAR} exists failed with the following output:\n"
        "${OUTPUT}\n\n")
    endif()
  endif()
endmacro()
