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

#include <grpc/support/port_platform.h>

#include "src/core/lib/promise/sleep.h"

#include <utility>

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"

namespace grpc_core {

using ::grpc_event_engine::experimental::GetDefaultEventEngine;

Sleep::Sleep(Timestamp deadline) : deadline_(deadline) {}

Sleep::~Sleep() {
  if (closure_ != nullptr) closure_->Cancel();
}

Poll<absl::Status> Sleep::operator()() {
  // Invalidate now so that we see a fresh version of the time.
  // TODO(ctiller): the following can be safely removed when we remove ExecCtx.
  ExecCtx::Get()->InvalidateNow();
  // If the deadline is earlier than now we can just return.
  if (deadline_ <= ExecCtx::Get()->Now()) return absl::OkStatus();
  if (closure_ == nullptr) {
    // TODO(ctiller): it's likely we'll want a pool of closures - probably per
    // cpu? - to avoid allocating/deallocating on fast paths.
    closure_ = new ActiveClosure(deadline_);
  }
  return Pending{};
}

Sleep::ActiveClosure::ActiveClosure(Timestamp deadline)
    : waker_(Activity::current()->MakeOwningWaker()),
      timer_handle_(GetDefaultEventEngine()->RunAfter(
          deadline - ExecCtx::Get()->Now(), this)) {}

void Sleep::ActiveClosure::Run() {
  ApplicationCallbackExecCtx callback_exec_ctx;
  ExecCtx exec_ctx;
  auto waker = std::move(waker_);
  if (refs_.Unref()) {
    delete this;
  } else {
    waker.Wakeup();
  }
}

void Sleep::ActiveClosure::Cancel() {
  // If we cancel correctly then we must own both refs still and can simply
  // delete without unreffing twice, otherwise try unreffing since this may be
  // the last owned ref.
  if (GetDefaultEventEngine()->Cancel(timer_handle_) || refs_.Unref()) {
    delete this;
  }
}

}  // namespace grpc_core
