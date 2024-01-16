// Copyright 2021 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_INTER_ACTIVITY_LATCH_H
#define GRPC_SRC_CORE_LIB_PROMISE_INTER_ACTIVITY_LATCH_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/strings/str_cat.h"

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/trace.h"
#include "src/core/lib/promise/wait_set.h"

namespace grpc_core {

// A latch providing true cross activity wakeups
template <typename T>
class InterActivityLatch;

template <>
class InterActivityLatch<void> {
 public:
  InterActivityLatch() = default;
  InterActivityLatch(const InterActivityLatch&) = delete;
  InterActivityLatch& operator=(const InterActivityLatch&) = delete;

  // Produce a promise to wait for this latch.
  auto Wait() {
    return [this]() -> Poll<Empty> {
      MutexLock lock(&mu_);
      if (grpc_trace_promise_primitives.enabled()) {
        gpr_log(GPR_INFO, "%sPollWait %s", DebugTag().c_str(),
                StateString().c_str());
      }
      if (is_set_) {
        return Empty{};
      } else {
        return waiters_.AddPending(Activity::current()->MakeNonOwningWaker());
      }
    };
  }

  // Set the latch.
  void Set() {
    MutexLock lock(&mu_);
    if (grpc_trace_promise_primitives.enabled()) {
      gpr_log(GPR_INFO, "%sSet %s", DebugTag().c_str(), StateString().c_str());
    }
    is_set_ = true;
    waiters_.WakeupAsync();
  }

  bool IsSet() const ABSL_LOCKS_EXCLUDED(mu_) {
    MutexLock lock(&mu_);
    return is_set_;
  }

 private:
  std::string DebugTag() {
    return absl::StrCat(Activity::current()->DebugTag(),
                        " INTER_ACTIVITY_LATCH[0x",
                        reinterpret_cast<uintptr_t>(this), "]: ");
  }

  std::string StateString() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return absl::StrCat("is_set:", is_set_);
  }

  mutable Mutex mu_;
  // True if we have a value set, false otherwise.
  bool is_set_ = false;
  WaitSet waiters_ ABSL_GUARDED_BY(mu_);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_INTER_ACTIVITY_LATCH_H
