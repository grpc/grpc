// Copyright 2021 The gRPC Authors
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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_PROMISE_H
#define GRPC_CORE_LIB_EVENT_ENGINE_PROMISE_H
#include <grpc/support/port_platform.h>

#include "absl/time/time.h"

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/sync.h"

namespace grpc_event_engine {
namespace experimental {

/// A minimal promise implementation.
///
/// This is light-duty, syntactical sugar around cv wait & signal, which is
/// useful in some cases. A more robust implementation is being worked on
/// separately.
template <typename T>
class Promise {
 public:
  Promise() = default;
  // Initialize a default value that will be returned if WaitWithTimeout times
  // out
  explicit Promise(T&& val) : val_(val) {}
  // The getter will wait until the setter has been called, and will return the
  // value passed during Set.
  T& Get() { return WaitWithTimeout(absl::Hours(1)); }
  // The getter will wait with timeout until the setter has been called, and
  // will return the value passed during Set.
  T& WaitWithTimeout(absl::Duration d) {
    grpc_core::MutexLock lock(&mu_);
    if (!set_) {
      cv_.WaitWithTimeout(&mu_, d);
    }
    return val_;
  }
  // This setter can only be called exactly once without a Reset.
  // Will automatically unblock getters.
  void Set(T&& val) {
    grpc_core::MutexLock lock(&mu_);
    GPR_ASSERT(!set_);
    val_ = std::move(val);
    set_ = true;
    cv_.SignalAll();
  }

  // Can only be called after a set operation.
  void Reset() {
    grpc_core::MutexLock lock(&mu_);
    GPR_ASSERT(set_);
    set_ = false;
  }

 private:
  grpc_core::Mutex mu_;
  grpc_core::CondVar cv_;
  T val_;
  bool set_ = false;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_PROMISE_H
