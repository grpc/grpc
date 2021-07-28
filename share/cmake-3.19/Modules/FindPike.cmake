# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindPike
--------

Find Pike

This module finds if PIKE is installed and determines where the
include files and libraries are.  It also determines what the name of
the library is.  This code sets the following variables:

::

  PIKE_INCLUDE_PATH       = path to where program.h is found
  PIKE_EXECUTABLE         = full path to the pike binary
#]=======================================================================]

find_path(PIKE_INCLUDE_PATH program.h
  ${PIKE_POSSIBLE_INCLUDE_PATHS}
  PATH_SUFFIXES include/pike8.0/pike include/pike7.8/pike include/pike7.4/pike)

find_program(PIKE_EXECUTABLE
  NAMES pike8.0 pike 7.8 pike7.4
  )

mark_as_advanced(
  PIKE_EXECUTABLE
  PIKE_INCLUDE_PATH
  )
