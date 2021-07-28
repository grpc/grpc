# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindDart
--------

Find DART

This module looks for the dart testing software and sets DART_ROOT to
point to where it found it.
#]=======================================================================]

find_path(DART_ROOT README.INSTALL
    HINTS
      ENV DART_ROOT
    PATHS
      ${PROJECT_SOURCE_DIR}
      /usr/share
      C:/
      "C:/Program Files"
      ${PROJECT_SOURCE_DIR}/..
      [HKEY_LOCAL_MACHINE\\SOFTWARE\\Dart\\InstallPath]
      ENV ProgramFiles
    PATH_SUFFIXES
      Dart
    DOC "If you have Dart installed, where is it located?"
    )

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Dart DEFAULT_MSG DART_ROOT)

mark_as_advanced(DART_ROOT)
