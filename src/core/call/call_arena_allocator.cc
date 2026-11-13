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

#include "src/core/call/call_arena_allocator.h"

#include <grpc/support/port_platform.h>

#include <algorithm>

namespace grpc_core {

void CallArenaAllocator::FinalizeArena(Arena* arena, size_t initial_zone_size,
                                       size_t total_used) {
  call_size_estimator_.UpdateCallSizeEstimate(total_used);
  const size_t size =
      Arena::RoundedInitialSize(call_size_estimator_.CallSizeEstimate());
  size_t expected_pooled_size = pooled_size_.load(std::memory_order_relaxed);
  if (expected_pooled_size != size) {
    if (pooled_size_.compare_exchange_strong(expected_pooled_size, size,
                                             std::memory_order_relaxed)) {
      pool_.Clear();
    }
  }
  if (initial_zone_size == size && pool_.TryPush(arena)) {
    return;
  }
  gpr_free_aligned(arena);
}

}  // namespace grpc_core
