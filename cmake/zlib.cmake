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

if("${gRPC_ZLIB_PROVIDER}" STREQUAL "module")
  # Build dependency as external project from git submodule

  include(ExternalProject)

  ExternalProject_Add(zlib
    PREFIX zlib
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib"
    CMAKE_CACHE_ARGS
          ${_gRPC_EP_COMMON_ARGS}
          -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/zlib
  )

  add_library(zlib::zlibstatic STATIC IMPORTED)
  add_dependencies(zlib::zlibstatic zlib)  # add dependency on the external project
  if(WIN32)
    set_property(TARGET zlib::zlibstatic PROPERTY IMPORTED_LOCATION_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/zlib/lib/zlibstatic.lib)
    set_property(TARGET zlib::zlibstatic PROPERTY IMPORTED_LOCATION_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/zlib/lib/zlibstaticd.lib)
  else()
    set_property(TARGET zlib::zlibstatic PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/zlib/lib/libz.a)
  endif()

  set(_gRPC_ZLIB_LIBRARIES zlib::zlibstatic)
  set(_gRPC_ZLIB_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/zlib/include)

elseif("${gRPC_ZLIB_PROVIDER}" STREQUAL "package")
  # Find pre-installed dependency

  find_package(ZLIB REQUIRED)

  if(TARGET ZLIB::ZLIB)
    set(_gRPC_ZLIB_LIBRARIES ZLIB::ZLIB)
  else()
    set(_gRPC_ZLIB_LIBRARIES ${ZLIB_LIBRARIES})
  endif()
  set(_gRPC_ZLIB_INCLUDE_DIR ${ZLIB_INCLUDE_DIRS})

  set(_gRPC_FIND_ZLIB "if(NOT ZLIB_FOUND)\n  find_package(ZLIB)\nendif()")

endif()
