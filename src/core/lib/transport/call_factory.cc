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

#include <grpc/support/port_platform.h>

#include "src/core/lib/transport/call_factory.h"

#include "src/core/lib/debug/stats.h"
#include "src/core/lib/resource_quota/resource_quota.h"

namespace grpc_core {

CallFactory::CallFactory(const ChannelArgs& args)
    : call_size_estimator_(1024),
      allocator_(args.GetObject<ResourceQuota>()
                     ->memory_quota()
                     ->CreateMemoryOwner()) {}

Arena* CallFactory::CreateArena() {
  const size_t initial_size = call_size_estimator_.CallSizeEstimate();
  global_stats().IncrementCallInitialSize(initial_size);
  return Arena::Create(initial_size, &allocator_);
}

void CallFactory::DestroyArena(Arena* arena) {
  call_size_estimator_.UpdateCallSizeEstimate(arena->TotalUsedBytes());
  arena->Destroy();
}

}  // namespace grpc_core
