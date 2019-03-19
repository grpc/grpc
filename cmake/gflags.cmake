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
set(gRPC_GFLAGS_PROVIDER "module" CACHE STRING "portability fix")
if("${gRPC_GFLAGS_PROVIDER}" STREQUAL "module")
  message("gRPC GFLAGS is MODULE")
  if(NOT GFLAGS_ROOT_DIR)
    set(GFLAGS_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/gflags)
  endif()
  if(EXISTS "${GFLAGS_ROOT_DIR}/CMakeLists.txt")
    message("gRPC GFLAGS adding subdirectory")
    add_subdirectory(${GFLAGS_ROOT_DIR} third_party/gflags)
    if(TARGET gflags_static)
      set(_gRPC_GFLAGS_LIBRARIES gflags_static)
      set(_gRPC_GFLAGS_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include")
    endif()
  else()
    message(WARNING "gRPC_GFLAGS_PROVIDER is \"module\" but GFLAGS_ROOT_DIR is wrong")
  endif()
elseif("${gRPC_GFLAGS_PROVIDER}" STREQUAL "package")
  message("gRPC GFLAGS is PACKAGE")
  # Use "CONFIG" as there is no built-in cmake module for gflags.
  find_package(gflags REQUIRED CONFIG)
  if(TARGET gflags)
    set(_gRPC_GFLAGS_LIBRARIES gflags)
    set(_gRPC_GFLAGS_INCLUDE_DIR ${GFLAGS_INCLUDE_DIR})
  endif()
  set(_gRPC_FIND_GFLAGS "if(NOT gflags_FOUND)\n  find_package(gflags CONFIG)\nendif()")
endif()
