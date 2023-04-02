//
// Copyright 2019 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_GPRPP_WORK_SERIALIZER_H
#define GRPC_SRC_CORE_LIB_GPRPP_WORK_SERIALIZER_H

#include <grpc/support/port_platform.h>

#include <functional>

#include "absl/base/thread_annotations.h"

#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"

namespace grpc_core {

// WorkSerializer is a mechanism to schedule callbacks in a synchronized manner.
// All callbacks scheduled on a WorkSerializer instance will be executed
// serially in a borrowed thread. The API provides a FIFO guarantee to the
// execution of callbacks scheduled on the thread.
// When a thread calls Run() with a callback, the thread is considered borrowed.
// The callback might run inline, or it might run asynchronously in a different
// thread that is already inside of Run(). If the callback runs directly inline,
// other callbacks from other threads might also be executed before Run()
// returns. Since an arbitrary set of callbacks might be executed when Run() is
// called, generally no locks should be held while calling Run().
// If a thread wants to preclude the possibility of the callback being invoked
// inline in Run() (for example, if a mutex lock is held and executing callbacks
// inline would cause a deadlock), it should use Schedule() instead and then
// invoke DrainQueue() when it is safe to invoke the callback.
class ABSL_LOCKABLE WorkSerializer {
 public:
  WorkSerializer();

  ~WorkSerializer();

  // Runs a given callback on the work serializer. If there is no other thread
  // currently executing the WorkSerializer, the callback is run immediately. In
  // this case, the current thread is also borrowed for draining the queue for
  // any callbacks that get added in the meantime.
  //
  // If you want to use clang thread annotation to make sure that callback is
  // called by WorkSerializer only, you need to add the annotation to both the
  // lambda function given to Run and the actual callback function like;
  //
  //   void run_callback() {
  //     work_serializer.Run(
  //         []() ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer) {
  //            callback();
  //         }, DEBUG_LOCATION);
  //   }
  //   void callback() ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer) { ... }
  //
  // TODO(yashkt): Replace DebugLocation with absl::SourceLocation
  // once we can start using it directly.
  void Run(std::function<void()> callback, const DebugLocation& location);

  // Schedule \a callback to be run later when the queue of callbacks is
  // drained.
  void Schedule(std::function<void()> callback, const DebugLocation& location);
  // Drains the queue of callbacks.
  void DrainQueue();

 private:
  class WorkSerializerImpl;

  OrphanablePtr<WorkSerializerImpl> impl_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_GPRPP_WORK_SERIALIZER_H
