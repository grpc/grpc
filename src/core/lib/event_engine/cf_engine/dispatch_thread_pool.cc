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

#include <grpc/support/port_platform.h>

#ifdef GPR_APPLE
#include <AvailabilityMacros.h>
#ifdef AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/lib/event_engine/cf_engine/dispatch_thread_pool.h"
#include "src/core/lib/event_engine/common_closures.h"

namespace grpc_event_engine::experimental {

DispatchThreadPool::DispatchThreadPool(size_t /*unused*/)
    : queue_(dispatch_queue_create("cf_engine_thread_pool",
                                   DISPATCH_QUEUE_CONCURRENT)) {}

DispatchThreadPool::~DispatchThreadPool() {
  CHECK(quiesced_.load(std::memory_order_relaxed));
  dispatch_release(queue_);
}

void DispatchThreadPool::Quiesce() {
  shutdown_.store(true, std::memory_order_relaxed);
  dispatch_barrier_sync_f(queue_, this, [](void* context) {
    auto* self = static_cast<DispatchThreadPool*>(context);
    self->quiesced_.store(true, std::memory_order_relaxed);
  });
}

void DispatchThreadPool::Run(absl::AnyInvocable<void()> callback) {
  Run(SelfDeletingClosure::Create(std::move(callback)));
}

void DispatchThreadPool::Run(EventEngine::Closure* closure) {
  // CHECK(!quiesced_.load(std::memory_order_relaxed));

  dispatch_async_f(queue_, closure, [](void* context) {
    auto* closure = static_cast<EventEngine::Closure*>(context);
    closure->Run();
  });
}

}  // namespace grpc_event_engine::experimental

#endif  // AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER
#endif  // GPR_APPLE
