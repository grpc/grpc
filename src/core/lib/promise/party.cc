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

#include "src/core/lib/promise/party.h"

#include "absl/cleanup/cleanup.h"

namespace grpc_core {

void Party::Participant::Ref() { refs_.Ref(); }

void Party::Participant::Unref() {
  if (refs_.Unref()) party_->Farewell(this);
}

void Party::Participant::Wakeup() {
  party_->ScheduleWakeupFor(this);
  Unref();
}

void Party::Participant::Drop() { Unref(); }

void Party::Run() {
  GPR_ASSERT(polling_participant_ == nullptr);
  ScopedActivity activity(this);
  while (!wakeup_participants_.empty()) {
    polling_participant_ = wakeup_participants_.back();
    wakeup_participants_.pop_back();
    polling_participant_->Poll();
    polling_participant_->Unref();
    polling_participant_ = nullptr;
  }
}

void Party::ScheduleWakeupFor(Participant* participant) {
  for (auto p : wakeup_participants_) {
    if (p == participant) return;
  }
  wakeup_participants_.push_back(participant);
  participant->Ref();
}

}  // namespace grpc_core
