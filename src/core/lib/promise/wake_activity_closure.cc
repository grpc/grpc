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

#include "src/core/lib/promise/wake_activity_closure.h"

#include "src/core/lib/promise/activity.h"

namespace grpc_core {

// Construct a closure that will wake the current activity when invoked.
grpc_closure* MakeWakeActivityClosure() {
  struct Callback : public grpc_closure {
    Waker waker;
  };
  auto* cb = new Callback;
  auto wakeable = Activity::current()->MakeNonOwningWaker();
  auto wakeup = [](void* p, grpc_error_handle) {
    auto* c = static_cast<Callback*>(p);
    c->waker.Wakeup();
    delete c;
  };
  GRPC_CLOSURE_INIT(cb, wakeup, cb, nullptr);
  return cb;
}

}  // namespace grpc_core
