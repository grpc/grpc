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
extern DebugOnlyTraceFlag grpc_work_serializer_trace;

class WorkSerializerImpl : public Orphanable {
 public:
  void Run(std::function<void()> callback,
           const grpc_core::DebugLocation& location);

  void Orphan() override;

 private:
  void DrainQueue();

  // An initial size of 1 keeps track of whether the work serializer has been
  // orphaned.
  Atomic<size_t> size_{1};
  MultiProducerSingleConsumerQueue queue_;
};

// WorkSerializer is a mechanism to schedule callbacks in a synchronized manner.
// All callbacks scheduled on a WorkSerializer instance will be executed
// serially in a borrowed thread. The API provides a FIFO guarantee to the
// execution of callbacks scheduled on the thread.
class WorkSerializer {
 public:
  WorkSerializer() { impl_ = MakeOrphanable<WorkSerializerImpl>(); }

  // TODO(yashkt): Replace grpc_core::DebugLocation with absl::SourceLocation
  // once we can start using it directly.
  void Run(std::function<void()> callback,
           const grpc_core::DebugLocation& location) {
    impl_->Run(callback, location);
  }

 private:
  OrphanablePtr<WorkSerializerImpl> impl_;
};

} /* namespace grpc_core */

#endif /* GRPC_CORE_LIB_IOMGR_WORK_SERIALIZER_H */
