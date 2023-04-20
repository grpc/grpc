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
#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/work_queue/basic_work_queue.h"

#include <utility>

#include "src/core/lib/event_engine/common_closures.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_event_engine {
namespace experimental {

bool BasicWorkQueue::Empty() const {
  grpc_core::MutexLock lock(&mu_);
  return q_.empty();
}

size_t BasicWorkQueue::Size() const {
  grpc_core::MutexLock lock(&mu_);
  return q_.size();
}

EventEngine::Closure* BasicWorkQueue::PopMostRecent() {
  grpc_core::MutexLock lock(&mu_);
  if (q_.empty()) return nullptr;
  auto tmp = q_.back();
  q_.pop_back();
  return tmp;
}

EventEngine::Closure* BasicWorkQueue::PopOldest() {
  grpc_core::MutexLock lock(&mu_);
  if (q_.empty()) return nullptr;
  auto tmp = q_.front();
  q_.pop_front();
  return tmp;
}

void BasicWorkQueue::Add(EventEngine::Closure* closure) {
  grpc_core::MutexLock lock(&mu_);
  q_.push_back(closure);
}

void BasicWorkQueue::Add(absl::AnyInvocable<void()> invocable) {
  grpc_core::MutexLock lock(&mu_);
  q_.push_back(SelfDeletingClosure::Create(std::move(invocable)));
}

}  // namespace experimental
}  // namespace grpc_event_engine
