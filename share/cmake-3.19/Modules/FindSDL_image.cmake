# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindSDL_image
-------------

Locate SDL_image library

This module defines:

::

  SDL_IMAGE_LIBRARIES, the name of the library to link against
  SDL_IMAGE_INCLUDE_DIRS, where to find the headers
  SDL_IMAGE_FOUND, if false, do not try to link against
  SDL_IMAGE_VERSION_STRING - human-readable string containing the
                             version of SDL_image



For backward compatibility the following variables are also set:

::

  SDLIMAGE_LIBRARY (same value as SDL_IMAGE_LIBRARIES)
  SDLIMAGE_INCLUDE_DIR (same value as SDL_IMAGE_INCLUDE_DIRS)
  SDLIMAGE_FOUND (same value as SDL_IMAGE_FOUND)



$SDLDIR is an environment variable that would correspond to the
./configure --prefix=$SDLDIR used in building SDL.

Created by Eric Wing.  This was influenced by the FindSDL.cmake
module, but with modifications to recognize OS X frameworks and
additional Unix paths (FreeBSD, etc).
#]=======================================================================]

if(NOT SDL_IMAGE_INCLUDE_DIR AND SDLIMAGE_INCLUDE_DIR)
  set(SDL_IMAGE_INCLUDE_DIR ${SDLIMAGE_INCLUDE_DIR} CACHE PATH "directory cache
entry initialized from old variable name")
endif()
find_path(SDL_IMAGE_INCLUDE_DIR SDL_image.h
  HINTS
    ENV SDLIMAGEDIR
    ENV SDLDIR
  PATH_SUFFIXES SDL
                # path suffixes to search inside ENV{SDLDIR}
                include/SDL include/SDL12 include/SDL11 include
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(VC_LIB_PATH_SUFFIX lib/x64)
else()
  set(VC_LIB_PATH_SUFFIX lib/x86)
endif()

if(NOT SDL_IMAGE_LIBRARY AND SDLIMAGE_LIBRARY)
  set(SDL_IMAGE_LIBRARY ${SDLIMAGE_LIBRARY} CACHE FILEPATH "file cache entry
initialized from old variable name")
endif()
find_library(SDL_IMAGE_LIBRARY
  NAMES SDL_image
  HINTS
    ENV SDLIMAGEDIR
    ENV SDLDIR
  PATH_SUFFIXES lib ${VC_LIB_PATH_SUFFIX}
)

if(SDL_IMAGE_INCLUDE_DIR AND EXISTS "${SDL_IMAGE_INCLUDE_DIR}/SDL_image.h")
  file(STRINGS "${SDL_IMAGE_INCLUDE_DIR}/SDL_image.h" SDL_IMAGE_VERSION_MAJOR_LINE REGEX "^#define[ \t]+SDL_IMAGE_MAJOR_VERSION[ \t]+[0-9]+$")
  file(STRINGS "${SDL_IMAGE_INCLUDE_DIR}/SDL_image.h" SDL_IMAGE_VERSION_MINOR_LINE REGEX "^#define[ \t]+SDL_IMAGE_MINOR_VERSION[ \t]+[0-9]+$")
  file(STRINGS "${SDL_IMAGE_INCLUDE_DIR}/SDL_image.h" SDL_IMAGE_VERSION_PATCH_LINE REGEX "^#define[ \t]+SDL_IMAGE_PATCHLEVEL[ \t]+[0-9]+$")
  string(REGEX REPLACE "^#define[ \t]+SDL_IMAGE_MAJOR_VERSION[ \t]+([0-9]+)$" "\\1" SDL_IMAGE_VERSION_MAJOR "${SDL_IMAGE_VERSION_MAJOR_LINE}")
  string(REGEX REPLACE "^#define[ \t]+SDL_IMAGE_MINOR_VERSION[ \t]+([0-9]+)$" "\\1" SDL_IMAGE_VERSION_MINOR "${SDL_IMAGE_VERSION_MINOR_LINE}")
  string(REGEX REPLACE "^#define[ \t]+SDL_IMAGE_PATCHLEVEL[ \t]+([0-9]+)$" "\\1" SDL_IMAGE_VERSION_PATCH "${SDL_IMAGE_VERSION_PATCH_LINE}")
  set(SDL_IMAGE_VERSION_STRING ${SDL_IMAGE_VERSION_MAJOR}.${SDL_IMAGE_VERSION_MINOR}.${SDL_IMAGE_VERSION_PATCH})
  unset(SDL_IMAGE_VERSION_MAJOR_LINE)
  unset(SDL_IMAGE_VERSION_MINOR_LINE)
  unset(SDL_IMAGE_VERSION_PATCH_LINE)
  unset(SDL_IMAGE_VERSION_MAJOR)
  unset(SDL_IMAGE_VERSION_MINOR)
  unset(SDL_IMAGE_VERSION_PATCH)
endif()

set(SDL_IMAGE_LIBRARIES ${SDL_IMAGE_LIBRARY})
set(SDL_IMAGE_INCLUDE_DIRS ${SDL_IMAGE_INCLUDE_DIR})

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)

FIND_PACKAGE_HANDLE_STANDARD_ARGS(SDL_image
                                  REQUIRED_VARS SDL_IMAGE_LIBRARIES SDL_IMAGE_INCLUDE_DIRS
                                  VERSION_VAR SDL_IMAGE_VERSION_STRING)

# for backward compatibility
set(SDLIMAGE_LIBRARY ${SDL_IMAGE_LIBRARIES})
set(SDLIMAGE_INCLUDE_DIR ${SDL_IMAGE_INCLUDE_DIRS})
set(SDLIMAGE_FOUND ${SDL_IMAGE_FOUND})

mark_as_advanced(SDL_IMAGE_LIBRARY SDL_IMAGE_INCLUDE_DIR)
