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

#include <grpcpp/impl/codegen/server_callback_impl.h>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/executor.h"

namespace grpc_impl {
namespace internal {

void ServerCallbackCall::CallOnCancel(ServerReactor* reactor) {
  if (reactor->InternalInlineable()) {
    reactor->OnCancel();
  } else {
    Ref();
    grpc_core::ExecCtx exec_ctx;
    struct ClosureArg {
      ServerCallbackCall* call;
      ServerReactor* reactor;
    };
    ClosureArg* arg = new ClosureArg{this, reactor};
    grpc_core::Executor::Run(GRPC_CLOSURE_CREATE(
                                 [](void* void_arg, grpc_error*) {
                                   ClosureArg* arg =
                                       static_cast<ClosureArg*>(void_arg);
                                   arg->reactor->OnCancel();
                                   arg->call->MaybeDone();
                                   delete arg;
                                 },
                                 arg, nullptr),
                             GRPC_ERROR_NONE);
  }
}

}  // namespace internal
}  // namespace grpc_impl
