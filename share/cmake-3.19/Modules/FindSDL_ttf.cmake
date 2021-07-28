# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindSDL_ttf
-----------

Locate SDL_ttf library

This module defines:

::

  SDL_TTF_LIBRARIES, the name of the library to link against
  SDL_TTF_INCLUDE_DIRS, where to find the headers
  SDL_TTF_FOUND, if false, do not try to link against
  SDL_TTF_VERSION_STRING - human-readable string containing the version of SDL_ttf



For backward compatibility the following variables are also set:

::

  SDLTTF_LIBRARY (same value as SDL_TTF_LIBRARIES)
  SDLTTF_INCLUDE_DIR (same value as SDL_TTF_INCLUDE_DIRS)
  SDLTTF_FOUND (same value as SDL_TTF_FOUND)



$SDLDIR is an environment variable that would correspond to the
./configure --prefix=$SDLDIR used in building SDL.

Created by Eric Wing.  This was influenced by the FindSDL.cmake
module, but with modifications to recognize OS X frameworks and
additional Unix paths (FreeBSD, etc).
#]=======================================================================]

if(NOT SDL_TTF_INCLUDE_DIR AND SDLTTF_INCLUDE_DIR)
  set(SDL_TTF_INCLUDE_DIR ${SDLTTF_INCLUDE_DIR} CACHE PATH "directory cache
entry initialized from old variable name")
endif()
find_path(SDL_TTF_INCLUDE_DIR SDL_ttf.h
  HINTS
    ENV SDLTTFDIR
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

if(NOT SDL_TTF_LIBRARY AND SDLTTF_LIBRARY)
  set(SDL_TTF_LIBRARY ${SDLTTF_LIBRARY} CACHE FILEPATH "file cache entry
initialized from old variable name")
endif()
find_library(SDL_TTF_LIBRARY
  NAMES SDL_ttf
  HINTS
    ENV SDLTTFDIR
    ENV SDLDIR
  PATH_SUFFIXES lib ${VC_LIB_PATH_SUFFIX}
)

if(SDL_TTF_INCLUDE_DIR AND EXISTS "${SDL_TTF_INCLUDE_DIR}/SDL_ttf.h")
  file(STRINGS "${SDL_TTF_INCLUDE_DIR}/SDL_ttf.h" SDL_TTF_VERSION_MAJOR_LINE REGEX "^#define[ \t]+SDL_TTF_MAJOR_VERSION[ \t]+[0-9]+$")
  file(STRINGS "${SDL_TTF_INCLUDE_DIR}/SDL_ttf.h" SDL_TTF_VERSION_MINOR_LINE REGEX "^#define[ \t]+SDL_TTF_MINOR_VERSION[ \t]+[0-9]+$")
  file(STRINGS "${SDL_TTF_INCLUDE_DIR}/SDL_ttf.h" SDL_TTF_VERSION_PATCH_LINE REGEX "^#define[ \t]+SDL_TTF_PATCHLEVEL[ \t]+[0-9]+$")
  string(REGEX REPLACE "^#define[ \t]+SDL_TTF_MAJOR_VERSION[ \t]+([0-9]+)$" "\\1" SDL_TTF_VERSION_MAJOR "${SDL_TTF_VERSION_MAJOR_LINE}")
  string(REGEX REPLACE "^#define[ \t]+SDL_TTF_MINOR_VERSION[ \t]+([0-9]+)$" "\\1" SDL_TTF_VERSION_MINOR "${SDL_TTF_VERSION_MINOR_LINE}")
  string(REGEX REPLACE "^#define[ \t]+SDL_TTF_PATCHLEVEL[ \t]+([0-9]+)$" "\\1" SDL_TTF_VERSION_PATCH "${SDL_TTF_VERSION_PATCH_LINE}")
  set(SDL_TTF_VERSION_STRING ${SDL_TTF_VERSION_MAJOR}.${SDL_TTF_VERSION_MINOR}.${SDL_TTF_VERSION_PATCH})
  unset(SDL_TTF_VERSION_MAJOR_LINE)
  unset(SDL_TTF_VERSION_MINOR_LINE)
  unset(SDL_TTF_VERSION_PATCH_LINE)
  unset(SDL_TTF_VERSION_MAJOR)
  unset(SDL_TTF_VERSION_MINOR)
  unset(SDL_TTF_VERSION_PATCH)
endif()

set(SDL_TTF_LIBRARIES ${SDL_TTF_LIBRARY})
set(SDL_TTF_INCLUDE_DIRS ${SDL_TTF_INCLUDE_DIR})

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)

FIND_PACKAGE_HANDLE_STANDARD_ARGS(SDL_ttf
                                  REQUIRED_VARS SDL_TTF_LIBRARIES SDL_TTF_INCLUDE_DIRS
                                  VERSION_VAR SDL_TTF_VERSION_STRING)

# for backward compatibility
set(SDLTTF_LIBRARY ${SDL_TTF_LIBRARIES})
set(SDLTTF_INCLUDE_DIR ${SDL_TTF_INCLUDE_DIRS})
set(SDLTTF_FOUND ${SDL_TTF_FOUND})

mark_as_advanced(SDL_TTF_LIBRARY SDL_TTF_INCLUDE_DIR)
