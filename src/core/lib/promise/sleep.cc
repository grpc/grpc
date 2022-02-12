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

namespace grpc_core {

Sleep::Sleep(grpc_millis deadline) : state_(new State(deadline)) {
  GRPC_CLOSURE_INIT(&state_->on_timer, &OnTimer, state_, nullptr);
}

Sleep::~Sleep() {
  if (state_ == nullptr) return;
  {
    MutexLock lock(&state_->mu);
    switch (state_->stage) {
      case Stage::kInitial:
        state_->Unref();
        break;
      case Stage::kStarted:
        grpc_timer_cancel(&state_->timer);
        break;
      case Stage::kDone:
        break;
    }
  }
  state_->Unref();
}

void Sleep::OnTimer(void* arg, grpc_error_handle) {
  auto* state = static_cast<State*>(arg);
  Waker waker;
  {
    MutexLock lock(&state->mu);
    state->stage = Stage::kDone;
    waker = std::move(state->waker);
  }
  waker.Wakeup();
  state->Unref();
}

Poll<absl::Status> Sleep::operator()() {
  MutexLock lock(&state_->mu);
  switch (state_->stage) {
    case Stage::kInitial:
      if (state_->deadline <= ExecCtx::Get()->Now()) {
        return absl::OkStatus();
      }
      state_->stage = Stage::kStarted;
      grpc_timer_init(&state_->timer, state_->deadline, &state_->on_timer);
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
