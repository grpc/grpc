// Copyright 2023 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_SURFACE_WAIT_FOR_CQ_END_OP_H
#define GRPC_SRC_CORE_LIB_SURFACE_WAIT_FOR_CQ_END_OP_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/surface/completion_queue.h"

namespace grpc_core {

// Defines a promise that calls grpc_cq_end_op() (on first poll) and then waits
// for the callback supplied to grpc_cq_end_op() to be called, before resolving
// to Empty{}
class WaitForCqEndOp {
 public:
  WaitForCqEndOp(bool is_closure, void* tag, grpc_error_handle error,
                 grpc_completion_queue* cq)
      : state_{NotStarted{is_closure, tag, std::move(error), cq}} {}

  Poll<Empty> operator()() {
    if (auto* n = absl::get_if<NotStarted>(&state_)) {
      if (n->is_closure) {
        ExecCtx::Run(DEBUG_LOCATION, static_cast<grpc_closure*>(n->tag),
                     std::move(n->error));
        return Empty{};
      } else {
        auto not_started = std::move(*n);
        auto& started =
            state_.emplace<Started>(Activity::current()->MakeOwningWaker());
        grpc_cq_end_op(
            not_started.cq, not_started.tag, std::move(not_started.error),
            [](void* p, grpc_cq_completion*) {
              auto started = static_cast<Started*>(p);
              started->done.store(true, std::memory_order_release);
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

  WaitForCqEndOp(const WaitForCqEndOp&) = delete;
  WaitForCqEndOp& operator=(const WaitForCqEndOp&) = delete;
  WaitForCqEndOp(WaitForCqEndOp&& other) noexcept
      : state_(std::move(absl::get<NotStarted>(other.state_))) {
    other.state_.emplace<Invalid>();
  }
  WaitForCqEndOp& operator=(WaitForCqEndOp&& other) noexcept {
    state_ = std::move(absl::get<NotStarted>(other.state_));
    other.state_.emplace<Invalid>();
    return *this;
  }

 private:
  struct NotStarted {
    bool is_closure;
    void* tag;
    grpc_error_handle error;
    grpc_completion_queue* cq;
  };
  struct Started {
    explicit Started(Waker waker) : waker(std::move(waker)) {}
    Waker waker;
    grpc_cq_completion completion;
    std::atomic<bool> done{false};
  };
  struct Invalid {};
  using State = absl::variant<NotStarted, Started, Invalid>;
  State state_{Invalid{}};
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_SURFACE_WAIT_FOR_CQ_END_OP_H
