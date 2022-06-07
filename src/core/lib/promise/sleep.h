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

#ifndef GRPC_CORE_LIB_PROMISE_SLEEP_H
#define GRPC_CORE_LIB_PROMISE_SLEEP_H

#include <grpc/support/port_platform.h>

#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

// Promise that sleeps until a deadline and then finishes.
class Sleep {
 public:
  explicit Sleep(Timestamp deadline);
  ~Sleep();

  Sleep(const Sleep&) = delete;
  Sleep& operator=(const Sleep&) = delete;
  Sleep(Sleep&& other) noexcept
      : deadline_(other.deadline_), timer_handle_(other.timer_handle_) {
    MutexLock lock2(&other.mu_);
    stage_ = other.stage_;
    waker_ = std::move(other.waker_);
    other.deadline_ = Timestamp::InfPast();
  };
  Sleep& operator=(Sleep&& other) noexcept {
    if (&other == this) return *this;
    MutexLock lock1(&mu_);
    MutexLock lock2(&other.mu_);
    deadline_ = other.deadline_;
    timer_handle_ = other.timer_handle_;
    stage_ = other.stage_;
    waker_ = std::move(other.waker_);
    other.deadline_ = Timestamp::InfPast();
    return *this;
  };

  Poll<absl::Status> operator()();

 private:
  enum class Stage { kInitial, kStarted, kDone };
  void OnTimer();

  Timestamp deadline_;
  grpc_event_engine::experimental::EventEngine::TaskHandle timer_handle_;
  Mutex mu_;
  Stage stage_ ABSL_GUARDED_BY(mu_) = Stage::kInitial;
  Waker waker_ ABSL_GUARDED_BY(mu_);
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_SLEEP_H
