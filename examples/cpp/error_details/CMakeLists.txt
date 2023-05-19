# Copyright 2023 the gRPC authors.
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
# cmake build file for C++ helloworld example.
# Assumes protobuf and gRPC have been installed using cmake.
# See cmake_externalproject/CMakeLists.txt for all-in-one cmake build
# that automatically builds all the dependencies before building helloworld.

cmake_minimum_required(VERSION 3.8)

project(ErrorDetails C CXX)

include(../cmake/common.cmake)

# Proto files
get_filename_component(hw_proto "../../protos/helloworld.proto" ABSOLUTE)
get_filename_component(hw_proto_path "${hw_proto}" PATH)
get_filename_component(status_proto "../../../third_party/googleapis/google/rpc/status.proto" ABSOLUTE)
get_filename_component(status_proto_path "${status_proto}" PATH)
get_filename_component(error_details_proto "../../../third_party/googleapis/google/rpc/error_details.proto" ABSOLUTE)
get_filename_component(error_details_proto_path "${error_details_proto}" PATH)
get_filename_component(protobuf_proto_path "../../../third_party/protobuf/src" ABSOLUTE)

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
set(status_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/status.pb.cc")
set(status_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/status.pb.h")
add_custom_command(
      OUTPUT "${status_proto_srcs}" "${status_proto_hdrs}"
      COMMAND ${_PROTOBUF_PROTOC}
      ARGS 
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${status_proto_path}"
        -I "${protobuf_proto_path}"
        "${status_proto}"
      DEPENDS "${status_proto}")      
set(error_details_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/error_details.pb.cc")
set(error_details_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/error_details.pb.h")
add_custom_command(
      OUTPUT "${error_details_proto_srcs}" "${error_details_proto_hdrs}"
      COMMAND ${_PROTOBUF_PROTOC}
      ARGS 
          --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
          -I "${error_details_proto_path}"
          -I "${protobuf_proto_path}"
          "${error_details_proto}"
      DEPENDS "${error_details_proto}")

# Include generated *.pb.h files
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

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

# status_proto
add_library(status_proto
  ${status_proto_srcs}
  ${status_proto_hdrs})
target_link_libraries(status_proto
  ${_PROTOBUF_LIBPROTOBUF})

# error_details_proto
add_library(error_details_proto
  ${error_details_proto_srcs}
  ${error_details_proto_hdrs})
target_link_libraries(error_details_proto
  ${_PROTOBUF_LIBPROTOBUF})

# Targets greeter_(client|server)
foreach(_target
  greeter_client greeter_server)
  add_executable(${_target} "${_target}.cc")
  target_link_libraries(${_target}
    hw_grpc_proto
    status_proto
    error_details_proto
    absl::flags
    absl::flags_parse
    ${_REFLECTION}
    ${_GRPC_GRPCPP}
    ${_PROTOBUF_LIBPROTOBUF})
endforeach()
