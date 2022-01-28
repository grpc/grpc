# Copyright 2022 The gRPC Authors
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

# Configure ccache if requested by environment variable GRPC_BUILD_ENABLE_CCACHE

if ($ENV{GRPC_BUILD_ENABLE_CCACHE})
  find_program(gRPC_CCACHE_BINARY ccache)
  if(gRPC_CCACHE_BINARY)
    message(STATUS "Will use ccache as compiler launcher: ${gRPC_CCACHE_BINARY}")
    set(CMAKE_C_COMPILER_LAUNCHER   ${gRPC_CCACHE_BINARY})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${gRPC_CCACHE_BINARY})
  else()
    message(STATUS "Build will not use ccache (ccache binary not found).")
  endif()
endif()