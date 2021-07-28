# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindLibinput
------------

.. versionadded:: 3.14

Find libinput headers and library.

Imported Targets
^^^^^^^^^^^^^^^^

``Libinput::Libinput``
  The libinput library, if found.

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables in your project:

``Libinput_FOUND``
  true if (the requested version of) libinput is available.
``Libinput_VERSION``
  the version of libinput.
``Libinput_LIBRARIES``
  the libraries to link against to use libinput.
``Libinput_INCLUDE_DIRS``
  where to find the libinput headers.
``Libinput_COMPILE_OPTIONS``
  this should be passed to target_compile_options(), if the
  target is not used for linking

#]=======================================================================]


# Use pkg-config to get the directories and then use these values
# in the FIND_PATH() and FIND_LIBRARY() calls
find_package(PkgConfig QUIET)
pkg_check_modules(PKG_Libinput QUIET libinput)

set(Libinput_COMPILE_OPTIONS ${PKG_Libinput_CFLAGS_OTHER})
set(Libinput_VERSION ${PKG_Libinput_VERSION})

find_path(Libinput_INCLUDE_DIR
  NAMES
    libinput.h
  HINTS
    ${PKG_Libinput_INCLUDE_DIRS}
)
find_library(Libinput_LIBRARY
  NAMES
    input
  HINTS
    ${PKG_Libinput_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Libinput
  FOUND_VAR
    Libinput_FOUND
  REQUIRED_VARS
    Libinput_LIBRARY
    Libinput_INCLUDE_DIR
  VERSION_VAR
    Libinput_VERSION
)

if(Libinput_FOUND AND NOT TARGET Libinput::Libinput)
  add_library(Libinput::Libinput UNKNOWN IMPORTED)
  set_target_properties(Libinput::Libinput PROPERTIES
    IMPORTED_LOCATION "${Libinput_LIBRARY}"
    INTERFACE_COMPILE_OPTIONS "${Libinput_COMPILE_OPTIONS}"
    INTERFACE_INCLUDE_DIRECTORIES "${Libinput_INCLUDE_DIR}"
  )
endif()

mark_as_advanced(Libinput_LIBRARY Libinput_INCLUDE_DIR)

if(Libinput_FOUND)
  set(Libinput_LIBRARIES ${Libinput_LIBRARY})
  set(Libinput_INCLUDE_DIRS ${Libinput_INCLUDE_DIR})
endif()
