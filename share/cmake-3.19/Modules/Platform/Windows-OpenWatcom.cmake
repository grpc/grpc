# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

# This module is shared by multiple languages; use include blocker.
include_guard()

set(CMAKE_BUILD_TYPE_INIT Debug)

set(CMAKE_CREATE_WIN32_EXE "system nt_win" )
set(CMAKE_CREATE_CONSOLE_EXE "system nt" )
string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT " system nt_dll")
string(APPEND CMAKE_MODULE_LINKER_FLAGS_INIT " system nt_dll")

set(CMAKE_C_COMPILE_OPTIONS_DLL "-bd") # Note: This variable is a ';' separated list
set(CMAKE_SHARED_LIBRARY_C_FLAGS "-bd") # ... while this is a space separated string.

set(CMAKE_RC_COMPILER "rc" )

# single/multi-threaded                 /-bm
# static/DLL run-time libraries         /-br
# default is setup for multi-threaded + DLL run-time libraries
string(APPEND CMAKE_C_FLAGS_INIT " -bt=nt -dWIN32 -br -bm")
string(APPEND CMAKE_CXX_FLAGS_INIT " -bt=nt -xs -dWIN32 -br -bm")

if(CMAKE_CROSSCOMPILING)
  if(NOT CMAKE_C_STANDARD_INCLUDE_DIRECTORIES)
    set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES $ENV{WATCOM}/h $ENV{WATCOM}/h/nt)
  endif()
  if(NOT CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES)
    set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES $ENV{WATCOM}/h $ENV{WATCOM}/h/nt)
  endif()
endif()
