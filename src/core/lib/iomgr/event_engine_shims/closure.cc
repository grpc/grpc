// Copyright 2021 gRPC Authors
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
#include "src/core/lib/iomgr/event_engine_shims/closure.h"

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_event_engine {
namespace experimental {

void RunEventEngineClosure(grpc_closure* closure, grpc_error_handle error) {
  if (closure == nullptr) {
    return;
  }
  grpc_core::ApplicationCallbackExecCtx app_ctx;
  grpc_core::ExecCtx exec_ctx;
#ifndef NDEBUG
  closure->scheduled = false;
  if (GRPC_TRACE_FLAG_ENABLED(closure)) {
    gpr_log(GPR_DEBUG,
            "EventEngine: running closure %p: created [%s:%d]: %s [%s:%d]",
            closure, closure->file_created, closure->line_created,
            closure->run ? "run" : "scheduled", closure->file_initiated,
            closure->line_initiated);
  }
#endif
  closure->cb(closure->cb_arg, error);
#ifndef NDEBUG
  if (GRPC_TRACE_FLAG_ENABLED(closure)) {
    gpr_log(GPR_DEBUG, "EventEngine: closure %p finished", closure);
  }
#endif
}

absl::AnyInvocable<void(absl::Status)> GrpcClosureToStatusCallback(
    grpc_closure* closure) {
  return [closure](absl::Status status) {
    RunEventEngineClosure(closure, absl_status_to_grpc_error(status));
  };
}

}  // namespace experimental
}  // namespace grpc_event_engine
