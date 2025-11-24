# Copyright 2018 gRPC authors.
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
#
# cmake build file for C++ gRPC OpenTelemetry example.
# Assumes absl, protobuf, prometheus-cpp, opentelemetry-cpp and gRPC (with -DgRPC_BUILD_OPENTELEMETRY_PLUGIN=ON) have been installed using cmake.
# See cmake_externalproject/CMakeLists.txt for all-in-one cmake build
# that automatically builds all the dependencies before building helloworld.

cmake_minimum_required(VERSION 3.16)

project(grpc_opentelemetry_example C CXX)

include(../cmake/common.cmake)

# Find prometheus-cpp package
find_package(prometheus-cpp CONFIG REQUIRED)

# Find opentelemetry-cpp package
find_package(opentelemetry-cpp CONFIG REQUIRED)

# Proto file
get_filename_component(hw_proto "../../protos/helloworld.proto" ABSOLUTE)
get_filename_component(hw_proto_path "${hw_proto}" PATH)

# Generated sources
set(hw_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/helloworld.pb.cc")
set(hw_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/helloworld.pb.h")
set(hw_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/helloworld.grpc.pb.cc")
set(hw_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/helloworld.grpc.pb.h")
add_custom_command(
      OUTPUT "${hw_proto_srcs}" "${hw_proto_hdrs}" "${hw_grpc_srcs}" "${hw_grpc_hdrs}"
      COMMAND ${_PROTOBUF_PROTOC}
      ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${hw_proto_path}"
        --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${hw_proto}"
      DEPENDS "${hw_proto}")

# Include generated *.pb.h files
include_directories("${CMAKE_CURRENT_BINARY_DIR}")
include_directories("${CMAKE_SOURCE_DIR}")

# hw_grpc_proto
add_library(hw_grpc_proto
  ${hw_grpc_srcs}
  ${hw_grpc_hdrs}
  ${hw_proto_srcs}
  ${hw_proto_hdrs})
target_link_libraries(hw_grpc_proto
  ${_REFLECTION}
  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF})

# util
add_library(util
  "util.cc")
target_link_libraries(util
  hw_grpc_proto
  opentelemetry-cpp::metrics
  ${_GRPC_GRPCPP}
  ${_REFLECTION}
  ${_PROTOBUF_LIBPROTOBUF})

# Targets greeter_callback_(client|server)
foreach(_target
  greeter_callback_client greeter_callback_server)
  add_executable(${_target} "${_target}.cc")
  target_link_libraries(${_target}
    absl::flags
    absl::flags_parse
    absl::log_initialize
    opentelemetry-cpp::metrics
    opentelemetry-cpp::prometheus_exporter
    gRPC::grpcpp_otel_plugin
    util)
endforeach()
