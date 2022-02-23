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

#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

// Promise that sleeps until a deadline and then finishes.
class Sleep {
 public:
  explicit Sleep(grpc_millis deadline);
  ~Sleep();

  Sleep(const Sleep&) = delete;
  Sleep& operator=(const Sleep&) = delete;
  Sleep(Sleep&& other) noexcept : state_(other.state_) {
    other.state_ = nullptr;
  }
  Sleep& operator=(Sleep&& other) noexcept {
    std::swap(state_, other.state_);
    return *this;
  }

  Poll<absl::Status> operator()();

 private:
  static void OnTimer(void* arg, grpc_error_handle error);

  enum class Stage { kInitial, kStarted, kDone };
  struct State {
    explicit State(grpc_millis deadline) : deadline(deadline) {}
    RefCount refs{2};
    const grpc_millis deadline;
    grpc_timer timer;
    grpc_closure on_timer;
    Mutex mu;
    Stage stage ABSL_GUARDED_BY(mu) = Stage::kInitial;
    Waker waker ABSL_GUARDED_BY(mu);
    void Unref() {
      if (refs.Unref()) delete this;
    }
  };
  State* state_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_SLEEP_H
