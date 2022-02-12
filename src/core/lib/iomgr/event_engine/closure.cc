// Copyright 2021 The gRPC Authors
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
#include <grpc/support/port_platform.h>

#ifdef GRPC_USE_EVENT_ENGINE
#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/event_engine/closure.h"
#include "src/core/lib/iomgr/event_engine/pollset.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_event_engine {
namespace experimental {

namespace {

void RunClosure(grpc_closure* closure, grpc_error_handle error) {
  GPR_ASSERT(closure != nullptr);
#ifndef NDEBUG
  closure->scheduled = false;
  if (grpc_trace_closure.enabled()) {
    gpr_log(GPR_DEBUG,
            "EventEngine: running closure %p: created [%s:%d]: %s [%s:%d]",
            closure, closure->file_created, closure->line_created,
            closure->run ? "run" : "scheduled", closure->file_initiated,
            closure->line_initiated);
  }
#endif
  closure->cb(closure->cb_arg, error);
#ifndef NDEBUG
  if (grpc_trace_closure.enabled()) {
    gpr_log(GPR_DEBUG, "EventEngine: closure %p finished", closure);
  }
#endif
}

}  // namespace

std::function<void(absl::Status)> GrpcClosureToStatusCallback(
    grpc_closure* closure) {
  return [closure](absl::Status status) {
    RunClosure(closure, absl_status_to_grpc_error(status));
    grpc_pollset_ee_broadcast_event();
  };
}

std::function<void()> GrpcClosureToCallback(grpc_closure* closure) {
  return [closure]() {
    RunClosure(closure, GRPC_ERROR_NONE);
    grpc_pollset_ee_broadcast_event();
  };
}

std::function<void()> GrpcClosureToCallback(grpc_closure* closure,
                                            grpc_error_handle error) {
  return [closure, error]() {
    RunClosure(closure, error);
    grpc_pollset_ee_broadcast_event();
  };
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_USE_EVENT_ENGINE
