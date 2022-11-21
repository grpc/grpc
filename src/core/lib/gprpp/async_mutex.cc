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

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/async_mutex.h"

#include "src/core/lib/gprpp/notification.h"

namespace grpc_core {

namespace {
void RunCallbacks(std::vector<absl::AnyInvocable<void()>> callbacks) {
  for (auto& callback : callbacks) {
    callback();
  }
}
}  // namespace

AsyncMutex::AsyncMutex(
    grpc_event_engine::experimental::EventEngine* event_engine)
    : event_engine_(event_engine) {}

AsyncMutex::~AsyncMutex() {
  MutexLock lock(&mu_);
  if (owner_ != nullptr) {
    // TODO(ctiller): this will fail with single threaded event engines.
    // In those cases we'll want to steal the ownership back and execute
    // callbacks inline here.
    // Figure out how to make that possible!
    Notification done;
    owner_->EnqueueLowPriority([&done] { done.Notify(); });
    done.WaitForNotification();
  }
}

void AsyncMutex::StartOffloadOwner() {
  event_engine_->Run([owner = std::make_unique<Owner>(this)] {});
}

void AsyncMutex::Owner::Shutdown(AsyncMutex* mutex) {
  while (true) {
    ReleasableMutexLock lock(&mutex->mu_);
    auto run = TakeHighPriorityQueue();
    if (!run.empty()) {
      lock.Release();
      RunCallbacks(std::move(run));
      continue;
    }
    run = TakeLowPriorityQueue();
    if (!run.empty()) {
      lock.Release();
      RunCallbacks(std::move(run));
      continue;
    }
    mutex->owner_ = nullptr;
    return;
  }
}

void AsyncMutex::InlineOwner::Shutdown(AsyncMutex* mutex) {
  while (true) {
    ReleasableMutexLock lock(&mutex->mu_);
    auto run = TakeHighPriorityQueue();
    if (GPR_UNLIKELY(!run.empty())) {
      mutex->owner_ = nullptr;
      mutex->event_engine_->Run(
          [owner = std::make_unique<Owner>(mutex, TakeLowPriorityQueue(),
                                           std::move(run))] {});
      return;
    }
    run = TakeLowPriorityQueue();
    if (!run.empty()) {
      lock.Release();
      RunCallbacks(std::move(run));
      continue;
    }
    mutex->owner_ = nullptr;
    return;
  }
}

}  // namespace grpc_core
