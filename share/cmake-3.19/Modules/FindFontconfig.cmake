# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindFontconfig
--------------

.. versionadded:: 3.14

Find Fontconfig headers and library.

Imported Targets
^^^^^^^^^^^^^^^^

``Fontconfig::Fontconfig``
  The Fontconfig library, if found.

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables in your project:

``Fontconfig_FOUND``
  true if (the requested version of) Fontconfig is available.
``Fontconfig_VERSION``
  the version of Fontconfig.
``Fontconfig_LIBRARIES``
  the libraries to link against to use Fontconfig.
``Fontconfig_INCLUDE_DIRS``
  where to find the Fontconfig headers.
``Fontconfig_COMPILE_OPTIONS``
  this should be passed to target_compile_options(), if the
  target is not used for linking

#]=======================================================================]


# use pkg-config to get the directories and then use these values
# in the FIND_PATH() and FIND_LIBRARY() calls
find_package(PkgConfig QUIET)
pkg_check_modules(PKG_FONTCONFIG QUIET fontconfig)
set(Fontconfig_COMPILE_OPTIONS ${PKG_FONTCONFIG_CFLAGS_OTHER})
set(Fontconfig_VERSION ${PKG_FONTCONFIG_VERSION})

find_path( Fontconfig_INCLUDE_DIR
  NAMES
    fontconfig/fontconfig.h
  HINTS
    ${PKG_FONTCONFIG_INCLUDE_DIRS}
    /usr/X11/include
)

find_library( Fontconfig_LIBRARY
  NAMES
    fontconfig
  PATHS
    ${PKG_FONTCONFIG_LIBRARY_DIRS}
)

if (Fontconfig_INCLUDE_DIR AND NOT Fontconfig_VERSION)
  file(STRINGS ${Fontconfig_INCLUDE_DIR}/fontconfig/fontconfig.h _contents REGEX "^#define[ \t]+FC_[A-Z]+[ \t]+[0-9]+$")
  unset(Fontconfig_VERSION)
  foreach(VPART MAJOR MINOR REVISION)
    foreach(VLINE ${_contents})
      if(VLINE MATCHES "^#define[\t ]+FC_${VPART}[\t ]+([0-9]+)$")
        set(Fontconfig_VERSION_PART "${CMAKE_MATCH_1}")
        if(Fontconfig_VERSION)
          string(APPEND Fontconfig_VERSION ".${Fontconfig_VERSION_PART}")
        else()
          set(Fontconfig_VERSION "${Fontconfig_VERSION_PART}")
        endif()
      endif()
    endforeach()
  endforeach()
endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Fontconfig
  FOUND_VAR
    Fontconfig_FOUND
  REQUIRED_VARS
    Fontconfig_LIBRARY
    Fontconfig_INCLUDE_DIR
  VERSION_VAR
    Fontconfig_VERSION
)


if(Fontconfig_FOUND AND NOT TARGET Fontconfig::Fontconfig)
  add_library(Fontconfig::Fontconfig UNKNOWN IMPORTED)
  set_target_properties(Fontconfig::Fontconfig PROPERTIES
    IMPORTED_LOCATION "${Fontconfig_LIBRARY}"
    INTERFACE_COMPILE_OPTIONS "${Fontconfig_COMPILE_OPTIONS}"
    INTERFACE_INCLUDE_DIRECTORIES "${Fontconfig_INCLUDE_DIR}"
  )
endif()

mark_as_advanced(Fontconfig_LIBRARY Fontconfig_INCLUDE_DIR)

if(Fontconfig_FOUND)
  set(Fontconfig_LIBRARIES ${Fontconfig_LIBRARY})
  set(Fontconfig_INCLUDE_DIRS ${Fontconfig_INCLUDE_DIR})
endif()
