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
#include <grpc/support/port_platform.h>  // IWYU pragma: keep

#ifdef GPR_APPLE
#include <AvailabilityMacros.h>
#ifdef AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER

#include <dispatch/dispatch.h>
#include <dispatch/queue.h>

#include "src/core/lib/event_engine/cf_engine/dispatch_thread_pool.h"
#include "src/core/lib/event_engine/common_closures.h"

namespace grpc_event_engine::experimental {

static const char* kDispatchQueueMarker = "";

// Callback for dispatch_group_async_f.
static void RunClosure(void* context) {
  auto* closure = static_cast<EventEngine::Closure*>(context);
  if (closure) closure->Run();
}

DispatchThreadPool::DispatchThreadPool()
    : queue_(dispatch_queue_create_with_target(
          "grpc_event_engine", DISPATCH_QUEUE_CONCURRENT,
          dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0))),
      pending_(dispatch_group_create()) {
  dispatch_queue_set_specific(queue_, &kDispatchQueueMarker,
                              const_cast<char*>(kDispatchQueueMarker), NULL);
}

DispatchThreadPool::~DispatchThreadPool() {
  dispatch_release(pending_);
  dispatch_release(queue_);
}

void DispatchThreadPool::Quiesce() {
  cancelled_.store(true);
  // NOTE: thread_pool.h states that this method is safe to call from within a
  // ThreadPool thread. However, this is not possible with dispatch_group_wait.
  // The workaround is to complete without waiting if we are already on the
  // queue.
  bool isOnQueue = !!dispatch_get_specific(&kDispatchQueueMarker);
  if (!isOnQueue) {
    dispatch_time_t long_time_from_now = dispatch_time(DISPATCH_TIME_NOW, 10);
    dispatch_group_wait(pending_, long_time_from_now);
  }
}

void DispatchThreadPool::Run(absl::AnyInvocable<void()> callback) {
  auto work = [cb = std::move(callback), this]() mutable {
    if (!cancelled_.load()) {
      cb();
    }
  };
  dispatch_group_async_f(pending_, queue_,
                         SelfDeletingClosure::Create(std::move(work)),
                         &RunClosure);
}

void DispatchThreadPool::Run(EventEngine::Closure* closure) {
  auto work = [closure, this]() {
    if (!cancelled_.load()) {
      closure->Run();
    }
  };
  dispatch_group_async_f(pending_, queue_,
                         SelfDeletingClosure::Create(std::move(work)),
                         &RunClosure);
}

}  // namespace grpc_event_engine::experimental

#endif  // AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER
#endif  // GPR_APPLE
