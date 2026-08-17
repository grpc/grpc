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
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opentelemetry-cpp/CMakeLists.txt")
  if(NOT OPENTELEMETRY_ROOT_DIR)
    set(OPENTELEMETRY_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/opentelemetry-cpp)
  endif()
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(WITH_BENCHMARK OFF CACHE BOOL "" FORCE)
  set(WITH_ABSEIL ON CACHE BOOL "" FORCE)
  set(WITH_OTLP_GRPC OFF CACHE BOOL "" FORCE)
  set(WITH_OTLP_HTTP OFF CACHE BOOL "" FORCE)
  set(OPENTELEMETRY_INSTALL OFF CACHE BOOL "" FORCE)
  include_directories(${OPENTELEMETRY_ROOT_DIR} "${OPENTELEMETRY_ROOT_DIR}/api/include" "${OPENTELEMETRY_ROOT_DIR}/sdk/include")
  add_subdirectory(${OPENTELEMETRY_ROOT_DIR} third_party/opentelemetry-cpp EXCLUDE_FROM_ALL)
  if(TARGET opentelemetry_api AND NOT TARGET opentelemetry-cpp::api)
    add_library(opentelemetry-cpp::api ALIAS opentelemetry_api)
  endif()
  if(TARGET opentelemetry_trace AND NOT TARGET opentelemetry-cpp::trace)
    add_library(opentelemetry-cpp::trace ALIAS opentelemetry_trace)
  endif()
  if(TARGET opentelemetry_metrics AND NOT TARGET opentelemetry-cpp::metrics)
    add_library(opentelemetry-cpp::metrics ALIAS opentelemetry_metrics)
  endif()
  if(TARGET opentelemetry_exporter_in_memory_span AND NOT TARGET opentelemetry-cpp::in_memory_span_exporter)
    add_library(opentelemetry-cpp::in_memory_span_exporter ALIAS opentelemetry_exporter_in_memory_span)
  endif()
elseif(gRPC_OPENTELEMETRY_PROVIDER STREQUAL "package")
  find_package(opentelemetry-cpp CONFIG REQUIRED)
endif()
set(_gRPC_FIND_OPENTELEMETRY "if(NOT TARGET opentelemetry-cpp::opentelemetry_api)\n  find_package(opentelemetry-cpp)\nendif()")
