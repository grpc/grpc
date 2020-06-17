/*
 *
 * Copyright 2019 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include <functional>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/atomic.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/mpscq.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/iomgr/exec_ctx.h"

#ifndef GRPC_CORE_LIB_IOMGR_WORK_SERIALIZER_H
#define GRPC_CORE_LIB_IOMGR_WORK_SERIALIZER_H

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
class WorkSerializer {
 public:
  WorkSerializer();

  ~WorkSerializer();

  // TODO(yashkt): Replace grpc_core::DebugLocation with absl::SourceLocation
  // once we can start using it directly.
  void Run(std::function<void()> callback,
           const grpc_core::DebugLocation& location);

 private:
  class WorkSerializerImpl;

  OrphanablePtr<WorkSerializerImpl> impl_;
};

} /* namespace grpc_core */

#endif /* GRPC_CORE_LIB_IOMGR_WORK_SERIALIZER_H */
