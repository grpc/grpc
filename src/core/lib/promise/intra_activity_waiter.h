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

#ifndef GRPC_CORE_LIB_PROMISE_INTRA_ACTIVITY_WAITER_H
#define GRPC_CORE_LIB_PROMISE_INTRA_ACTIVITY_WAITER_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

// Helper type to track wakeups between objects in the same activity.
// Can be fairly fast as no ref counting or locking needs to occur.
class IntraActivityWaiter {
 public:
  // Register for wakeup, return Pending(). If state is not ready to proceed,
  // Promises should bottom out here.
  Pending pending() {
    waiting_ = true;
    return Pending();
  }
  // Wake the activity
  void Wake() {
    if (waiting_) {
      waiting_ = false;
      Activity::current()->ForceImmediateRepoll();
    }
  }

 private:
  bool waiting_ = false;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_INTRA_ACTIVITY_WAITER_H
