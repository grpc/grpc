# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindFreetype
------------

Find the FreeType font renderer includes and library.

Imported Targets
^^^^^^^^^^^^^^^^

This module defines the following :prop_tgt:`IMPORTED` target:

``Freetype::Freetype``
  The Freetype ``freetype`` library, if found

Result Variables
^^^^^^^^^^^^^^^^

This module will set the following variables in your project:

``FREETYPE_FOUND``
  true if the Freetype headers and libraries were found
``FREETYPE_INCLUDE_DIRS``
  directories containing the Freetype headers. This is the
  concatenation of the variables:

  ``FREETYPE_INCLUDE_DIR_ft2build``
    directory holding the main Freetype API configuration header
  ``FREETYPE_INCLUDE_DIR_freetype2``
    directory holding Freetype public headers
``FREETYPE_LIBRARIES``
  the library to link against
``FREETYPE_VERSION_STRING``
  the version of freetype found (since CMake 2.8.8)

Hints
^^^^^

The user may set the environment variable ``FREETYPE_DIR`` to the root
directory of a Freetype installation.
#]=======================================================================]

# Created by Eric Wing.
# Modifications by Alexander Neundorf.
# This file has been renamed to "FindFreetype.cmake" instead of the correct
# "FindFreeType.cmake" in order to be compatible with the one from KDE4, Alex.

# Ugh, FreeType seems to use some #include trickery which
# makes this harder than it should be. It looks like they
# put ft2build.h in a common/easier-to-find location which
# then contains a #include to a more specific header in a
# more specific location (#include <freetype/config/ftheader.h>).
# Then from there, they need to set a bunch of #define's
# so you can do something like:
# #include FT_FREETYPE_H
# Unfortunately, using CMake's mechanisms like include_directories()
# wants explicit full paths and this trickery doesn't work too well.
# I'm going to attempt to cut out the middleman and hope
# everything still works.

set(FREETYPE_FIND_ARGS
  HINTS
    ENV FREETYPE_DIR
  PATHS
    ENV GTKMM_BASEPATH
    [HKEY_CURRENT_USER\\SOFTWARE\\gtkmm\\2.4;Path]
    [HKEY_LOCAL_MACHINE\\SOFTWARE\\gtkmm\\2.4;Path]
)

find_path(
  FREETYPE_INCLUDE_DIR_ft2build
  ft2build.h
  ${FREETYPE_FIND_ARGS}
  PATH_SUFFIXES
    include/freetype2
    include
    freetype2
)

find_path(
  FREETYPE_INCLUDE_DIR_freetype2
  NAMES
    freetype/config/ftheader.h
    config/ftheader.h
  ${FREETYPE_FIND_ARGS}
  PATH_SUFFIXES
    include/freetype2
    include
    freetype2
)

if(NOT FREETYPE_LIBRARY)
  find_library(FREETYPE_LIBRARY_RELEASE
    NAMES
      freetype
      libfreetype
      freetype219
    ${FREETYPE_FIND_ARGS}
    PATH_SUFFIXES
      lib
  )
  find_library(FREETYPE_LIBRARY_DEBUG
    NAMES
      freetyped
      libfreetyped
      freetype219d
    ${FREETYPE_FIND_ARGS}
    PATH_SUFFIXES
      lib
  )
  include(${CMAKE_CURRENT_LIST_DIR}/SelectLibraryConfigurations.cmake)
  select_library_configurations(FREETYPE)
else()
  # on Windows, ensure paths are in canonical format (forward slahes):
  file(TO_CMAKE_PATH "${FREETYPE_LIBRARY}" FREETYPE_LIBRARY)
endif()

unset(FREETYPE_FIND_ARGS)

# set the user variables
if(FREETYPE_INCLUDE_DIR_ft2build AND FREETYPE_INCLUDE_DIR_freetype2)
  set(FREETYPE_INCLUDE_DIRS "${FREETYPE_INCLUDE_DIR_ft2build};${FREETYPE_INCLUDE_DIR_freetype2}")
  list(REMOVE_DUPLICATES FREETYPE_INCLUDE_DIRS)
endif()
set(FREETYPE_LIBRARIES "${FREETYPE_LIBRARY}")

if(EXISTS "${FREETYPE_INCLUDE_DIR_freetype2}/freetype/freetype.h")
  set(FREETYPE_H "${FREETYPE_INCLUDE_DIR_freetype2}/freetype/freetype.h")
elseif(EXISTS "${FREETYPE_INCLUDE_DIR_freetype2}/freetype.h")
  set(FREETYPE_H "${FREETYPE_INCLUDE_DIR_freetype2}/freetype.h")
endif()

if(FREETYPE_INCLUDE_DIR_freetype2 AND FREETYPE_H)
  file(STRINGS "${FREETYPE_H}" freetype_version_str
       REGEX "^#[\t ]*define[\t ]+FREETYPE_(MAJOR|MINOR|PATCH)[\t ]+[0-9]+$")

  unset(FREETYPE_VERSION_STRING)
  foreach(VPART MAJOR MINOR PATCH)
    foreach(VLINE ${freetype_version_str})
      if(VLINE MATCHES "^#[\t ]*define[\t ]+FREETYPE_${VPART}[\t ]+([0-9]+)$")
        set(FREETYPE_VERSION_PART "${CMAKE_MATCH_1}")
        if(FREETYPE_VERSION_STRING)
          string(APPEND FREETYPE_VERSION_STRING ".${FREETYPE_VERSION_PART}")
        else()
          set(FREETYPE_VERSION_STRING "${FREETYPE_VERSION_PART}")
        endif()
        unset(FREETYPE_VERSION_PART)
      endif()
    endforeach()
  endforeach()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)

find_package_handle_standard_args(
  Freetype
  REQUIRED_VARS
    FREETYPE_LIBRARY
    FREETYPE_INCLUDE_DIRS
  VERSION_VAR
    FREETYPE_VERSION_STRING
)

mark_as_advanced(
  FREETYPE_INCLUDE_DIR_freetype2
  FREETYPE_INCLUDE_DIR_ft2build
)

if(Freetype_FOUND)
  if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype UNKNOWN IMPORTED)
    set_target_properties(Freetype::Freetype PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${FREETYPE_INCLUDE_DIRS}")

    if(FREETYPE_LIBRARY_RELEASE)
      set_property(TARGET Freetype::Freetype APPEND PROPERTY
        IMPORTED_CONFIGURATIONS RELEASE)
      set_target_properties(Freetype::Freetype PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
        IMPORTED_LOCATION_RELEASE "${FREETYPE_LIBRARY_RELEASE}")
    endif()

    if(FREETYPE_LIBRARY_DEBUG)
      set_property(TARGET Freetype::Freetype APPEND PROPERTY
        IMPORTED_CONFIGURATIONS DEBUG)
      set_target_properties(Freetype::Freetype PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
        IMPORTED_LOCATION_DEBUG "${FREETYPE_LIBRARY_DEBUG}")
    endif()

    if(NOT FREETYPE_LIBRARY_RELEASE AND NOT FREETYPE_LIBRARY_DEBUG)
      set_target_properties(Freetype::Freetype PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LOCATION "${FREETYPE_LIBRARY}")
    endif()
  endif()
endif()
