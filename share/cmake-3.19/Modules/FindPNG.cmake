# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindPNG
-------

Find libpng, the official reference library for the PNG image format.

Imported targets
^^^^^^^^^^^^^^^^

This module defines the following :prop_tgt:`IMPORTED` target:

``PNG::PNG``
  The libpng library, if found.

Result variables
^^^^^^^^^^^^^^^^

This module will set the following variables in your project:

``PNG_INCLUDE_DIRS``
  where to find png.h, etc.
``PNG_LIBRARIES``
  the libraries to link against to use PNG.
``PNG_DEFINITIONS``
  You should add_definitions(${PNG_DEFINITIONS}) before compiling code
  that includes png library files.
``PNG_FOUND``
  If false, do not try to use PNG.
``PNG_VERSION_STRING``
  the version of the PNG library found (since CMake 2.8.8)

Obsolete variables
^^^^^^^^^^^^^^^^^^

The following variables may also be set, for backwards compatibility:

``PNG_LIBRARY``
  where to find the PNG library.
``PNG_INCLUDE_DIR``
  where to find the PNG headers (same as PNG_INCLUDE_DIRS)

Since PNG depends on the ZLib compression library, none of the above
will be defined unless ZLib can be found.
#]=======================================================================]

if(PNG_FIND_QUIETLY)
  set(_FIND_ZLIB_ARG QUIET)
endif()
find_package(ZLIB ${_FIND_ZLIB_ARG})

if(ZLIB_FOUND)
  find_path(PNG_PNG_INCLUDE_DIR png.h PATH_SUFFIXES include/libpng)

  list(APPEND PNG_NAMES png libpng)
  unset(PNG_NAMES_DEBUG)
  set(_PNG_VERSION_SUFFIXES 17 16 15 14 12)
  if (PNG_FIND_VERSION MATCHES "^([0-9]+)\\.([0-9]+)(\\..*)?$")
    set(_PNG_VERSION_SUFFIX_MIN "${CMAKE_MATCH_1}${CMAKE_MATCH_2}")
    if (PNG_FIND_VERSION_EXACT)
      set(_PNG_VERSION_SUFFIXES ${_PNG_VERSION_SUFFIX_MIN})
    else ()
      string(REGEX REPLACE
          "${_PNG_VERSION_SUFFIX_MIN}.*" "${_PNG_VERSION_SUFFIX_MIN}"
          _PNG_VERSION_SUFFIXES "${_PNG_VERSION_SUFFIXES}")
    endif ()
    unset(_PNG_VERSION_SUFFIX_MIN)
  endif ()
  foreach(v IN LISTS _PNG_VERSION_SUFFIXES)
    list(APPEND PNG_NAMES png${v} libpng${v} libpng${v}_static)
    list(APPEND PNG_NAMES_DEBUG png${v}d libpng${v}d libpng${v}_staticd)
  endforeach()
  unset(_PNG_VERSION_SUFFIXES)
  # For compatibility with versions prior to this multi-config search, honor
  # any PNG_LIBRARY that is already specified and skip the search.
  if(NOT PNG_LIBRARY)
    find_library(PNG_LIBRARY_RELEASE NAMES ${PNG_NAMES} NAMES_PER_DIR)
    find_library(PNG_LIBRARY_DEBUG NAMES ${PNG_NAMES_DEBUG} NAMES_PER_DIR)
    include(${CMAKE_CURRENT_LIST_DIR}/SelectLibraryConfigurations.cmake)
    select_library_configurations(PNG)
    mark_as_advanced(PNG_LIBRARY_RELEASE PNG_LIBRARY_DEBUG)
  endif()
  unset(PNG_NAMES)
  unset(PNG_NAMES_DEBUG)

  # Set by select_library_configurations(), but we want the one from
  # find_package_handle_standard_args() below.
  unset(PNG_FOUND)

  if (PNG_LIBRARY AND PNG_PNG_INCLUDE_DIR)
      # png.h includes zlib.h. Sigh.
      set(PNG_INCLUDE_DIRS ${PNG_PNG_INCLUDE_DIR} ${ZLIB_INCLUDE_DIR} )
      set(PNG_INCLUDE_DIR ${PNG_INCLUDE_DIRS} ) # for backward compatibility
      set(PNG_LIBRARIES ${PNG_LIBRARY} ${ZLIB_LIBRARY})
      if((CMAKE_SYSTEM_NAME STREQUAL "Linux") AND
         ("${PNG_LIBRARY}" MATCHES "\\${CMAKE_STATIC_LIBRARY_SUFFIX}$"))
        list(APPEND PNG_LIBRARIES m)
      endif()

      if (CYGWIN)
        if(BUILD_SHARED_LIBS)
           # No need to define PNG_USE_DLL here, because it's default for Cygwin.
        else()
          set (PNG_DEFINITIONS -DPNG_STATIC)
          set(_PNG_COMPILE_DEFINITIONS PNG_STATIC)
        endif()
      endif ()

      if(NOT TARGET PNG::PNG)
        add_library(PNG::PNG UNKNOWN IMPORTED)
        set_target_properties(PNG::PNG PROPERTIES
          INTERFACE_COMPILE_DEFINITIONS "${_PNG_COMPILE_DEFINITIONS}"
          INTERFACE_INCLUDE_DIRECTORIES "${PNG_INCLUDE_DIRS}"
          INTERFACE_LINK_LIBRARIES ZLIB::ZLIB)
        if((CMAKE_SYSTEM_NAME STREQUAL "Linux") AND
           ("${PNG_LIBRARY}" MATCHES "\\${CMAKE_STATIC_LIBRARY_SUFFIX}$"))
          set_property(TARGET PNG::PNG APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES m)
        endif()

        if(EXISTS "${PNG_LIBRARY}")
          set_target_properties(PNG::PNG PROPERTIES
            IMPORTED_LINK_INTERFACE_LANGUAGES "C"
            IMPORTED_LOCATION "${PNG_LIBRARY}")
        endif()
        if(EXISTS "${PNG_LIBRARY_RELEASE}")
          set_property(TARGET PNG::PNG APPEND PROPERTY
            IMPORTED_CONFIGURATIONS RELEASE)
          set_target_properties(PNG::PNG PROPERTIES
            IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
            IMPORTED_LOCATION_RELEASE "${PNG_LIBRARY_RELEASE}")
        endif()
        if(EXISTS "${PNG_LIBRARY_DEBUG}")
          set_property(TARGET PNG::PNG APPEND PROPERTY
            IMPORTED_CONFIGURATIONS DEBUG)
          set_target_properties(PNG::PNG PROPERTIES
            IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
            IMPORTED_LOCATION_DEBUG "${PNG_LIBRARY_DEBUG}")
        endif()
      endif()

      unset(_PNG_COMPILE_DEFINITIONS)
  endif ()

  if (PNG_PNG_INCLUDE_DIR AND EXISTS "${PNG_PNG_INCLUDE_DIR}/png.h")
      file(STRINGS "${PNG_PNG_INCLUDE_DIR}/png.h" png_version_str REGEX "^#define[ \t]+PNG_LIBPNG_VER_STRING[ \t]+\".+\"")

      string(REGEX REPLACE "^#define[ \t]+PNG_LIBPNG_VER_STRING[ \t]+\"([^\"]+)\".*" "\\1" PNG_VERSION_STRING "${png_version_str}")
      unset(png_version_str)
  endif ()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
find_package_handle_standard_args(PNG
                                  REQUIRED_VARS PNG_LIBRARY PNG_PNG_INCLUDE_DIR
                                  VERSION_VAR PNG_VERSION_STRING)

mark_as_advanced(PNG_PNG_INCLUDE_DIR PNG_LIBRARY )
