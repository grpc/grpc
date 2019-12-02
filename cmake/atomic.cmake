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

include(CheckCXXSourceCompiles)
include(CMakePushCheckState)

set(ATOMIC_TEST_CXX_SOURCE "
#include <atomic>
#include <cstdint>
std::atomic<std::int64_t> v;
int main() {
  return v;
}")

# Determine if libatomic is needed for C++ atomics.
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_FLAGS -std=c++11)
check_cxx_source_compiles("${ATOMIC_TEST_CXX_SOURCE}" HAVE_ATOMICS_WITHOUT_LIBATOMIC)
if(NOT HAVE_ATOMICS_WITHOUT_LIBATOMIC)
    set(CMAKE_REQUIRED_LIBRARIES atomic)
    check_cxx_source_compiles("${ATOMIC_TEST_CXX_SOURCE}" HAVE_ATOMICS_WITH_LIBATOMIC)
    if(HAVE_ATOMICS_WITH_LIBATOMIC)
        set(_gRPC_ATOMIC_LIBRARIES atomic)
    else()
        message(FATAL_ERROR "Could not determine support for atomic operations.")
    endif()
endif()
cmake_pop_check_state()
