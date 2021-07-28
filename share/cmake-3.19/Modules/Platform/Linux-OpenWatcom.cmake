# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

# This module is shared by multiple languages; use include blocker.
include_guard()

set(CMAKE_BUILD_TYPE_INIT Debug)

string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " system linux opt noextension")
string(APPEND CMAKE_MODULE_LINKER_FLAGS_INIT " system linux")
string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT " system linux")

# single/multi-threaded                 /-bm
# default is setup for single-threaded libraries
string(APPEND CMAKE_C_FLAGS_INIT " -bt=linux")
string(APPEND CMAKE_CXX_FLAGS_INIT " -bt=linux -xs")

if(CMAKE_CROSSCOMPILING)
  if(NOT CMAKE_C_STANDARD_INCLUDE_DIRECTORIES)
    set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES $ENV{WATCOM}/lh)
  endif()
  if(NOT CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES)
    set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES $ENV{WATCOM}/lh)
  endif()
endif()
