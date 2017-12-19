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

if("${gRPC_BENCHMARK_PROVIDER}" STREQUAL "module")
  # Build dependency as external project from git submodule

  include(ExternalProject)

  ExternalProject_Add(benchmark
    PREFIX benchmark
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/benchmark"
    CMAKE_CACHE_ARGS
          ${_gRPC_EP_COMMON_ARGS}
          -DBENCHMARK_ENABLE_TESTING:BOOL=OFF
          -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/benchmark
  )

  add_library(benchmark::benchmark STATIC IMPORTED)
  add_dependencies(benchmark::benchmark benchmark)  # add dependency on the external project
  if(WIN32)
    set_property(TARGET benchmark::benchmark PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/benchmark/lib/benchmark.lib)
  else()
    set_property(TARGET benchmark::benchmark PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/benchmark/lib/libbenchmark.a)
  endif()

  set(_gRPC_BENCHMARK_LIBRARIES benchmark::benchmark)
  set(_gRPC_BENCHMARK_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/benchmark/include)

elseif("${gRPC_BENCHMARK_PROVIDER}" STREQUAL "package")
  # Find pre-installed dependency

  find_package(benchmark REQUIRED)

  if(NOT TARGET benchmark::benchmark)
     message(WARNING "Target benchmark::benchmark not found by find_package")
  endif()
  set(_gRPC_BENCHMARK_LIBRARIES benchmark::benchmark)
  # extract the include dir from target's properties
  get_target_property(_gRPC_BENCHMARK_INCLUDE_DIR benchmark::benchmark INTERFACE_INCLUDE_DIRECTORIES)

  set(_gRPC_FIND_BENCHMARK "if(NOT benchmark_FOUND)\n  find_package(benchmark)\nendif()")
endif()
