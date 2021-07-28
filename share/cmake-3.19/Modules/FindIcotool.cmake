# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindIcotool
-----------

Find icotool

This module looks for icotool. Convert and create Win32 icon and cursor files.
This module defines the following values:

::

  ICOTOOL_EXECUTABLE: the full path to the icotool tool.
  ICOTOOL_FOUND: True if icotool has been found.
  ICOTOOL_VERSION_STRING: the version of icotool found.
#]=======================================================================]

find_program(ICOTOOL_EXECUTABLE
  icotool
)

if(ICOTOOL_EXECUTABLE)
  execute_process(
    COMMAND ${ICOTOOL_EXECUTABLE} --version
    OUTPUT_VARIABLE _icotool_version
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if("${_icotool_version}" MATCHES "^icotool \\([^\\)]*\\) ([0-9\\.]+[^ \n]*)")
    set( ICOTOOL_VERSION_STRING
      "${CMAKE_MATCH_1}"
    )
  else()
    set( ICOTOOL_VERSION_STRING
      ""
    )
  endif()
  unset(_icotool_version)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(
  Icotool
  REQUIRED_VARS ICOTOOL_EXECUTABLE
  VERSION_VAR ICOTOOL_VERSION_STRING
)

mark_as_advanced(
  ICOTOOL_EXECUTABLE
)
