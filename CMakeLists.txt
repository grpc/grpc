# GRPC global cmake file
# This currently builds C and C++ code.
# This file has been automatically generated from a template file.
# Please look at the templates directory instead.
# This file can be regenerated from the template by running
# tools/buildgen/generate_projects.sh
#
# Copyright 2015 gRPC authors.
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

cmake_minimum_required(VERSION 2.8)

set(PACKAGE_NAME      "grpc")
set(PACKAGE_VERSION   "1.12.0-dev")
set(PACKAGE_STRING    "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_TARNAME   "${PACKAGE_NAME}-${PACKAGE_VERSION}")
set(PACKAGE_BUGREPORT "https://github.com/grpc/grpc/issues/")
project(${PACKAGE_NAME} C CXX)

set(gRPC_INSTALL_BINDIR "bin" CACHE STRING "Installation directory for executables")
set(gRPC_INSTALL_LIBDIR "lib" CACHE STRING "Installation directory for libraries")
set(gRPC_INSTALL_INCLUDEDIR "include" CACHE STRING "Installation directory for headers")
set(gRPC_INSTALL_CMAKEDIR "lib/cmake/${PACKAGE_NAME}" CACHE STRING "Installation directory for cmake config files")

# Options
option(gRPC_BUILD_TESTS "Build tests" OFF)
option(gRPC_BUILD_CODEGEN "Build codegen" ON)

set(gRPC_INSTALL_default ON)
if (NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  # Disable gRPC_INSTALL by default if building as a submodule
  set(gRPC_INSTALL_default OFF)
endif()
set(gRPC_INSTALL ${gRPC_INSTALL_default} CACHE BOOL
    "Generate installation target: gRPC_ZLIB_PROVIDER, gRPC_CARES_PROVIDER, gRPC_SSL_PROVIDER and gRPC_PROTOBUF_PROVIDER must all be \"package\"")

# Providers for third-party dependencies (gRPC_*_PROVIDER properties):
# "module": build the dependency using sources from git submodule (under third_party)
# "package": use cmake's find_package functionality to locate a pre-installed dependency

set(gRPC_ZLIB_PROVIDER "module" CACHE STRING "Provider of zlib library")
set_property(CACHE gRPC_ZLIB_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_CARES_PROVIDER "module" CACHE STRING "Provider of c-ares library")
set_property(CACHE gRPC_CARES_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_SSL_PROVIDER "module" CACHE STRING "Provider of ssl library")
set_property(CACHE gRPC_SSL_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_PROTOBUF_PROVIDER "module" CACHE STRING "Provider of protobuf library")
set_property(CACHE gRPC_PROTOBUF_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_PROTOBUF_PACKAGE_TYPE "" CACHE STRING "Algorithm for searching protobuf package")
set_property(CACHE gRPC_PROTOBUF_PACKAGE_TYPE PROPERTY STRINGS "CONFIG" "MODULE")

set(gRPC_GFLAGS_PROVIDER "module" CACHE STRING "Provider of gflags library")
set_property(CACHE gRPC_GFLAGS_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_BENCHMARK_PROVIDER "module" CACHE STRING "Provider of benchmark library")
set_property(CACHE gRPC_BENCHMARK_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_USE_PROTO_LITE OFF CACHE BOOL "Use the protobuf-lite library")

if(UNIX)
  if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(_gRPC_PLATFORM_LINUX ON)
  elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(_gRPC_PLATFORM_MAC ON)
  elseif(${CMAKE_SYSTEM_NAME} MATCHES "Android")
    set(_gRPC_PLATFORM_ANDROID ON)
  else()
    set(_gRPC_PLATFORM_POSIX ON)
  endif()
endif()
if(WIN32)
  set(_gRPC_PLATFORM_WINDOWS ON)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

add_definitions(-DPB_FIELD_16BIT)

if (MSVC)
  include(cmake/msvc_static_runtime.cmake)
  add_definitions(-D_WIN32_WINNT=0x600 -D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS)
  # needed to compile protobuf
  add_definitions(/wd4065 /wd4506)
  # TODO(jtattermusch): revisit C4267 occurrences throughout the code
  add_definitions(/wd4267)
  # TODO(jtattermusch): needed to build boringssl with VS2017, revisit later
  add_definitions(/wd4987 /wd4774 /wd4819 /wd4996 /wd4619)
endif()

if (gRPC_USE_PROTO_LITE)
  set(_gRPC_PROTOBUF_LIBRARY_NAME "libprotobuf-lite")
  add_definitions("-DGRPC_USE_PROTO_LITE")
else()
  set(_gRPC_PROTOBUF_LIBRARY_NAME "libprotobuf")
endif()

include(cmake/zlib.cmake)
include(cmake/cares.cmake)
include(cmake/protobuf.cmake)
include(cmake/ssl.cmake)
include(cmake/gflags.cmake)
include(cmake/benchmark.cmake)
include(cmake/address_sorting.cmake)

if(NOT MSVC)
  set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -std=c99")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()

if(_gRPC_PLATFORM_MAC)
  set(_gRPC_ALLTARGETS_LIBRARIES ${CMAKE_DL_LIBS} m pthread)
elseif(_gRPC_PLATFORM_ANDROID)
  set(_gRPC_ALLTARGETS_LIBRARIES ${CMAKE_DL_LIBS} m)
elseif(UNIX)
  set(_gRPC_ALLTARGETS_LIBRARIES ${CMAKE_DL_LIBS} rt m pthread)
endif()

if(WIN32 AND MSVC)
  set(_gRPC_BASELIB_LIBRARIES wsock32 ws2_32)
endif()

# Create directory for generated .proto files
set(_gRPC_PROTO_GENS_DIR ${CMAKE_BINARY_DIR}/gens)
file(MAKE_DIRECTORY ${_gRPC_PROTO_GENS_DIR})

#  protobuf_generate_grpc_cpp
#  --------------------------
#
#   Add custom commands to process ``.proto`` files to C++ using protoc and
#   GRPC plugin::
#
#     protobuf_generate_grpc_cpp [<ARGN>...]
#
#   ``ARGN``
#     ``.proto`` files
#
function(protobuf_generate_grpc_cpp)
  if(NOT ARGN)
    message(SEND_ERROR "Error: PROTOBUF_GENERATE_GRPC_CPP() called without any proto files")
    return()
  endif()

  set(_protobuf_include_path -I . -I ${_gRPC_PROTOBUF_WELLKNOWN_INCLUDE_DIR})
  foreach(FIL ${ARGN})
    get_filename_component(ABS_FIL ${FIL} ABSOLUTE)
    get_filename_component(FIL_WE ${FIL} NAME_WE)
    file(RELATIVE_PATH REL_FIL ${CMAKE_CURRENT_SOURCE_DIR} ${ABS_FIL})
    get_filename_component(REL_DIR ${REL_FIL} DIRECTORY)
    set(RELFIL_WE "${REL_DIR}/${FIL_WE}")

    add_custom_command(
      OUTPUT "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.grpc.pb.cc"
             "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.grpc.pb.h"
             "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}_mock.grpc.pb.h"
             "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.pb.cc"
             "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.pb.h"
      COMMAND ${_gRPC_PROTOBUF_PROTOC_EXECUTABLE}
      ARGS --grpc_out=generate_mock_code=true:${_gRPC_PROTO_GENS_DIR}
           --cpp_out=${_gRPC_PROTO_GENS_DIR}
           --plugin=protoc-gen-grpc=$<TARGET_FILE:grpc_cpp_plugin>
           ${_protobuf_include_path}
           ${REL_FIL}
      DEPENDS ${ABS_FIL} ${_gRPC_PROTOBUF_PROTOC} grpc_cpp_plugin
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Running gRPC C++ protocol buffer compiler on ${FIL}"
      VERBATIM)

      set_source_files_properties("${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.grpc.pb.cc" "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.grpc.pb.h"  "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}_mock.grpc.pb.h" "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.pb.cc" "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.pb.h" PROPERTIES GENERATED TRUE)
  endforeach()
endfunction()

include(cmake/generated/targets.cmake)
