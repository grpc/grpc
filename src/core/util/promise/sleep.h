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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_SLEEP_H
#define GRPC_SRC_CORE_LIB_PROMISE_SLEEP_H

#include <atomic>
#include <utility>

#include "absl/status/status.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

// Promise that sleeps until a deadline and then finishes.
class Sleep final {
 public:
  explicit Sleep(Timestamp deadline);
  ~Sleep();

  Sleep(const Sleep&) = delete;
  Sleep& operator=(const Sleep&) = delete;
  Sleep(Sleep&& other) noexcept
      : deadline_(other.deadline_),
        closure_(std::exchange(other.closure_, nullptr)) {}
  Sleep& operator=(Sleep&& other) noexcept {
    deadline_ = other.deadline_;
    std::swap(closure_, other.closure_);
    return *this;
  };

  Poll<absl::Status> operator()();

 private:
  class ActiveClosure final
      : public grpc_event_engine::experimental::EventEngine::Closure {
   public:
    explicit ActiveClosure(Timestamp deadline);

    void Run() override;
    // After calling Cancel, it's no longer safe to access this object.
    void Cancel();

    bool HasRun() const;

   private:
    bool Unref();

    Waker waker_;
    // One ref dropped by Run(), the other by Cancel().
    std::atomic<int> refs_{2};
    grpc_event_engine::experimental::EventEngine::TaskHandle timer_handle_;
  };

  Timestamp deadline_;
  ActiveClosure* closure_{nullptr};
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_SLEEP_H
