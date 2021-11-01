//
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
//

#include <grpc/support/port_platform.h>

#include <atomic>
#include <functional>

#include "absl/synchronization/mutex.h"

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/mpscq.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/iomgr/exec_ctx.h"

#ifndef GRPC_CORE_LIB_IOMGR_EXEC_CTX_WORK_SERIALIZER_H
#define GRPC_CORE_LIB_IOMGR_EXEC_CTX_WORK_SERIALIZER_H

namespace grpc_core {

// ExecCtxWorkSerializer is a mechanism to schedule callbacks in a synchronized
// manner through ExecCtx.
//
// All callbacks scheduled on an ExecCtxWorkSerializer instance will be executed
// serially on a thread's ExecCtx. The API provides a FIFO guarantee to the
// execution of callbacks scheduled on the thread.

// When Run() is invoked with a callback, it is added on to a queue which is
// drained when ExecCtx is flushed. The ExecCtx used for drained the callbacks
// is determined by the size of the queue of callbacks. If the queue is empty,
// the thread invoking `Run()` to add a callback to the queue shares its
// ExecCtx. If the queue already has other callbacks, the current callback is
// simply added to the queue.
//
// The prime reason to use ExecCtxWorkSerializer instead of WorkSerializer is
// for the ability to execute callbacks without worrying about the locks being
// held when scheduling a callback.
class ABSL_LOCKABLE ExecCtxWorkSerializer {
 public:
  ExecCtxWorkSerializer();

  ~ExecCtxWorkSerializer();

  // Runs a given callback.
  //
  // If you want to use clang thread annotation to make sure that callback is
  // called by ExecCtxWorkSerializer only, you need to add the annotation to
  // both the lambda function given to Run and the actual callback function
  // like;
  //
  //   void run_callback() {
  //     exec_ctx_work_serializer.Run(
  //         []() ABSL_EXCLUSIVE_LOCKS_REQUIRED(exec_ctx_work_serializer) {
  //            callback();
  //         }, DEBUG_LOCATION);
  //   }
  //   void callback() ABSL_EXCLUSIVE_LOCKS_REQUIRED(exec_ctx_work_serializer) {
  //   ... }
  //
  // TODO(yashkt): Replace grpc_core::DebugLocation with absl::SourceLocation
  // once we can start using it directly.
  void Run(std::function<void()> callback,
           const grpc_core::DebugLocation& location);

 private:
  class ExecCtxWorkSerializerImpl;

  OrphanablePtr<ExecCtxWorkSerializerImpl> impl_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_IOMGR_EXEC_CTX_WORK_SERIALIZER_H
