# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindMotif
---------

Try to find Motif (or lesstif)

Once done this will define:

::

  MOTIF_FOUND        - system has MOTIF
  MOTIF_INCLUDE_DIR  - include paths to use Motif
  MOTIF_LIBRARIES    - Link these to use Motif
#]=======================================================================]

set(MOTIF_FOUND 0)

if(UNIX)
  find_path(MOTIF_INCLUDE_DIR
    Xm/Xm.h
    /usr/openwin/include
    )

  find_library(MOTIF_LIBRARIES
    Xm
    /usr/openwin/lib
    )

endif()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Motif DEFAULT_MSG MOTIF_LIBRARIES MOTIF_INCLUDE_DIR)

mark_as_advanced(
  MOTIF_INCLUDE_DIR
  MOTIF_LIBRARIES
)
