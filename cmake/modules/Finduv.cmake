# Copyright 2017 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Cloned from Findre2.cmake.

find_package(libuv CONFIG)
if(libuv_FOUND)
  message(STATUS "Found libuv via CMake.")
  return()
endif()

# As per https://github.com/grpc/grpc/issues/25434, idempotence is necessary
# because CMake fails when another target with the same name already exists.
message(STATUS "Target is ${TARGET}")
if(TARGET libuv::uv)
  message(STATUS "Found libuv via pkg-config already?")
  return()
endif()

find_package(PkgConfig REQUIRED)
# TODO(junyer): Use the IMPORTED_TARGET option whenever CMake 3.6 (or newer)
# becomes the minimum required: that will take care of the add_library() and
# set_property() calls; then we can simply alias PkgConfig::uv as libuv::uv.
# For now, we can only set INTERFACE_* properties that existed in CMake 3.5.
pkg_check_modules(UV REQUIRED libuv)
if(UV_FOUND)
  set(libuv_FOUND "${UV_FOUND}")
  add_library(libuv::uv INTERFACE IMPORTED)
  if(UV_INCLUDE_DIRS)
    set_property(TARGET libuv::uv PROPERTY
                 INTERFACE_INCLUDE_DIRECTORIES "${UV_INCLUDE_DIRS}")
  endif()
  if(UV_CFLAGS_OTHER)
    # Filter out the -std flag, which is handled by CMAKE_CXX_STANDARD.
    # TODO(junyer): Use the FILTER option whenever CMake 3.6 (or newer)
    # becomes the minimum required: that will allow this to be concise.
    foreach(flag IN LISTS UV_CFLAGS_OTHER)
      if("${flag}" MATCHES "^-std=")
        list(REMOVE_ITEM UV_CFLAGS_OTHER "${flag}")
      endif()
    endforeach()
    set_property(TARGET libuv::uv PROPERTY
                 INTERFACE_COMPILE_OPTIONS "${UV_CFLAGS_OTHER}")
  endif()
  if(UV_LDFLAGS)
    set_property(TARGET libuv::uv PROPERTY
                 INTERFACE_LINK_LIBRARIES "${UV_LDFLAGS}")
  endif()
  message(STATUS "Found libuv via pkg-config.")
  return()
endif()

if(libuv_FIND_REQUIRED)
  message(FATAL_ERROR "Failed to find libuv.")
elseif(NOT libuv_FIND_QUIETLY)
  message(WARNING "Failed to find libuv.")
endif()
