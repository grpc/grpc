// Copyright 2023 gRPC authors.
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

#include "src/core/lib/gprpp/per_cpu.h"

#include <atomic>

#include "absl/random/random.h"

#include <grpc/support/cpu.h>

#include "src/core/lib/gpr/useful.h"

namespace grpc_core {

namespace {
std::atomic<size_t> g_counter{0};
}
thread_local size_t PerCpuShardingHelper::per_thread_id_ =
    g_counter.fetch_add(1, std::memory_order_relaxed);

size_t PerCpuOptions::Shards() {
  return ShardsForCpuCount(gpr_cpu_num_cores());
}

size_t PerCpuOptions::ShardsForCpuCount(size_t cpu_count) {
  return Clamp<size_t>(cpu_count / cpus_per_shard_, 1, max_shards_);
}

}  // namespace grpc_core
