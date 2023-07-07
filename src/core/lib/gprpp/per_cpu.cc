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

#include <grpc/support/cpu.h>

#include "src/core/lib/gpr/useful.h"

namespace grpc_core {

size_t PerCpuOptions::Shards() {
  return ShardsForCpuCount(gpr_cpu_num_cores());
}

size_t PerCpuOptions::ShardsForCpuCount(size_t cpu_count) {
  return Clamp<size_t>(cpu_count / cpus_per_shard_, 1, max_shards_);
}

}  // namespace grpc_core
