// Copyright 2024 gRPC authors.
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

#include "src/core/lib/surface/wait_for_cq_end_op.h"

#include <atomic>

#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/promise/trace.h"

namespace grpc_core {
Poll<Empty> WaitForCqEndOp::operator()() {
  if (grpc_trace_promise_primitives.enabled()) {
    gpr_log(GPR_INFO, "%sWaitForCqEndOp[%p] %s",
            Activity::current()->DebugTag().c_str(), this,
            StateString(state_).c_str());
  }
  if (auto* n = absl::get_if<NotStarted>(&state_)) {
    if (n->is_closure) {
      ExecCtx::Run(DEBUG_LOCATION, static_cast<grpc_closure*>(n->tag),
                   std::move(n->error));
      return Empty{};
    } else {
      auto not_started = std::move(*n);
      auto& started =
          state_.emplace<Started>(GetContext<Activity>()->MakeOwningWaker());
      grpc_cq_end_op(
          not_started.cq, not_started.tag, std::move(not_started.error),
          [](void* p, grpc_cq_completion*) {
            auto started = static_cast<Started*>(p);
            auto wakeup = std::move(started->waker);
            started->done.store(true, std::memory_order_release);
            wakeup.Wakeup();
          },
          &started, &started.completion);
    }
  }
  auto& started = absl::get<Started>(state_);
  if (started.done.load(std::memory_order_acquire)) {
    return Empty{};
  } else {
    return Pending{};
  }
}

std::string WaitForCqEndOp::StateString(const State& state) {
  return Match(
      state,
      [](const NotStarted& x) {
        return absl::StrFormat(
            "NotStarted{is_closure=%s, tag=%p, error=%s, cq=%p}",
            x.is_closure ? "true" : "false", x.tag, x.error.ToString(), x.cq);
      },
      [](const Started& x) {
        return absl::StrFormat(
            "Started{completion=%p, done=%s}", &x.completion,
            x.done.load(std::memory_order_relaxed) ? "true" : "false");
      },
      [](const Invalid&) -> std::string { return "Invalid{}"; });
}

}  // namespace grpc_core