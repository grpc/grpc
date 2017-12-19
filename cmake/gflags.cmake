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

if("${gRPC_GFLAGS_PROVIDER}" STREQUAL "module")
  # Build dependency as external project from git submodule

  include(ExternalProject)

  ExternalProject_Add(gflags
    PREFIX gflags
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gflags"
    CMAKE_CACHE_ARGS
          ${_gRPC_EP_COMMON_ARGS}
          -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/gflags
  )

  add_library(gflags::gflags_static STATIC IMPORTED)
  add_dependencies(gflags::gflags_static gflags)  # add dependency on the external project
  if(WIN32)
    set_property(TARGET gflags::gflags_static PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/gflags/lib/gflags_static.lib)
    # TODO(jtattermusch): is there a better way?
    set_property(TARGET gflags::gflags_static PROPERTY INTERFACE_LINK_LIBRARIES "shlwapi.lib")
  else()
    set_property(TARGET gflags::gflags_static PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/gflags/lib/libgflags.a)
  endif()

  set(_gRPC_GFLAGS_LIBRARIES gflags::gflags_static)
  set(_gRPC_GFLAGS_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/gflags/include)

elseif("${gRPC_GFLAGS_PROVIDER}" STREQUAL "package")
  # Find pre-installed dependency

  find_package(gflags CONFIG REQUIRED)

  if(NOT TARGET gflags::gflags_static)
     message(WARNING "Target gflags::gflags_static not found by find_package")
  endif()
  set(_gRPC_GFLAGS_LIBRARIES gflags::gflags_static)
  set(_gRPC_GFLAGS_INCLUDE_DIR ${GFLAGS_INCLUDE_DIR})

  set(_gRPC_FIND_GFLAGS "if(NOT gflags_FOUND)\n  find_package(gflags CONFIG)\nendif()")

endif()
