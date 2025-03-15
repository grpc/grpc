//
//
// Copyright 2025 gRPC authors.
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
//
//

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_CF_ENGINE_DISPATCH_THREAD_POOL_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_CF_ENGINE_DISPATCH_THREAD_POOL_H

#include <grpc/support/port_platform.h>

#ifdef GPR_APPLE
#include <AvailabilityMacros.h>
#ifdef AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER

#include "src/core/lib/event_engine/thread_pool/thread_pool.h"

namespace grpc_event_engine::experimental {

// thread pool that uses Grand Central Dispatch (GCD) dispatch queues
class DispatchThreadPool final : public ThreadPool {
 public:
  // Asserts Quiesce was called.
  ~DispatchThreadPool() override {}
  // Shut down the pool, and wait for all threads to exit.
  // This method is safe to call from within a ThreadPool thread.
  void Quiesce() override {};
  // Run must not be called after Quiesce completes
  void Run(absl::AnyInvocable<void()> callback) override;
  void Run(EventEngine::Closure* closure) override;

  // Forkable
  // These methods are exposed on the public object to allow for testing.
  void PrepareFork() override {}
  void PostforkParent() override {}
  void PostforkChild() override {}
};

}  // namespace grpc_event_engine::experimental

#endif  // AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER
#endif  // GPR_APPLE

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_CF_ENGINE_DISPATCH_THREAD_POOL_H