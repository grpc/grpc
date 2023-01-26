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
//

#include <utility>

#include "absl/status/status.h"

#include <grpc/grpc.h>
#include <grpcpp/support/client_callback.h>
#include <grpcpp/support/status.h>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/surface/call.h"

namespace grpc {
namespace internal {

void ClientReactor::InternalScheduleOnDone(grpc::Status s) {
  // Unlike other uses of closure, do not Ref or Unref here since the reactor
  // object's lifetime is controlled by user code.
  grpc_core::ExecCtx exec_ctx;
  struct ClosureWithArg {
    grpc_closure closure;
    ClientReactor* const reactor;
    const grpc::Status status;
    ClosureWithArg(ClientReactor* reactor_arg, grpc::Status s)
        : reactor(reactor_arg), status(std::move(s)) {
      GRPC_CLOSURE_INIT(
          &closure,
          [](void* void_arg, grpc_error_handle) {
            ClosureWithArg* arg = static_cast<ClosureWithArg*>(void_arg);
            arg->reactor->OnDone(arg->status);
            delete arg;
          },
          this, grpc_schedule_on_exec_ctx);
    }
  };
  ClosureWithArg* arg = new ClosureWithArg(this, std::move(s));
  grpc_core::Executor::Run(&arg->closure, absl::OkStatus());
}

bool ClientReactor::InternalTrailersOnly(const grpc_call* call) const {
  return grpc_call_is_trailers_only(call);
}

}  // namespace internal
}  // namespace grpc
