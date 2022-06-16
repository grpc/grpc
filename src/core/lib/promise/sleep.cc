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

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/event_engine_factory.h"
#include "src/core/lib/iomgr/exec_ctx.h"

namespace grpc_core {

using ::grpc_event_engine::experimental::GetDefaultEventEngine;

Sleep::Sleep(Timestamp deadline) : deadline_(deadline) {}

Sleep::~Sleep() {
  if (deadline_ == Timestamp::InfPast()) return;
  ReleasableMutexLock lock(&mu_);
  switch (stage_) {
    case Stage::kInitial:
      break;
    case Stage::kStarted:
      if (GetDefaultEventEngine()->Cancel(timer_handle_)) {
        lock.Release();
        OnTimer();
      }
      break;
    case Stage::kDone:
      break;
  }
}

void Sleep::OnTimer() {
  Waker tmp_waker;
  {
    MutexLock lock(&mu_);
    stage_ = Stage::kDone;
    tmp_waker = std::move(waker_);
  }
  tmp_waker.Wakeup();
}

Poll<absl::Status> Sleep::operator()() {
  MutexLock lock(&mu_);
  switch (stage_) {
    case Stage::kInitial:
      if (deadline_ <= ExecCtx::Get()->Now()) {
        return absl::OkStatus();
      }
      stage_ = Stage::kStarted;
      timer_handle_ = GetDefaultEventEngine()->RunAfter(
          deadline_ - ExecCtx::Get()->Now(), [this] {
            ApplicationCallbackExecCtx callback_exec_ctx;
            ExecCtx exec_ctx;
            OnTimer();
          });
      break;
    case Stage::kStarted:
      break;
    case Stage::kDone:
      return absl::OkStatus();
  }
  waker_ = Activity::current()->MakeNonOwningWaker();
  return Pending{};
}

}  // namespace grpc_core
