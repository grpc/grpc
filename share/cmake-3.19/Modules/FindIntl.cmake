# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindIntl
--------

.. versionadded:: 3.2

Find the Gettext libintl headers and libraries.

This module reports information about the Gettext libintl
installation in several variables.  General variables::

  Intl_FOUND - true if the libintl headers and libraries were found
  Intl_INCLUDE_DIRS - the directory containing the libintl headers
  Intl_LIBRARIES - libintl libraries to be linked

The following cache variables may also be set::

  Intl_INCLUDE_DIR - the directory containing the libintl headers
  Intl_LIBRARY - the libintl library (if any)

.. note::
  On some platforms, such as Linux with GNU libc, the gettext
  functions are present in the C standard library and libintl
  is not required.  ``Intl_LIBRARIES`` will be empty in this
  case.

.. note::
  If you wish to use the Gettext tools (``msgmerge``,
  ``msgfmt``, etc.), use :module:`FindGettext`.
#]=======================================================================]


# Written by Roger Leigh <rleigh@codelibre.net>

# Find include directory
find_path(Intl_INCLUDE_DIR
          NAMES "libintl.h"
          DOC "libintl include directory")
mark_as_advanced(Intl_INCLUDE_DIR)

# Find all Intl libraries
find_library(Intl_LIBRARY "intl" NAMES_PER_DIR
  DOC "libintl libraries (if not in the C library)")
mark_as_advanced(Intl_LIBRARY)

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Intl
                                  FOUND_VAR Intl_FOUND
                                  REQUIRED_VARS Intl_INCLUDE_DIR
                                  FAIL_MESSAGE "Failed to find Gettext libintl")

if(Intl_FOUND)
  set(Intl_INCLUDE_DIRS "${Intl_INCLUDE_DIR}")
  if(Intl_LIBRARY)
    set(Intl_LIBRARIES "${Intl_LIBRARY}")
  else()
    unset(Intl_LIBRARIES)
  endif()
endif()
