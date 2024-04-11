// Copyright 2022 gRPC authors.
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

#include "src/core/lib/promise/sleep.h"

#include <utility>

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/event_engine_context.h"  // IWYU pragma: keep
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

using ::grpc_event_engine::experimental::EventEngine;

Sleep::Sleep(Timestamp deadline) : deadline_(deadline) {}

Sleep::~Sleep() {
  if (closure_ != nullptr) closure_->Cancel();
}

Poll<absl::Status> Sleep::operator()() {
  // Invalidate now so that we see a fresh version of the time.
  // TODO(ctiller): the following can be safely removed when we remove ExecCtx.
  ExecCtx::Get()->InvalidateNow();
  const auto now = Timestamp::Now();
  // If the deadline is earlier than now we can just return.
  if (deadline_ <= now) return absl::OkStatus();
  if (closure_ == nullptr) {
    // TODO(ctiller): it's likely we'll want a pool of closures - probably per
    // cpu? - to avoid allocating/deallocating on fast paths.
    closure_ = new ActiveClosure(deadline_);
  }
  if (closure_->HasRun()) return absl::OkStatus();
  return Pending{};
}

Sleep::ActiveClosure::ActiveClosure(Timestamp deadline)
    : waker_(GetContext<Activity>()->MakeOwningWaker()),
      timer_handle_(GetContext<EventEngine>()->RunAfter(
          deadline - Timestamp::Now(), this)) {}

void Sleep::ActiveClosure::Run() {
  ApplicationCallbackExecCtx callback_exec_ctx;
  ExecCtx exec_ctx;
  auto waker = std::move(waker_);
  if (Unref()) {
    delete this;
  } else {
    waker.Wakeup();
  }
}

void Sleep::ActiveClosure::Cancel() {
  // If we cancel correctly then we must own both refs still and can simply
  // delete without unreffing twice, otherwise try unreffing since this may be
  // the last owned ref.
  if (HasRun() || GetContext<EventEngine>()->Cancel(timer_handle_) || Unref()) {
    delete this;
  }
}

bool Sleep::ActiveClosure::Unref() {
  return (refs_.fetch_sub(1, std::memory_order_acq_rel) == 1);
}

bool Sleep::ActiveClosure::HasRun() const {
  // If the closure has run (ie woken up the activity) then it will have
  // decremented this ref count once.
  return refs_.load(std::memory_order_acquire) == 1;
}

}  // namespace grpc_core
