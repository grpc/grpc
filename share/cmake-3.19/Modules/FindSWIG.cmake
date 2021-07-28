# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindSWIG
--------

Find the Simplified Wrapper and Interface Generator (SWIG_) executable.

This module finds an installed SWIG and determines its version. If a
``COMPONENTS`` or ``OPTIONAL_COMPONENTS`` argument is given to the
:command:`find_package` command, it will also determine supported target
languages.

When a version is requested, it can be specified as a simple value or as a
range. For a detailed description of version range usage and capabilities,
refer to the :command:`find_package` command.

The module defines the following variables:

``SWIG_FOUND``
  Whether SWIG and any required components were found on the system.
``SWIG_EXECUTABLE``
  Path to the SWIG executable.
``SWIG_DIR``
  Path to the installed SWIG ``Lib`` directory (result of ``swig -swiglib``).
``SWIG_VERSION``
  SWIG executable version (result of ``swig -version``).
``SWIG_<lang>_FOUND``
  If ``COMPONENTS`` or ``OPTIONAL_COMPONENTS`` are requested, each available
  target language ``<lang>`` (lowercase) will be set to TRUE.

Any ``COMPONENTS`` given to ``find_package`` should be the names of supported
target languages as provided to the LANGUAGE argument of ``swig_add_library``,
such as ``python`` or ``perl5``. Language names *must* be lowercase.

All information is collected from the ``SWIG_EXECUTABLE``, so the version
to be found can be changed from the command line by means of setting
``SWIG_EXECUTABLE``.

Example usage requiring SWIG 4.0 or higher and Python language support, with
optional Fortran support:

.. code-block:: cmake

   find_package(SWIG 4.0 COMPONENTS python OPTIONAL_COMPONENTS fortran)
   if(SWIG_FOUND)
     message("SWIG found: ${SWIG_EXECUTABLE}")
     if(NOT SWIG_fortran_FOUND)
       message(WARNING "SWIG Fortran bindings cannot be generated")
     endif()
   endif()

.. _`SWIG`: http://swig.org

#]=======================================================================]

# compute list of possible names
unset (_SWIG_NAMES)
if (SWIG_FIND_VERSION_RANGE)
  foreach (_SWIG_MAJOR IN ITEMS 4 3 2)
    if (_SWIG_MAJOR VERSION_GREATER_EQUAL SWIG_FIND_VERSION_MIN_MAJOR
        AND ((SWIG_FIND_VERSION_RANGE_MAX STREQUAL "INCLUDE" AND _SWIG_MAJOR VERSION_LESS_EQUAL SWIG_FIND_VERSION_MAX)
        OR (SWIG_FIND_VERSION_RANGE_MAX STREQUAL "EXCLUDE" AND _SWIG_MAJOR VERSION_LESS SWIG_FIND_VERSION_MAX)))
      list (APPEND _SWIG_NAMES swig${_SWIG_MAJOR}.0)
    endif()
  endforeach()
elseif(SWIG_FIND_VERSION)
  if (SWIG_FIND_VERSION_EXACT)
    set(_SWIG_NAMES swig${SWIG_FIND_VERSION_MAJOR}.0)
  else()
    foreach (_SWIG_MAJOR IN ITEMS 4 3 2)
      if (_SWIG_MAJOR VERSION_GREATER_EQUAL SWIG_FIND_VERSION_MAJOR)
        list (APPEND _SWIG_NAMES swig${_SWIG_MAJOR}.0)
      endif()
    endforeach()
  endif()
else()
  set (_SWIG_NAMES swig4.0 swig3.0 swig2.0)
endif()
if (NOT _SWIG_NAMES)
  # try to find any version
  set (_SWIG_NAMES swig4.0 swig3.0 swig2.0)
endif()

find_program(SWIG_EXECUTABLE NAMES ${_SWIG_NAMES} swig)

if(SWIG_EXECUTABLE)
  execute_process(COMMAND ${SWIG_EXECUTABLE} -swiglib
    OUTPUT_VARIABLE SWIG_swiglib_output
    ERROR_VARIABLE SWIG_swiglib_error
    RESULT_VARIABLE SWIG_swiglib_result)

  if(SWIG_swiglib_result)
    if(SWIG_FIND_REQUIRED)
      message(SEND_ERROR "Command \"${SWIG_EXECUTABLE} -swiglib\" failed with output:\n${SWIG_swiglib_error}")
    else()
      message(STATUS "Command \"${SWIG_EXECUTABLE} -swiglib\" failed with output:\n${SWIG_swiglib_error}")
    endif()
  else()
    string(REGEX REPLACE "[\n\r]+" ";" SWIG_swiglib_output ${SWIG_swiglib_output})
    find_path(SWIG_DIR swig.swg PATHS ${SWIG_swiglib_output} NO_CMAKE_FIND_ROOT_PATH)
    if(SWIG_DIR)
      set(SWIG_USE_FILE ${CMAKE_CURRENT_LIST_DIR}/UseSWIG.cmake)
      execute_process(COMMAND ${SWIG_EXECUTABLE} -version
        OUTPUT_VARIABLE SWIG_version_output
        ERROR_VARIABLE SWIG_version_output
        RESULT_VARIABLE SWIG_version_result)
      if(SWIG_version_result)
        message(SEND_ERROR "Command \"${SWIG_EXECUTABLE} -version\" failed with output:\n${SWIG_version_output}")
      else()
        string(REGEX REPLACE ".*SWIG Version[^0-9.]*\([0-9.]+\).*" "\\1"
          SWIG_version_output "${SWIG_version_output}")
        set(SWIG_VERSION ${SWIG_version_output} CACHE STRING "Swig version" FORCE)
      endif()
    endif()
  endif()

  if(SWIG_FIND_COMPONENTS)
    execute_process(COMMAND ${SWIG_EXECUTABLE} -help
      OUTPUT_VARIABLE SWIG_swighelp_output
      ERROR_VARIABLE SWIG_swighelp_error
      RESULT_VARIABLE SWIG_swighelp_result)
    if(SWIG_swighelp_result)
      message(SEND_ERROR "Command \"${SWIG_EXECUTABLE} -help\" failed with output:\n${SWIG_swiglib_error}")
    else()
      string(REPLACE "\n" ";" SWIG_swighelp_output "${SWIG_swighelp_output}")
      foreach(SWIG_line IN LISTS SWIG_swighelp_output)
        if(SWIG_line MATCHES "-([A-Za-z0-9_]+) +- *Generate.*wrappers")
          set(SWIG_${CMAKE_MATCH_1}_FOUND TRUE)
        endif()
      endforeach()
    endif()
  endif()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
find_package_handle_standard_args(
  SWIG HANDLE_COMPONENTS
  REQUIRED_VARS SWIG_EXECUTABLE SWIG_DIR
  VERSION_VAR SWIG_VERSION
  HANDLE_VERSION_RANGE)

mark_as_advanced(SWIG_DIR SWIG_VERSION SWIG_EXECUTABLE)
