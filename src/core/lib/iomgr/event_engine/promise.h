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
#ifndef GRPC_CORE_LIB_IOMGR_EVENT_ENGINE_PROMISE_H
#define GRPC_CORE_LIB_IOMGR_EVENT_ENGINE_PROMISE_H
#include <grpc/support/port_platform.h>

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
  T& Get() {
    absl::MutexLock lock(&mu_);
    cv_.Wait(&mu_);
    return val_;
  }
  void Set(T&& val) {
    absl::MutexLock lock(&mu_);
    val_ = std::move(val);
    cv_.Signal();
  }

 private:
  absl::Mutex mu_;
  absl::CondVar cv_;
  T val_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_IOMGR_EVENT_ENGINE_PROMISE_H
