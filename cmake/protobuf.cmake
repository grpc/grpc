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

if (gRPC_USE_PROTO_LITE)
  set(_gRPC_PROTOBUF_LIBRARY_NAME "libprotobuf-lite")
  add_definitions("-DGRPC_USE_PROTO_LITE")
else()
  set(_gRPC_PROTOBUF_LIBRARY_NAME "libprotobuf")
endif()

if("${gRPC_PROTOBUF_PROVIDER}" STREQUAL "module")
  # Build dependency as external project from git submodule

  include(ExternalProject)

  ExternalProject_Add(protobuf
    PREFIX protobuf
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/protobuf/cmake"
    CMAKE_CACHE_ARGS
          ${_gRPC_EP_COMMON_ARGS}
          -Dprotobuf_BUILD_TESTS:BOOL=OFF
          -Dprotobuf_WITH_ZLIB:BOOL=OFF
          -Dprotobuf_MSVC_STATIC_RUNTIME:BOOL=${gRPC_MSVC_STATIC_RUNTIME}
          -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/protobuf
  )

  add_library(protobuf::libprotobuf STATIC IMPORTED)
  add_dependencies(protobuf::libprotobuf protobuf)  # add dependency on the external project

  # protobuf uses GNUInstallDirs for its installation path, which makes it
  # hard to determine the location of the installed libraries. Instead,
  # we rely on identical libraries from protobuf's build tree.
  if(WIN32)
    set_property(TARGET protobuf::libprotobuf PROPERTY IMPORTED_LOCATION_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/protobuf/src/protobuf-build/Release/libprotobuf.lib)
    set_property(TARGET protobuf::libprotobuf PROPERTY IMPORTED_LOCATION_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/protobuf/src/protobuf-build/Debug/libprotobufd.lib)
  else()
    set_property(TARGET protobuf::libprotobuf PROPERTY IMPORTED_LOCATION_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/protobuf/src/protobuf-build/libprotobuf.a)
    set_property(TARGET protobuf::libprotobuf PROPERTY IMPORTED_LOCATION_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/protobuf/src/protobuf-build/libprotobufd.a)
  endif()

  add_library(protobuf::libprotobuf-lite STATIC IMPORTED)
  add_dependencies(protobuf::libprotobuf-lite protobuf)  # add dependency on the external project
  if(WIN32)
    set_property(TARGET protobuf::libprotobuf-lite PROPERTY IMPORTED_LOCATION_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/protobuf/src/protobuf-build/Release/libprotobuf-lite.lib)
    set_property(TARGET protobuf::libprotobuf-lite PROPERTY IMPORTED_LOCATION_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/protobuf/src/protobuf-build/Debug/libprotobuf-lited.lib)
  else()
    set_property(TARGET protobuf::libprotobuf-lite PROPERTY IMPORTED_LOCATION_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/protobuf/src/protobuf-build/libprotobuf-lite.a)
    set_property(TARGET protobuf::libprotobuf-lite PROPERTY IMPORTED_LOCATION_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/protobuf/src/protobuf-build/libprotobuf-lited.a)
  endif()

  add_library(protobuf::libprotoc STATIC IMPORTED)
  add_dependencies(protobuf::libprotoc protobuf)  # add dependency on the external project
  if(WIN32)
    set_property(TARGET protobuf::libprotoc PROPERTY IMPORTED_LOCATION_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/protobuf/src/protobuf-build/Release/libprotoc.lib)
    set_property(TARGET protobuf::libprotoc PROPERTY IMPORTED_LOCATION_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/protobuf/src/protobuf-build/Debug/libprotocd.lib)
  else()
    set_property(TARGET protobuf::libprotoc PROPERTY IMPORTED_LOCATION_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/protobuf/src/protobuf-build/libprotoc.a)
    set_property(TARGET protobuf::libprotoc PROPERTY IMPORTED_LOCATION_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/protobuf/src/protobuf-build/libprotocd.a)
  endif()

  add_executable(protobuf::protoc IMPORTED)
  add_dependencies(protobuf::protoc protobuf)  # add dependency on the external project
  if(WIN32)
    set_property(TARGET protobuf::protoc PROPERTY IMPORTED_LOCATION_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/protobuf/src/protobuf-build/Release/protoc.exe)
    set_property(TARGET protobuf::protoc PROPERTY IMPORTED_LOCATION_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/protobuf/src/protobuf-build/Debug/protoc.exe)
  else()
    set_property(TARGET protobuf::protoc PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/protobuf/src/protobuf-build/protoc)
  endif()

  set(_gRPC_PROTOBUF_LIBRARIES protobuf::${_gRPC_PROTOBUF_LIBRARY_NAME})
  set(_gRPC_PROTOBUF_PROTOC_LIBRARIES protobuf::libprotoc)
  set(_gRPC_PROTOBUF_PROTOC protobuf::protoc)
  set(_gRPC_PROTOBUF_PROTOC_EXECUTABLE $<TARGET_FILE:protobuf::protoc>)
  set(_gRPC_PROTOBUF_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/protobuf/include)
  # For well-known .proto files distributed with protobuf
  set(_gRPC_PROTOBUF_WELLKNOWN_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/protobuf/include)

elseif("${gRPC_PROTOBUF_PROVIDER}" STREQUAL "package")
  # Find pre-installed dependency

  find_package(Protobuf REQUIRED ${gRPC_PROTOBUF_PACKAGE_TYPE})

  # {Protobuf,PROTOBUF}_FOUND is defined based on find_package type ("MODULE" vs "CONFIG").
  # For "MODULE", the case has also changed between cmake 3.5 and 3.6.
  # We use the legacy uppercase version for *_LIBRARIES AND *_INCLUDE_DIRS variables
  # as newer cmake versions provide them too for backward compatibility.
  if(Protobuf_FOUND OR PROTOBUF_FOUND)
    if(TARGET protobuf::${_gRPC_PROTOBUF_LIBRARY_NAME})
      set(_gRPC_PROTOBUF_LIBRARIES protobuf::${_gRPC_PROTOBUF_LIBRARY_NAME})
    else()
      set(_gRPC_PROTOBUF_LIBRARIES ${PROTOBUF_LIBRARIES})
    endif()
    if(TARGET protobuf::libprotoc)
      set(_gRPC_PROTOBUF_PROTOC_LIBRARIES protobuf::libprotoc)
    else()
      set(_gRPC_PROTOBUF_PROTOC_LIBRARIES ${PROTOBUF_PROTOC_LIBRARIES})
    endif()
    if(TARGET protobuf::protoc)
      set(_gRPC_PROTOBUF_PROTOC protobuf::protoc)
      set(_gRPC_PROTOBUF_PROTOC_EXECUTABLE $<TARGET_FILE:protobuf::protoc>)
    else()
      set(_gRPC_PROTOBUF_PROTOC ${PROTOBUF_PROTOC_EXECUTABLE})
      set(_gRPC_PROTOBUF_PROTOC_EXECUTABLE ${PROTOBUF_PROTOC_EXECUTABLE})
    endif()
    set(_gRPC_FIND_PROTOBUF "if(NOT Protobuf_FOUND AND NOT PROTOBUF_FOUND)\n  find_package(Protobuf ${gRPC_PROTOBUF_PACKAGE_TYPE})\nendif()")
  endif()
  if(PROTOBUF_FOUND)
    set(_gRPC_PROTOBUF_INCLUDE_DIR ${PROTOBUF_INCLUDE_DIRS})
    set(_gRPC_PROTOBUF_WELLKNOWN_INCLUDE_DIR ${PROTOBUF_INCLUDE_DIRS})
  endif()
  if(Protobuf_FOUND)
    set(_gRPC_PROTOBUF_INCLUDE_DIR ${Protobuf_INCLUDE_DIRS})
    set(_gRPC_PROTOBUF_WELLKNOWN_INCLUDE_DIR ${Protobuf_INCLUDE_DIRS})
  endif()
endif()
