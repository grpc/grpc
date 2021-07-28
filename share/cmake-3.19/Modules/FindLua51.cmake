# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindLua51
---------



Locate Lua library.
This module defines::

::

  LUA51_FOUND, if false, do not try to link to Lua
  LUA_LIBRARIES
  LUA_INCLUDE_DIR, where to find lua.h
  LUA_VERSION_STRING, the version of Lua found (since CMake 2.8.8)



Note that the expected include convention is

::

  #include "lua.h"

and not

::

  #include <lua/lua.h>

This is because, the lua location is not standardized and may exist in
locations other than lua/
#]=======================================================================]

find_path(LUA_INCLUDE_DIR lua.h
  HINTS
    ENV LUA_DIR
  PATH_SUFFIXES include/lua51 include/lua5.1 include/lua-5.1 include/lua include
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /opt
)

find_library(LUA_LIBRARY
  NAMES lua51 lua5.1 lua-5.1 lua
  HINTS
    ENV LUA_DIR
  PATH_SUFFIXES lib
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /opt
)

if(LUA_LIBRARY)
  # include the math library for Unix
  if(UNIX AND NOT APPLE AND NOT BEOS AND NOT HAIKU)
    find_library(LUA_MATH_LIBRARY m)
    set( LUA_LIBRARIES "${LUA_LIBRARY};${LUA_MATH_LIBRARY}" CACHE STRING "Lua Libraries")
  # For Windows and Mac, don't need to explicitly include the math library
  else()
    set( LUA_LIBRARIES "${LUA_LIBRARY}" CACHE STRING "Lua Libraries")
  endif()
endif()

if(LUA_INCLUDE_DIR AND EXISTS "${LUA_INCLUDE_DIR}/lua.h")
  file(STRINGS "${LUA_INCLUDE_DIR}/lua.h" lua_version_str REGEX "^#define[ \t]+LUA_RELEASE[ \t]+\"Lua .+\"")

  string(REGEX REPLACE "^#define[ \t]+LUA_RELEASE[ \t]+\"Lua ([^\"]+)\".*" "\\1" LUA_VERSION_STRING "${lua_version_str}")
  unset(lua_version_str)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
# handle the QUIETLY and REQUIRED arguments and set LUA_FOUND to TRUE if
# all listed variables are TRUE
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Lua51
                                  REQUIRED_VARS LUA_LIBRARIES LUA_INCLUDE_DIR
                                  VERSION_VAR LUA_VERSION_STRING)

mark_as_advanced(LUA_INCLUDE_DIR LUA_LIBRARIES LUA_LIBRARY LUA_MATH_LIBRARY)
