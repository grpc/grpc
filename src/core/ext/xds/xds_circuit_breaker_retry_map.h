/*
 *
 * Copyright 2021 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_EXT_XDS_CIRCUIT_BREAKER_RETRY_MAP_H
#define GRPC_CORE_EXT_XDS_CIRCUIT_BREAKER_RETRY_MAP_H

#include <grpc/support/port_platform.h>

#include <map>
#include <string>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/atomic.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_core {

class XdsCircuitBreakerRetryMap {
 public:
  using Key = std::string;

  class RetryCounter : public RefCounted<RetryCounter> {
   public:
    explicit RetryCounter(Key key) : key_(std::move(key)) {}
    ~RetryCounter() override;

    uint32_t Load() { return concurrent_requests_.Load(MemoryOrder::SEQ_CST); }
    uint32_t Increment() { return concurrent_requests_.FetchAdd(1); }
    void Decrement() { concurrent_requests_.FetchSub(1); }

   private:
    Key key_;
    Atomic<uint32_t> concurrent_requests_{0};
  };

  static RefCountedPtr<RetryCounter> GetOrCreate(const std::string& cluster);

  // Global Init and Shutdown
  static void Init();
  static void Shutdown();

 private:
  Mutex mu_;
  std::map<Key, RetryCounter*> map_ ABSL_GUARDED_BY(mu_);
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_XDS_XDS_CIRCUIT_BREAKER_RETRY_MAP_H */
