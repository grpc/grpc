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

using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;

Sleep::Sleep(Timestamp deadline) : state_(new State(deadline)) {}

Sleep::~Sleep() {
  if (state_ == nullptr) return;
  {
    MutexLock lock(&state_->mu);
    switch (state_->stage) {
      case Stage::kInitial:
        state_->Unref();
        break;
      case Stage::kStarted:
        GetDefaultEventEngine()->Cancel(state_->timer_handle);
        break;
      case Stage::kDone:
        break;
    }
  }
  state_->Unref();
}

void Sleep::State::OnTimer() {
  Waker tmp_waker;
  {
    MutexLock lock(&mu);
    stage = Stage::kDone;
    tmp_waker = std::move(waker);
  }
  tmp_waker.Wakeup();
  Unref();
}

// TODO(hork): refactor gpr_base to allow a separate time_util target.
namespace {
absl::Time ToAbslTime(Timestamp timestamp) {
  if (timestamp == Timestamp::InfFuture()) return absl::InfiniteFuture();
  if (timestamp == Timestamp::InfPast()) return absl::InfinitePast();
  return absl::Now() +
         absl::Milliseconds((timestamp - ExecCtx::Get()->Now()).millis());
}
}  // namespace

Poll<absl::Status> Sleep::operator()() {
  MutexLock lock(&state_->mu);
  switch (state_->stage) {
    case Stage::kInitial:
      if (state_->deadline <= ExecCtx::Get()->Now()) {
        return absl::OkStatus();
      }
      state_->stage = Stage::kStarted;
      state_->timer_handle = GetDefaultEventEngine()->RunAt(
          ToAbslTime(state_->deadline), [this] { state_->OnTimer(); });
      break;
    case Stage::kStarted:
      break;
    case Stage::kDone:
      return absl::OkStatus();
  }
  state_->waker = Activity::current()->MakeNonOwningWaker();
  return Pending{};
}

}  // namespace grpc_core
