# Copyright 2024 gRPC authors.
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

if(TARGET opentelemetry-cpp::api)
  # If opentelemetry is included already, skip including it.
# OpenTelemetry does not work with "module" mode at present.
# elseif(gRPC_OPENTELEMETRY_PROVIDER STREQUAL "module")
#  if(NOT OPENTELEMETRY_ROOT_DIR)
#    set(OPENTELEMETRY_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/opentelemetry-cpp)
#  endif()
#  set(BUILD_TESTING OFF)
#  if(NOT gRPC_BUILD_TESTS)
#    set(WITH_API_ONLY ON)
#  endif()
#  set(WITH_ABSEIL ON)
#  include_directories(${OPENTELEMETRY_ROOT_DIR} "${OPENTELEMETRY_ROOT_DIR}/api/include")
#  add_subdirectory(${OPENTELEMETRY_ROOT_DIR} third_party/opentelemetry-cpp)
#  if(EXISTS "${OPENTELEMETRY_ROOT_DIR}/CMakeLists.txt")
    # Unclear whether we should install OpenTelemetry along with gRPC
    # if(gRPC_INSTALL  AND _gRPC_INSTALL_SUPPORTED_FROM_MODULE)
    #  set(OPENTELEMETRY_INSTALL ON)
    # endif()
#  else()
#    message(WARNING "gRPC_OPENTELEMETRY_PROVIDER is \"module\" but OPENTELEMETRY_ROOT_DIR is wrong")
#  endif()
#  if(gRPC_INSTALL AND NOT _gRPC_INSTALL_SUPPORTED_FROM_MODULE)
#    message(WARNING "gRPC_INSTALL will be forced to FALSE because gRPC_OPENTELEMETRY_PROVIDER is \"module\" and CMake version (${CMAKE_VERSION}) is less than 3.13.")
#    set(gRPC_INSTALL FALSE)
#  endif()
elseif(gRPC_OPENTELEMETRY_PROVIDER STREQUAL "package")
  find_package(opentelemetry-cpp CONFIG REQUIRED)
endif()
set(_gRPC_FIND_OPENTELEMETRY "if(NOT TARGET opentelemetry-cpp::opentelemetry_api)\n  find_package(opentelemetry-cpp)\nendif()")
