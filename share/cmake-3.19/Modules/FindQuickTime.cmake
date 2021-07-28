# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindQuickTime
-------------



Locate QuickTime This module defines QUICKTIME_LIBRARY
QUICKTIME_FOUND, if false, do not try to link to gdal
QUICKTIME_INCLUDE_DIR, where to find the headers

$QUICKTIME_DIR is an environment variable that would correspond to the
./configure --prefix=$QUICKTIME_DIR

Created by Eric Wing.
#]=======================================================================]

find_path(QUICKTIME_INCLUDE_DIR QuickTime/QuickTime.h QuickTime.h
  HINTS
    ENV QUICKTIME_DIR
  PATH_SUFFIXES
    include
)
find_library(QUICKTIME_LIBRARY QuickTime
  HINTS
    ENV QUICKTIME_DIR
  PATH_SUFFIXES
    lib
)

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(QuickTime DEFAULT_MSG QUICKTIME_LIBRARY QUICKTIME_INCLUDE_DIR)
