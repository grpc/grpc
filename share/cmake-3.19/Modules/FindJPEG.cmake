# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindJPEG
--------

Find the Joint Photographic Experts Group (JPEG) library (``libjpeg``)

Imported targets
^^^^^^^^^^^^^^^^

This module defines the following :prop_tgt:`IMPORTED` targets:

``JPEG::JPEG``
  The JPEG library, if found.

Result variables
^^^^^^^^^^^^^^^^

This module will set the following variables in your project:

``JPEG_FOUND``
  If false, do not try to use JPEG.
``JPEG_INCLUDE_DIRS``
  where to find jpeglib.h, etc.
``JPEG_LIBRARIES``
  the libraries needed to use JPEG.
``JPEG_VERSION``
  the version of the JPEG library found

Cache variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``JPEG_INCLUDE_DIRS``
  where to find jpeglib.h, etc.
``JPEG_LIBRARY_RELEASE``
  where to find the JPEG library (optimized).
``JPEG_LIBRARY_DEBUG``
  where to find the JPEG library (debug).

Obsolete variables
^^^^^^^^^^^^^^^^^^

``JPEG_INCLUDE_DIR``
  where to find jpeglib.h, etc. (same as JPEG_INCLUDE_DIRS)
``JPEG_LIBRARY``
  where to find the JPEG library.
#]=======================================================================]

find_path(JPEG_INCLUDE_DIR jpeglib.h)

set(jpeg_names ${JPEG_NAMES} jpeg jpeg-static libjpeg libjpeg-static)
foreach(name ${jpeg_names})
  list(APPEND jpeg_names_debug "${name}d")
endforeach()

if(NOT JPEG_LIBRARY)
  find_library(JPEG_LIBRARY_RELEASE NAMES ${jpeg_names} NAMES_PER_DIR)
  find_library(JPEG_LIBRARY_DEBUG NAMES ${jpeg_names_debug} NAMES_PER_DIR)
  include(${CMAKE_CURRENT_LIST_DIR}/SelectLibraryConfigurations.cmake)
  select_library_configurations(JPEG)
  mark_as_advanced(JPEG_LIBRARY_RELEASE JPEG_LIBRARY_DEBUG)
endif()
unset(jpeg_names)
unset(jpeg_names_debug)

if(JPEG_INCLUDE_DIR)
  file(GLOB _JPEG_CONFIG_HEADERS_FEDORA "${JPEG_INCLUDE_DIR}/jconfig*.h")
  file(GLOB _JPEG_CONFIG_HEADERS_DEBIAN "${JPEG_INCLUDE_DIR}/*/jconfig.h")
  set(_JPEG_CONFIG_HEADERS
    "${JPEG_INCLUDE_DIR}/jpeglib.h"
    ${_JPEG_CONFIG_HEADERS_FEDORA}
    ${_JPEG_CONFIG_HEADERS_DEBIAN})
  foreach (_JPEG_CONFIG_HEADER IN LISTS _JPEG_CONFIG_HEADERS)
    if (NOT EXISTS "${_JPEG_CONFIG_HEADER}")
      continue ()
    endif ()
    file(STRINGS "${_JPEG_CONFIG_HEADER}"
      jpeg_lib_version REGEX "^#define[\t ]+JPEG_LIB_VERSION[\t ]+.*")

    if (NOT jpeg_lib_version)
      continue ()
    endif ()

    string(REGEX REPLACE "^#define[\t ]+JPEG_LIB_VERSION[\t ]+([0-9]+).*"
      "\\1" JPEG_VERSION "${jpeg_lib_version}")
    break ()
  endforeach ()
  unset(jpeg_lib_version)
  unset(_JPEG_CONFIG_HEADER)
  unset(_JPEG_CONFIG_HEADERS)
  unset(_JPEG_CONFIG_HEADERS_FEDORA)
  unset(_JPEG_CONFIG_HEADERS_DEBIAN)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
find_package_handle_standard_args(JPEG
  REQUIRED_VARS JPEG_LIBRARY JPEG_INCLUDE_DIR
  VERSION_VAR JPEG_VERSION)

if(JPEG_FOUND)
  set(JPEG_LIBRARIES ${JPEG_LIBRARY})
  set(JPEG_INCLUDE_DIRS "${JPEG_INCLUDE_DIR}")

  if(NOT TARGET JPEG::JPEG)
    add_library(JPEG::JPEG UNKNOWN IMPORTED)
    if(JPEG_INCLUDE_DIRS)
      set_target_properties(JPEG::JPEG PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${JPEG_INCLUDE_DIRS}")
    endif()
    if(EXISTS "${JPEG_LIBRARY}")
      set_target_properties(JPEG::JPEG PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LOCATION "${JPEG_LIBRARY}")
    endif()
    if(EXISTS "${JPEG_LIBRARY_RELEASE}")
      set_property(TARGET JPEG::JPEG APPEND PROPERTY
        IMPORTED_CONFIGURATIONS RELEASE)
      set_target_properties(JPEG::JPEG PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
        IMPORTED_LOCATION_RELEASE "${JPEG_LIBRARY_RELEASE}")
    endif()
    if(EXISTS "${JPEG_LIBRARY_DEBUG}")
      set_property(TARGET JPEG::JPEG APPEND PROPERTY
        IMPORTED_CONFIGURATIONS DEBUG)
      set_target_properties(JPEG::JPEG PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
        IMPORTED_LOCATION_DEBUG "${JPEG_LIBRARY_DEBUG}")
    endif()
  endif()
endif()

mark_as_advanced(JPEG_LIBRARY JPEG_INCLUDE_DIR)
