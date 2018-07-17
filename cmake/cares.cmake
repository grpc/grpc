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

if("${gRPC_CARES_PROVIDER}" STREQUAL "module")
  if(NOT CARES_ROOT_DIR)
    set(CARES_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cares/cares)
  endif()
  set(CARES_SHARED OFF CACHE BOOL "disable shared library")
  set(CARES_STATIC ON CACHE BOOL "link cares statically")
  add_subdirectory(third_party/cares/cares)

  if(TARGET c-ares)
    set(_gRPC_CARES_LIBRARIES c-ares)
    set(_gRPC_CARES_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cares/cares" "${CMAKE_CURRENT_BINARY_DIR}/third_party/cares/cares")
  endif()

  if(gRPC_INSTALL)
    message(WARNING "gRPC_INSTALL will be forced to FALSE because gRPC_CARES_PROVIDER is \"module\"")
    set(gRPC_INSTALL FALSE)
  endif()
elseif("${gRPC_CARES_PROVIDER}" STREQUAL "package")
  # Use "CONFIG" as there is no built-in cmake module for c-ares.
  find_package(c-ares REQUIRED CONFIG)
  if(TARGET c-ares::cares)
    set(_gRPC_CARES_LIBRARIES c-ares::cares)
    set(_gRPC_CARES_INCLUDE_DIR ${c-ares_INCLUDE_DIR})
  endif()
  set(_gRPC_FIND_CARES "if(NOT c-ares_FOUND)\n  find_package(c-ares CONFIG)\nendif()")
endif()
