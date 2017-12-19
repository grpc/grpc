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
  # Build dependency as external project from git submodule

  include(ExternalProject)

  ExternalProject_Add(c-ares
    PREFIX c-ares
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cares/cares"
    CMAKE_CACHE_ARGS
          ${_gRPC_EP_COMMON_ARGS}
          -DCARES_SHARED:BOOL=OFF
          -DCARES_STATIC:BOOL=ON
          -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/c-ares
  )

  add_library(c-ares::cares STATIC IMPORTED)
  add_dependencies(c-ares::cares c-ares)  # add dependency on the external project
  if(WIN32)
    set_property(TARGET c-ares::cares PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/c-ares/lib/cares.lib)
  else()
    set_property(TARGET c-ares::cares PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/c-ares/lib/libcares.a)
  endif()

  set(_gRPC_CARES_LIBRARIES c-ares::cares)
  set(_gRPC_CARES_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/c-ares/include)

elseif("${gRPC_CARES_PROVIDER}" STREQUAL "package")
  # Find pre-installed dependency

  find_package(c-ares CONFIG REQUIRED)

  if(NOT TARGET c-ares::cares)
     message(WARNING "Target c-ares::cares not found by find_package")
  endif()
  set(_gRPC_CARES_LIBRARIES c-ares::cares)
  set(_gRPC_CARES_INCLUDE_DIR ${c-ares_INCLUDE_DIR})

  set(_gRPC_FIND_CARES "if(NOT c-ares_FOUND)\n  find_package(c-ares CONFIG)\nendif()")

endif()
