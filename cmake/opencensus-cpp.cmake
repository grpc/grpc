# Copyright 2019 gRPC authors.
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

if(TARGET opencensus-cpp::trace)
  # If opencensus-cpp is included already, skip including it.
elseif(gRPC_OPENCENSUS_PROVIDER STREQUAL "module")
  if(NOT OPENCENSUS_ROOT_DIR)
    set(OPENCENSUS_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencensus-cpp)
  endif()
  if(EXISTS "${OPENCENSUS_ROOT_DIR}/CMakeLists.txt")
    set(OpenCensus_BUILD_TESTING OFF)
    add_subdirectory(${OPENCENSUS_ROOT_DIR} third_party/opencensus-cpp)
  else()
    message(WARNING "gRPC_OPENCENSUS_PROVIDER is \"module\" but OPENCENSUS_ROOT_DIR is wrong")
  endif()
  if(gRPC_INSTALL AND NOT _gRPC_INSTALL_SUPPORTED_FROM_MODULE)
    message(WARNING "gRPC_INSTALL will be forced to FALSE because gRPC_OPENCENSUS_PROVIDER is \"module\" and CMake version (${CMAKE_VERSION}) is less than 3.13.")
    set(gRPC_INSTALL FALSE)
  endif()
elseif(gRPC_OPENCENSUS_PROVIDER STREQUAL "package")
  # Use "CONFIG" as there is no built-in cmake module for opencensus-cpp.
  find_package(OpenCensus REQUIRED CONFIG)
endif()
set(_gRPC_FIND_OPENCENSUS "if(NOT TARGET opencensus-cpp::trace)\n  find_package(OpenCensus CONFIG)\nendif()")
