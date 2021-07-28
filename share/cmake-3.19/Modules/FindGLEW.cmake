# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindGLEW
--------

Find the OpenGL Extension Wrangler Library (GLEW)

Input Variables
^^^^^^^^^^^^^^^

The following variables may be set to influence this module's behavior:

``GLEW_USE_STATIC_LIBS``
  to find and create :prop_tgt:`IMPORTED` target for static linkage.

``GLEW_VERBOSE``
  to output a detailed log of this module.

Imported Targets
^^^^^^^^^^^^^^^^

This module defines the following :ref:`Imported Targets <Imported Targets>`:


``GLEW::glew``
  The GLEW shared library.
``GLEW::glew_s``
  The GLEW static library, if ``GLEW_USE_STATIC_LIBS`` is set to ``TRUE``.
``GLEW::GLEW``
  Duplicates either ``GLEW::glew`` or ``GLEW::glew_s`` based on availability.

Result Variables
^^^^^^^^^^^^^^^^

This module defines the following variables:

``GLEW_INCLUDE_DIRS``
  include directories for GLEW
``GLEW_LIBRARIES``
  libraries to link against GLEW
``GLEW_SHARED_LIBRARIES``
  libraries to link against shared GLEW
``GLEW_STATIC_LIBRARIES``
  libraries to link against static GLEW
``GLEW_FOUND``
  true if GLEW has been found and can be used
``GLEW_VERSION``
  GLEW version
``GLEW_VERSION_MAJOR``
  GLEW major version
``GLEW_VERSION_MINOR``
  GLEW minor version
``GLEW_VERSION_MICRO``
  GLEW micro version

#]=======================================================================]

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)

find_package(GLEW CONFIG QUIET)

if(GLEW_FOUND)
  find_package_handle_standard_args(GLEW DEFAULT_MSG GLEW_CONFIG)
  return()
endif()

if(GLEW_VERBOSE)
  message(STATUS "FindGLEW: did not find GLEW CMake config file. Searching for libraries.")
endif()

if(APPLE)
  find_package(OpenGL QUIET)

  if(OpenGL_FOUND)
    if(GLEW_VERBOSE)
      message(STATUS "FindGLEW: Found OpenGL Framework.")
      message(STATUS "FindGLEW: OPENGL_LIBRARIES: ${OPENGL_LIBRARIES}")
    endif()
  else()
    if(GLEW_VERBOSE)
      message(STATUS "FindGLEW: could not find GLEW library.")
    endif()
    return()
  endif()
endif()


function(__glew_set_find_library_suffix shared_or_static)
  if((UNIX AND NOT APPLE) AND "${shared_or_static}" MATCHES "SHARED")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".so" PARENT_SCOPE)
  elseif((UNIX AND NOT APPLE) AND "${shared_or_static}" MATCHES "STATIC")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" PARENT_SCOPE)
  elseif(APPLE AND "${shared_or_static}" MATCHES "SHARED")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".dylib;.so" PARENT_SCOPE)
  elseif(APPLE AND "${shared_or_static}" MATCHES "STATIC")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" PARENT_SCOPE)
  elseif(WIN32 AND "${shared_or_static}" MATCHES "SHARED")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".lib" PARENT_SCOPE)
  elseif(WIN32 AND "${shared_or_static}" MATCHES "STATIC")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".lib;.dll.a" PARENT_SCOPE)
  endif()

  if(GLEW_VERBOSE)
    message(STATUS "FindGLEW: CMAKE_FIND_LIBRARY_SUFFIXES for ${shared_or_static}: ${CMAKE_FIND_LIBRARY_SUFFIXES}")
  endif()
endfunction()


if(GLEW_VERBOSE)
  if(DEFINED GLEW_USE_STATIC_LIBS)
    message(STATUS "FindGLEW: GLEW_USE_STATIC_LIBS: ${GLEW_USE_STATIC_LIBS}.")
  else()
    message(STATUS "FindGLEW: GLEW_USE_STATIC_LIBS is undefined. Treated as FALSE.")
  endif()
endif()

find_path(GLEW_INCLUDE_DIR GL/glew.h)
mark_as_advanced(GLEW_INCLUDE_DIR)

set(GLEW_INCLUDE_DIRS ${GLEW_INCLUDE_DIR})

if(GLEW_VERBOSE)
  message(STATUS "FindGLEW: GLEW_INCLUDE_DIR: ${GLEW_INCLUDE_DIR}")
  message(STATUS "FindGLEW: GLEW_INCLUDE_DIRS: ${GLEW_INCLUDE_DIRS}")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_arch "x64")
else()
  set(_arch "Win32")
endif()

set(__GLEW_CURRENT_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})

__glew_set_find_library_suffix(SHARED)

find_library(GLEW_SHARED_LIBRARY_RELEASE
             NAMES GLEW glew glew32
             NAMES_PER_DIR
             PATH_SUFFIXES lib lib64 libx32 lib/Release/${_arch}
             PATHS ENV GLEW_ROOT)

find_library(GLEW_SHARED_LIBRARY_DEBUG
             NAMES GLEWd glewd glew32d
             NAMES_PER_DIR
             PATH_SUFFIXES lib lib64
             PATHS ENV GLEW_ROOT)


__glew_set_find_library_suffix(STATIC)

find_library(GLEW_STATIC_LIBRARY_RELEASE
             NAMES GLEW glew glew32s
             NAMES_PER_DIR
             PATH_SUFFIXES lib lib64 libx32 lib/Release/${_arch}
             PATHS ENV GLEW_ROOT)

find_library(GLEW_STATIC_LIBRARY_DEBUG
             NAMES GLEWds glewds glew32ds
             NAMES_PER_DIR
             PATH_SUFFIXES lib lib64
             PATHS ENV GLEW_ROOT)

set(CMAKE_FIND_LIBRARY_SUFFIXES ${__GLEW_CURRENT_FIND_LIBRARY_SUFFIXES})
unset(__GLEW_CURRENT_FIND_LIBRARY_SUFFIXES)

include(${CMAKE_CURRENT_LIST_DIR}/SelectLibraryConfigurations.cmake)

select_library_configurations(GLEW_SHARED)
select_library_configurations(GLEW_STATIC)

if(NOT GLEW_USE_STATIC_LIBS)
  set(GLEW_LIBRARIES ${GLEW_SHARED_LIBRARY})
else()
  set(GLEW_LIBRARIES ${GLEW_STATIC_LIBRARY})
endif()


if(GLEW_VERBOSE)
  message(STATUS "FindGLEW: GLEW_SHARED_LIBRARY_RELEASE: ${GLEW_SHARED_LIBRARY_RELEASE}")
  message(STATUS "FindGLEW: GLEW_STATIC_LIBRARY_RELEASE: ${GLEW_STATIC_LIBRARY_RELEASE}")
  message(STATUS "FindGLEW: GLEW_SHARED_LIBRARY_DEBUG: ${GLEW_SHARED_LIBRARY_DEBUG}")
  message(STATUS "FindGLEW: GLEW_STATIC_LIBRARY_DEBUG: ${GLEW_STATIC_LIBRARY_DEBUG}")
  message(STATUS "FindGLEW: GLEW_SHARED_LIBRARY: ${GLEW_SHARED_LIBRARY}")
  message(STATUS "FindGLEW: GLEW_STATIC_LIBRARY: ${GLEW_STATIC_LIBRARY}")
  message(STATUS "FindGLEW: GLEW_LIBRARIES: ${GLEW_LIBRARIES}")
endif()


# Read version from GL/glew.h file
if(EXISTS "${GLEW_INCLUDE_DIR}/GL/glew.h")
  file(STRINGS "${GLEW_INCLUDE_DIR}/GL/glew.h" _contents REGEX "^VERSION_.+ [0-9]+")
  if(_contents)
    string(REGEX REPLACE ".*VERSION_MAJOR[ \t]+([0-9]+).*" "\\1" GLEW_VERSION_MAJOR "${_contents}")
    string(REGEX REPLACE ".*VERSION_MINOR[ \t]+([0-9]+).*" "\\1" GLEW_VERSION_MINOR "${_contents}")
    string(REGEX REPLACE ".*VERSION_MICRO[ \t]+([0-9]+).*" "\\1" GLEW_VERSION_MICRO "${_contents}")
    set(GLEW_VERSION "${GLEW_VERSION_MAJOR}.${GLEW_VERSION_MINOR}.${GLEW_VERSION_MICRO}")
  endif()
endif()

if(GLEW_VERBOSE)
  message(STATUS "FindGLEW: GLEW_VERSION_MAJOR: ${GLEW_VERSION_MAJOR}")
  message(STATUS "FindGLEW: GLEW_VERSION_MINOR: ${GLEW_VERSION_MINOR}")
  message(STATUS "FindGLEW: GLEW_VERSION_MICRO: ${GLEW_VERSION_MICRO}")
  message(STATUS "FindGLEW: GLEW_VERSION: ${GLEW_VERSION}")
endif()

find_package_handle_standard_args(GLEW
                                  REQUIRED_VARS GLEW_INCLUDE_DIRS GLEW_LIBRARIES
                                  VERSION_VAR GLEW_VERSION)

if(NOT GLEW_FOUND)
  if(GLEW_VERBOSE)
    message(STATUS "FindGLEW: could not find GLEW library.")
  endif()
  return()
endif()


if(NOT TARGET GLEW::glew AND NOT GLEW_USE_STATIC_LIBS)
  if(GLEW_VERBOSE)
    message(STATUS "FindGLEW: Creating GLEW::glew imported target.")
  endif()

  add_library(GLEW::glew UNKNOWN IMPORTED)

  set_target_properties(GLEW::glew
                        PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${GLEW_INCLUDE_DIRS}")

  if(APPLE)
    set_target_properties(GLEW::glew
                          PROPERTIES INTERFACE_LINK_LIBRARIES OpenGL::GL)
  endif()

  if(GLEW_SHARED_LIBRARY_RELEASE)
    set_property(TARGET GLEW::glew
                 APPEND
                 PROPERTY IMPORTED_CONFIGURATIONS RELEASE)

    set_target_properties(GLEW::glew
                          PROPERTIES IMPORTED_LOCATION_RELEASE "${GLEW_SHARED_LIBRARY_RELEASE}")
  endif()

  if(GLEW_SHARED_LIBRARY_DEBUG)
    set_property(TARGET GLEW::glew
                 APPEND
                 PROPERTY IMPORTED_CONFIGURATIONS DEBUG)

    set_target_properties(GLEW::glew
                          PROPERTIES IMPORTED_LOCATION_DEBUG "${GLEW_SHARED_LIBRARY_DEBUG}")
  endif()

elseif(NOT TARGET GLEW::glew_s AND GLEW_USE_STATIC_LIBS)
  if(GLEW_VERBOSE)
    message(STATUS "FindGLEW: Creating GLEW::glew_s imported target.")
  endif()

  add_library(GLEW::glew_s UNKNOWN IMPORTED)

  set_target_properties(GLEW::glew_s
                        PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${GLEW_INCLUDE_DIRS}")

  if(APPLE)
    set_target_properties(GLEW::glew_s
                          PROPERTIES INTERFACE_LINK_LIBRARIES OpenGL::GL)
  endif()

  if(GLEW_STATIC_LIBRARY_RELEASE)
    set_property(TARGET GLEW::glew_s
                 APPEND
                 PROPERTY IMPORTED_CONFIGURATIONS RELEASE)

    set_target_properties(GLEW::glew_s
                          PROPERTIES IMPORTED_LOCATION_RELEASE "${GLEW_STATIC_LIBRARY_RELEASE}")
  endif()

  if(GLEW_STATIC_LIBRARY_DEBUG)
    set_property(TARGET GLEW::glew_s
                 APPEND
                 PROPERTY IMPORTED_CONFIGURATIONS DEBUG)

    set_target_properties(GLEW::glew_s
                          PROPERTIES IMPORTED_LOCATION_DEBUG "${GLEW_STATIC_LIBRARY_DEBUG}")
  endif()
endif()

if(NOT TARGET GLEW::GLEW)
  if(GLEW_VERBOSE)
    message(STATUS "FindGLEW: Creating GLEW::GLEW imported target.")
  endif()

  add_library(GLEW::GLEW UNKNOWN IMPORTED)

  set_target_properties(GLEW::GLEW
                        PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${GLEW_INCLUDE_DIRS}")

  if(APPLE)
    set_target_properties(GLEW::GLEW
                          PROPERTIES INTERFACE_LINK_LIBRARIES OpenGL::GL)
  endif()

  if(TARGET GLEW::glew)
    if(GLEW_SHARED_LIBRARY_RELEASE)
      set_property(TARGET GLEW::GLEW
                   APPEND
                   PROPERTY IMPORTED_CONFIGURATIONS RELEASE)

      set_target_properties(GLEW::GLEW
                            PROPERTIES IMPORTED_LOCATION_RELEASE "${GLEW_SHARED_LIBRARY_RELEASE}")
    endif()

    if(GLEW_SHARED_LIBRARY_DEBUG)
      set_property(TARGET GLEW::GLEW
                   APPEND
                   PROPERTY IMPORTED_CONFIGURATIONS DEBUG)

      set_target_properties(GLEW::GLEW
                            PROPERTIES IMPORTED_LOCATION_DEBUG "${GLEW_SHARED_LIBRARY_DEBUG}")
    endif()

  elseif(TARGET GLEW::glew_s)
    if(GLEW_STATIC_LIBRARY_RELEASE)
      set_property(TARGET GLEW::GLEW
                   APPEND
                   PROPERTY IMPORTED_CONFIGURATIONS RELEASE)

      set_target_properties(GLEW::GLEW
                            PROPERTIES IMPORTED_LOCATION_RELEASE "${GLEW_STATIC_LIBRARY_RELEASE}")
    endif()

    if(GLEW_STATIC_LIBRARY_DEBUG AND GLEW_USE_STATIC_LIBS)
      set_property(TARGET GLEW::GLEW
                   APPEND
                   PROPERTY IMPORTED_CONFIGURATIONS DEBUG)

      set_target_properties(GLEW::GLEW
                            PROPERTIES IMPORTED_LOCATION_DEBUG "${GLEW_STATIC_LIBRARY_DEBUG}")
    endif()

  elseif(GLEW_VERBOSE)
    message(WARNING "FindGLEW: no `GLEW::glew` or `GLEW::glew_s` target was created. Something went wrong in FindGLEW target creation.")
  endif()
endif()
