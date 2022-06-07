/*
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

#include <grpcpp/impl/codegen/server_callback.h>
#include <grpcpp/support/server_callback.h>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/executor.h"

namespace grpc {
namespace internal {

void ServerCallbackCall::ScheduleOnDone(bool inline_ondone) {
  if (inline_ondone) {
    CallOnDone();
  } else {
    // Unlike other uses of closure, do not Ref or Unref here since at this
    // point, all the Ref'fing and Unref'fing is done for this call.
    grpc_core::ExecCtx exec_ctx;
    struct ClosureWithArg {
      grpc_closure closure;
      ServerCallbackCall* call;
      explicit ClosureWithArg(ServerCallbackCall* call_arg) : call(call_arg) {
        GRPC_CLOSURE_INIT(
            &closure,
            [](void* void_arg, grpc_error_handle) {
              ClosureWithArg* arg = static_cast<ClosureWithArg*>(void_arg);
              arg->call->CallOnDone();
              delete arg;
            },
            this, grpc_schedule_on_exec_ctx);
      }
    };
    ClosureWithArg* arg = new ClosureWithArg(this);
    grpc_core::Executor::Run(&arg->closure, GRPC_ERROR_NONE);
  }
}

void ServerCallbackCall::CallOnCancel(ServerReactor* reactor) {
  if (reactor->InternalInlineable()) {
    reactor->OnCancel();
  } else {
    // Ref to make sure that the closure executes before the whole call gets
    // destructed, and Unref within the closure.
    Ref();
    grpc_core::ExecCtx exec_ctx;
    struct ClosureWithArg {
      grpc_closure closure;
      ServerCallbackCall* call;
      ServerReactor* reactor;
      ClosureWithArg(ServerCallbackCall* call_arg, ServerReactor* reactor_arg)
          : call(call_arg), reactor(reactor_arg) {
        GRPC_CLOSURE_INIT(
            &closure,
            [](void* void_arg, grpc_error_handle) {
              ClosureWithArg* arg = static_cast<ClosureWithArg*>(void_arg);
              arg->reactor->OnCancel();
              arg->call->MaybeDone();
              delete arg;
            },
            this, grpc_schedule_on_exec_ctx);
      }
    };
    ClosureWithArg* arg = new ClosureWithArg(this, reactor);
    grpc_core::Executor::Run(&arg->closure, GRPC_ERROR_NONE);
  }
}

}  // namespace internal
}  // namespace grpc
