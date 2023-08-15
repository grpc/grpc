// Copyright 2023 The gRPC Authors
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
#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_THREAD_POOL_THREAD_COUNT_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_THREAD_POOL_THREAD_COUNT_H

#include <grpc/support/port_platform.h>

#include <atomic>
#include <cstddef>
#include <vector>

#include "absl/base/thread_annotations.h"

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_event_engine {
namespace experimental {

// Tracks counts across some fixed number of shards.
// It is intended for fast increment/decrement operations, but a slower overall
// count operation.
class BusyThreadCount {
 public:
  // Increments a per-shard counter on construction, decrements on destruction.
  class AutoThreadCounter {
   public:
    AutoThreadCounter(BusyThreadCount* counter, size_t idx);
    ~AutoThreadCounter();
    AutoThreadCounter(AutoThreadCounter&& other) noexcept;
    AutoThreadCounter& operator=(AutoThreadCounter&& other) noexcept;

   private:
    BusyThreadCount* counter_;
    size_t idx_;
  };

  BusyThreadCount();
  AutoThreadCounter MakeAutoThreadCounter(size_t idx);
  void Increment(size_t idx);
  void Decrement(size_t idx);
  size_t count();
  // Returns some valid index into the per-shard data, which is rotated on every
  // call to distribute load and reduce contention.
  size_t NextIndex();

 private:
  struct ShardedData {
    std::atomic<size_t> busy_count{0};
  } GPR_ALIGN_STRUCT(GPR_CACHELINE_SIZE);

  std::vector<ShardedData> shards_;
  std::atomic<size_t> next_idx_{0};
};

// Tracks the number of living threads. It is intended for a fast count
// operation, with relatively slower increment/decrement operations.
class LivingThreadCount {
 public:
  // Increments the global counter on construction, decrements on destruction.
  class AutoThreadCounter {
   public:
    explicit AutoThreadCounter(LivingThreadCount* counter);
    ~AutoThreadCounter();
    AutoThreadCounter(AutoThreadCounter&& other) noexcept;
    AutoThreadCounter& operator=(AutoThreadCounter&& other) noexcept;

   private:
    LivingThreadCount* counter_;
  };

  AutoThreadCounter MakeAutoThreadCounter();
  void Increment() ABSL_LOCKS_EXCLUDED(mu_);
  void Decrement() ABSL_LOCKS_EXCLUDED(mu_);
  void BlockUntilThreadCount(size_t desired_threads,
                             grpc_core::Duration timeout, const char* why)
      ABSL_LOCKS_EXCLUDED(mu_);
  size_t count() ABSL_LOCKS_EXCLUDED(mu_);

 private:
  size_t CountLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  size_t WaitForCountChange(size_t desired_threads,
                            grpc_core::Duration timeout);

  grpc_core::Mutex mu_;
  grpc_core::CondVar cv_ ABSL_GUARDED_BY(mu_);
  size_t living_count_ ABSL_GUARDED_BY(mu_) = 0;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_THREAD_POOL_THREAD_COUNT_H
