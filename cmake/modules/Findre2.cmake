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

find_package(re2 QUIET CONFIG)
if(re2_FOUND)
  message(STATUS "Found RE2 via CMake.")
  return()
endif()

# As per https://github.com/grpc/grpc/issues/25434, idempotence is necessary
# because CMake fails when another target with the same name already exists.
if(TARGET re2::re2)
  message(STATUS "Found RE2 via pkg-config already?")
  return()
endif()

find_package(PkgConfig REQUIRED)
# TODO(junyer): Use the IMPORTED_TARGET option whenever CMake 3.6 (or newer)
# becomes the minimum required: that will take care of the add_library() and
# set_property() calls; then we can simply alias PkgConfig::RE2 as re2::re2.
# For now, we can only set INTERFACE_* properties that existed in CMake 3.5.
pkg_check_modules(RE2 QUIET re2)
if(RE2_FOUND)
  set(re2_FOUND "${RE2_FOUND}")
  add_library(re2::re2 INTERFACE IMPORTED)
  if(RE2_INCLUDE_DIRS)
    set_property(TARGET re2::re2 PROPERTY
                 INTERFACE_INCLUDE_DIRECTORIES "${RE2_INCLUDE_DIRS}")
  endif()
  if(RE2_CFLAGS_OTHER)
    # Filter out the -std flag, which is handled by CMAKE_CXX_STANDARD.
    # TODO(junyer): Use the FILTER option whenever CMake 3.6 (or newer)
    # becomes the minimum required: that will allow this to be concise.
    foreach(flag IN LISTS RE2_CFLAGS_OTHER)
      if("${flag}" MATCHES "^-std=")
        list(REMOVE_ITEM RE2_CFLAGS_OTHER "${flag}")
      endif()
    endforeach()
    set_property(TARGET re2::re2 PROPERTY
                 INTERFACE_COMPILE_OPTIONS "${RE2_CFLAGS_OTHER}")
  endif()
  if(RE2_LDFLAGS)
    set_property(TARGET re2::re2 PROPERTY
                 INTERFACE_LINK_LIBRARIES "${RE2_LDFLAGS}")
  endif()
  message(STATUS "Found RE2 via pkg-config.")
  return()
endif()

if(re2_FIND_REQUIRED)
  message(FATAL_ERROR "Failed to find RE2.")
elseif(NOT re2_FIND_QUIETLY)
  message(WARNING "Failed to find RE2.")
endif()
