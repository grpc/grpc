# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindTclsh
---------

Find tclsh

This module finds if TCL is installed and determines where the include
files and libraries are.  It also determines what the name of the
library is.  This code sets the following variables:

::

  TCLSH_FOUND = TRUE if tclsh has been found
  TCL_TCLSH = the path to the tclsh executable
#]=======================================================================]

get_filename_component(TK_WISH_PATH "${TK_WISH}" PATH)
get_filename_component(TK_WISH_PATH_PARENT "${TK_WISH_PATH}" PATH)
string(REGEX REPLACE
  "^.*wish([0-9]\\.*[0-9]).*$" "\\1" TK_WISH_VERSION "${TK_WISH}")

get_filename_component(TCL_INCLUDE_PATH_PARENT "${TCL_INCLUDE_PATH}" PATH)
get_filename_component(TK_INCLUDE_PATH_PARENT "${TK_INCLUDE_PATH}" PATH)

get_filename_component(TCL_LIBRARY_PATH "${TCL_LIBRARY}" PATH)
get_filename_component(TCL_LIBRARY_PATH_PARENT "${TCL_LIBRARY_PATH}" PATH)
string(REGEX REPLACE
  "^.*tcl([0-9]\\.*[0-9]).*$" "\\1" TCL_LIBRARY_VERSION "${TCL_LIBRARY}")

get_filename_component(TK_LIBRARY_PATH "${TK_LIBRARY}" PATH)
get_filename_component(TK_LIBRARY_PATH_PARENT "${TK_LIBRARY_PATH}" PATH)
string(REGEX REPLACE
  "^.*tk([0-9]\\.*[0-9]).*$" "\\1" TK_LIBRARY_VERSION "${TK_LIBRARY}")

set(TCLTK_POSSIBLE_BIN_PATHS
  "${TCL_INCLUDE_PATH_PARENT}/bin"
  "${TK_INCLUDE_PATH_PARENT}/bin"
  "${TCL_LIBRARY_PATH_PARENT}/bin"
  "${TK_LIBRARY_PATH_PARENT}/bin"
  "${TK_WISH_PATH_PARENT}/bin"
  )

if(WIN32)
  get_filename_component(
    ActiveTcl_CurrentVersion
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\ActiveState\\ActiveTcl;CurrentVersion]"
    NAME)
  set(TCLTK_POSSIBLE_BIN_PATHS ${TCLTK_POSSIBLE_BIN_PATHS}
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\ActiveState\\ActiveTcl\\${ActiveTcl_CurrentVersion}]/bin"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Scriptics\\Tcl\\8.6;Root]/bin"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Scriptics\\Tcl\\8.5;Root]/bin"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Scriptics\\Tcl\\8.4;Root]/bin"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Scriptics\\Tcl\\8.3;Root]/bin"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Scriptics\\Tcl\\8.2;Root]/bin"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Scriptics\\Tcl\\8.0;Root]/bin"
    )
endif()

set(TCL_TCLSH_NAMES
  tclsh
  tclsh${TCL_LIBRARY_VERSION} tclsh${TK_LIBRARY_VERSION} tclsh${TK_WISH_VERSION}
  tclsh87 tclsh8.7
  tclsh86 tclsh8.6
  tclsh85 tclsh8.5
  tclsh84 tclsh8.4
  tclsh83 tclsh8.3
  tclsh82 tclsh8.2
  tclsh80 tclsh8.0
  )

find_program(TCL_TCLSH
  NAMES ${TCL_TCLSH_NAMES}
  HINTS ${TCLTK_POSSIBLE_BIN_PATHS}
  )

if(TCL_TCLSH)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E echo puts "\$tcl_version"
                  COMMAND "${TCL_TCLSH}"
                  OUTPUT_VARIABLE TCLSH_VERSION_STRING
                  ERROR_QUIET
                  OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
if (CMAKE_FIND_PACKAGE_NAME STREQUAL "TCL" OR
    CMAKE_FIND_PACKAGE_NAME STREQUAL "TclStub")
  # FindTCL include()'s this module. It's an old pattern, but rather than
  # trying to suppress this from outside the module (which is then sensitive to
  # the contents, detect the case in this module and suppress it explicitly.
  # Transitively, FindTclStub includes FindTCL.
  set(FPHSA_NAME_MISMATCHED 1)
endif ()
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Tclsh
                                  REQUIRED_VARS TCL_TCLSH
                                  VERSION_VAR TCLSH_VERSION_STRING)
unset(FPHSA_NAME_MISMATCHED)

mark_as_advanced(TCL_TCLSH)
