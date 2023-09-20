// Copyright 2022 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_GPRPP_PER_CPU_H
#define GRPC_SRC_CORE_LIB_GPRPP_PER_CPU_H

#include <grpc/support/port_platform.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>

#include "src/core/lib/iomgr/exec_ctx.h"

// Sharded collections of objects
// This used to be per-cpu, now it's much less so - but still a way to limit
// contention.

namespace grpc_core {

class PerCpuOptions {
 public:
  // Set the number of cpus that colocate on the same shard
  PerCpuOptions SetCpusPerShard(size_t cpus_per_shard) {
    cpus_per_shard_ = std::max<size_t>(1, cpus_per_shard);
    return *this;
  }

  // Set the maximum number of allowable shards
  PerCpuOptions SetMaxShards(size_t max_shards) {
    max_shards_ = std::max<size_t>(1, max_shards);
    return *this;
  }

  size_t cpus_per_shard() const { return cpus_per_shard_; }
  size_t max_shards() const { return max_shards_; }

  size_t Shards();
  size_t ShardsForCpuCount(size_t cpu_count);

 private:
  size_t cpus_per_shard_ = 1;
  size_t max_shards_ = std::numeric_limits<size_t>::max();
};

class PerCpuShardingHelper {
 protected:
  size_t GetShardingBits() { return per_thread_id_; }

 private:
  static thread_local size_t per_thread_id_;
};

template <typename T>
class PerCpu : public PerCpuShardingHelper {
 public:
  // Options are not defaulted to try and force consideration of what the
  // options specify.
  explicit PerCpu(PerCpuOptions options) : shards_(options.Shards()) {}

  T& this_cpu() { return data_[GetShardingBits() % shards_]; }

  T* begin() { return data_.get(); }
  T* end() { return data_.get() + shards_; }
  const T* begin() const { return data_.get(); }
  const T* end() const { return data_.get() + shards_; }

 private:
  const size_t shards_;
  std::unique_ptr<T[]> data_{new T[shards_]};
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_GPRPP_PER_CPU_H
