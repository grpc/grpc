//
//
// Copyright 2018 gRPC authors.
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

#ifndef GRPC_SRC_CPP_EXT_FILTERS_CENSUS_PROMISE_NOTIFICATION_H
#define GRPC_SRC_CPP_EXT_FILTERS_CENSUS_PROMISE_NOTIFICATION_H

#include <grpc/support/port_platform.h>

#include <utility>

#include "absl/base/thread_annotations.h"

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"

// TODO(yashykt): Make this part of the standard promises library.
// Helper class for creating a promise that waits until it is notified.
class PromiseNotification {
 public:
  grpc_core::Poll<int> Wait() {
    grpc_core::MutexLock lock(&mu_);
    if (done_) {
      return 42;
    }
    if (!polled_) {
      waker_ = grpc_core::Activity::current()->MakeOwningWaker();
      polled_ = true;
    }
    return grpc_core::Pending{};
  }

  void Notify() {
    grpc_core::Waker waker;
    {
      grpc_core::MutexLock lock(&mu_);
      done_ = true;
      waker = std::move(waker_);
    }
    waker.Wakeup();
  }

 private:
  grpc_core::Mutex mu_;
  bool done_ ABSL_GUARDED_BY(mu_) = false;
  bool polled_ ABSL_GUARDED_BY(mu_) = false;
  grpc_core::Waker waker_ ABSL_GUARDED_BY(mu_);
};

#endif  // GRPC_SRC_CPP_EXT_FILTERS_CENSUS_PROMISE_NOTIFICATION_H
