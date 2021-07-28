# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindLibArchive
--------------

Find libarchive library and headers.
Libarchive is multi-format archive and compression library.

The module defines the following variables:

::

  LibArchive_FOUND        - true if libarchive was found
  LibArchive_INCLUDE_DIRS - include search path
  LibArchive_LIBRARIES    - libraries to link
  LibArchive_VERSION      - libarchive 3-component version number

The module defines the following ``IMPORTED`` targets:

::

  LibArchive::LibArchive  - target for linking against libarchive
#]=======================================================================]

find_path(LibArchive_INCLUDE_DIR
  NAMES archive.h
  PATHS
  "[HKEY_LOCAL_MACHINE\\SOFTWARE\\GnuWin32\\LibArchive;InstallPath]/include"
  DOC "libarchive include directory"
  )

find_library(LibArchive_LIBRARY
  NAMES archive libarchive
  PATHS
  "[HKEY_LOCAL_MACHINE\\SOFTWARE\\GnuWin32\\LibArchive;InstallPath]/lib"
  DOC "libarchive library"
  )

mark_as_advanced(LibArchive_INCLUDE_DIR LibArchive_LIBRARY)

# Extract the version number from the header.
if(LibArchive_INCLUDE_DIR AND EXISTS "${LibArchive_INCLUDE_DIR}/archive.h")
  # The version string appears in one of three known formats in the header:
  #  #define ARCHIVE_LIBRARY_VERSION "libarchive 2.4.12"
  #  #define ARCHIVE_VERSION_STRING "libarchive 2.8.4"
  #  #define ARCHIVE_VERSION_ONLY_STRING "3.2.0"
  # Match any format.
  set(_LibArchive_VERSION_REGEX "^#define[ \t]+ARCHIVE[_A-Z]+VERSION[_A-Z]*[ \t]+\"(libarchive +)?([0-9]+)\\.([0-9]+)\\.([0-9]+)[^\"]*\".*$")
  file(STRINGS "${LibArchive_INCLUDE_DIR}/archive.h" _LibArchive_VERSION_STRING LIMIT_COUNT 1 REGEX "${_LibArchive_VERSION_REGEX}")
  if(_LibArchive_VERSION_STRING)
    string(REGEX REPLACE "${_LibArchive_VERSION_REGEX}" "\\2.\\3.\\4" LibArchive_VERSION "${_LibArchive_VERSION_STRING}")
  endif()
  unset(_LibArchive_VERSION_REGEX)
  unset(_LibArchive_VERSION_STRING)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
find_package_handle_standard_args(LibArchive
                                  REQUIRED_VARS LibArchive_LIBRARY LibArchive_INCLUDE_DIR
                                  VERSION_VAR LibArchive_VERSION
  )
unset(LIBARCHIVE_FOUND)

if(LibArchive_FOUND)
  set(LibArchive_INCLUDE_DIRS ${LibArchive_INCLUDE_DIR})
  set(LibArchive_LIBRARIES    ${LibArchive_LIBRARY})

  if (NOT TARGET LibArchive::LibArchive)
    add_library(LibArchive::LibArchive UNKNOWN IMPORTED)
    set_target_properties(LibArchive::LibArchive PROPERTIES
      IMPORTED_LOCATION "${LibArchive_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${LibArchive_INCLUDE_DIR}")
  endif ()
endif()
