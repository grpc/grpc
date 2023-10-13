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

#include "absl/status/status.h"

#include <grpcpp/support/server_callback.h>

#include "src/core/lib/event_engine/default_event_engine.h"
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
    // DO NOT SUBMIT(hork): an EventEngien must share a lifetime with this
    // reactor, and be accessible
    auto engine = grpc_event_engine::experimental::GetDefaultEventEngine();
    auto* engine_ptr = engine.get();
    engine_ptr->Run([this, engine = std::move(engine)]() { CallOnDone(); });
  }
}

void ServerCallbackCall::CallOnCancel(ServerReactor* reactor) {
  if (reactor->InternalInlineable()) {
    reactor->OnCancel();
  } else {
    // Ref to make sure that the closure executes before the whole call gets
    // destructed, and Unref within the closure.
    Ref();
    // DO NOT SUBMIT(hork): an EventEngien must share a lifetime with this
    // reactor, and be accessible
    auto engine = grpc_event_engine::experimental::GetDefaultEventEngine();
    auto* engine_ptr = engine.get();
    engine_ptr->Run([this, reactor, engine = std::move(engine)]() {
      reactor->OnCancel();
      MaybeDone();
    });
  }
}

}  // namespace internal
}  // namespace grpc
