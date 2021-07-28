# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindGnuplot
-----------

this module looks for gnuplot



Once done this will define

::

  GNUPLOT_FOUND - system has Gnuplot
  GNUPLOT_EXECUTABLE - the Gnuplot executable
  GNUPLOT_VERSION_STRING - the version of Gnuplot found (since CMake 2.8.8)



GNUPLOT_VERSION_STRING will not work for old versions like 3.7.1.
#]=======================================================================]

include(${CMAKE_CURRENT_LIST_DIR}/FindCygwin.cmake)

find_program(GNUPLOT_EXECUTABLE
  NAMES
  gnuplot
  pgnuplot
  wgnupl32
  PATHS
  ${CYGWIN_INSTALL_PATH}/bin
)

if (GNUPLOT_EXECUTABLE)
    execute_process(COMMAND "${GNUPLOT_EXECUTABLE}" --version
                  OUTPUT_VARIABLE GNUPLOT_OUTPUT_VARIABLE
                  ERROR_QUIET
                  OUTPUT_STRIP_TRAILING_WHITESPACE)

    string(REGEX REPLACE "^gnuplot ([0-9\\.]+)( patchlevel )?" "\\1." GNUPLOT_VERSION_STRING "${GNUPLOT_OUTPUT_VARIABLE}")
    string(REGEX REPLACE "\\.$" "" GNUPLOT_VERSION_STRING "${GNUPLOT_VERSION_STRING}")
    unset(GNUPLOT_OUTPUT_VARIABLE)
endif()

# for compatibility
set(GNUPLOT ${GNUPLOT_EXECUTABLE})

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Gnuplot
                                  REQUIRED_VARS GNUPLOT_EXECUTABLE
                                  VERSION_VAR GNUPLOT_VERSION_STRING)

mark_as_advanced( GNUPLOT_EXECUTABLE )
