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

    # avoid conflicts when multiple processes try to write to PDB files. Instead make debug info part of object files.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
      string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
      string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
      string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
      string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
    elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
      string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
      string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
    endif()
  else()
    message(STATUS "Build will not use ccache (ccache binary not found).")
  endif()
endif()