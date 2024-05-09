// Copyright 2024 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/core/lib/transport/call_arena_allocator.h"

#include <algorithm>

#include <grpc/support/port_platform.h>

namespace grpc_core {

void CallSizeEstimator::UpdateCallSizeEstimate(size_t size) {
  size_t cur = call_size_estimate_.load(std::memory_order_relaxed);
  if (cur < size) {
    // size grew: update estimate
    call_size_estimate_.compare_exchange_weak(
        cur, size, std::memory_order_relaxed, std::memory_order_relaxed);
    // if we lose: never mind, something else will likely update soon enough
  } else if (cur == size) {
    // no change: holding pattern
  } else if (cur > 0) {
    // size shrank: decrease estimate
    call_size_estimate_.compare_exchange_weak(
        cur, std::min(cur - 1, (255 * cur + size) / 256),
        std::memory_order_relaxed, std::memory_order_relaxed);
    // if we lose: never mind, something else will likely update soon enough
  }
}

}  // namespace grpc_core
