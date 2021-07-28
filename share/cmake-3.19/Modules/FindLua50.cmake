# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindLua50
---------



Locate Lua library.
This module defines::

::

  LUA50_FOUND, if false, do not try to link to Lua
  LUA_LIBRARIES, both lua and lualib
  LUA_INCLUDE_DIR, where to find lua.h and lualib.h (and probably lauxlib.h)



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
  PATH_SUFFIXES include/lua50 include/lua5.0 include/lua5 include/lua include
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /opt
)

find_library(LUA_LIBRARY_lua
  NAMES lua50 lua5.0 lua-5.0 lua5 lua
  HINTS
    ENV LUA_DIR
  PATH_SUFFIXES lib
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /opt
)

# In an OS X framework, lualib is usually included as part of the framework
# (like GLU in OpenGL.framework)
if(${LUA_LIBRARY_lua} MATCHES "framework")
  set( LUA_LIBRARIES "${LUA_LIBRARY_lua}" CACHE STRING "Lua framework")
else()
  find_library(LUA_LIBRARY_lualib
    NAMES lualib50 lualib5.0 lualib5 lualib
    HINTS
      ENV LUALIB_DIR
      ENV LUA_DIR
    PATH_SUFFIXES lib
    PATHS
    /opt
  )
  if(LUA_LIBRARY_lualib AND LUA_LIBRARY_lua)
    # include the math library for Unix
    if(UNIX AND NOT APPLE)
      find_library(MATH_LIBRARY_FOR_LUA m)
      set( LUA_LIBRARIES "${LUA_LIBRARY_lualib};${LUA_LIBRARY_lua};${MATH_LIBRARY_FOR_LUA}" CACHE STRING "This is the concatenation of lua and lualib libraries")
    # For Windows and Mac, don't need to explicitly include the math library
    else()
      set( LUA_LIBRARIES "${LUA_LIBRARY_lualib};${LUA_LIBRARY_lua}" CACHE STRING "This is the concatenation of lua and lualib libraries")
    endif()
  endif()
endif()


include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
# handle the QUIETLY and REQUIRED arguments and set LUA_FOUND to TRUE if
# all listed variables are TRUE
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Lua50  DEFAULT_MSG  LUA_LIBRARIES LUA_INCLUDE_DIR)

mark_as_advanced(LUA_INCLUDE_DIR LUA_LIBRARIES)
