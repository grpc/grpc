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

#include <atomic>
#include <cstdint>
#include <utility>

#include "absl/cleanup/cleanup.h"

#include "src/core/lib/gprpp/crash.h"

namespace grpc_core {

void Party::Ref() {
  wakeups_and_refs_.fetch_add(kOneRef, std::memory_order_relaxed);
}

void Party::Unref() {
  auto prev = wakeups_and_refs_.fetch_sub(kOneRef, std::memory_order_acq_rel);
  if (prev == kOneRef) {
    delete this;
  }
  GPR_DEBUG_ASSERT((prev & kRefMask) != 0);
}

Waker Party::MakeOwningWaker() {
  Ref();
  return Waker(this, reinterpret_cast<void*>(currently_polling_));
}

void Party::ForceImmediateRepoll() {
  GPR_ASSERT(currently_polling_ != kNotPolling);
  wakeups_and_refs_.fetch_or(1 << currently_polling_,
                             std::memory_order_relaxed);
}

void Party::Run() {
  ScopedActivity activity(this);
  uint64_t prev_wakeups_and_refs;
  do {
    prev_wakeups_and_refs = wakeups_and_refs_.fetch_and(
        kRefMask | kAwoken, std::memory_order_relaxed);
    uint64_t wakeups = prev_wakeups_and_refs & kParticipantMask;
    if (prev_wakeups_and_refs & kAddsPending) DrainAdds(wakeups);
    prev_wakeups_and_refs &= kRefMask | kAwoken;
    for (size_t i = 0; wakeups != 0; i++, wakeups >>= 1) {
      if ((wakeups & 1) == 0) continue;
      if (participants_[i] == nullptr) continue;
      currently_polling_ = i;
      if (participants_[i]->Poll()) participants_[i].reset();
      currently_polling_ = kNotPolling;
    }
  } while (!wakeups_and_refs_.compare_exchange_weak(
      prev_wakeups_and_refs,
      // Clear awoken bit and unref
      (prev_wakeups_and_refs & kRefMask), std::memory_order_acq_rel,
      std::memory_order_relaxed));
}

void Party::DrainAdds(uint64_t& wakeups) {
  AddingParticipant* adding =
      adding_.exchange(nullptr, std::memory_order_acquire);
  while (adding != nullptr) {
    wakeups |= 1 << SituateNewParticipant(std::move(adding->participant));
    delete std::exchange(adding, adding->next);
  }
}

void Party::AddParticipant(Arena::PoolPtr<Participant> participant) {
  // Lock
  auto prev_wakeups_and_refs =
      wakeups_and_refs_.fetch_or(kAwoken, std::memory_order_relaxed);
  if ((prev_wakeups_and_refs & kAwoken) == 0) {
    // Lock acquired
    wakeups_and_refs_.fetch_or(
        1 << SituateNewParticipant(std::move(participant)));
    Run();
    return;
  }
  // Already locked: add to the list of things to add
  auto* add = new AddingParticipant{std::move(participant), nullptr};
  while (!adding_.compare_exchange_weak(
      add->next, add, std::memory_order_acq_rel, std::memory_order_relaxed)) {
  }
  // And signal that there are adds waiting
  prev_wakeups_and_refs = wakeups_and_refs_.fetch_or(kAwoken | kAddsPending,
                                                     std::memory_order_relaxed);
  if ((prev_wakeups_and_refs & kAwoken) == 0) {
    // We queued the add but the lock was released before we signalled that.
    // We acquired the lock though, so now we can run.
    Run();
  }
}

size_t Party::SituateNewParticipant(Arena::PoolPtr<Participant> participant) {
  for (size_t i = 0; i < participants_.size(); i++) {
    if (participants_[i] != nullptr) continue;
    participants_[i] = std::move(participant);
    return i;
  }

  participants_.push_back(std::move(participant));
  return participants_.size() - 1;
}

void Party::ScheduleWakeup(uint64_t participant_index) {
  uint64_t prev_wakeups =
      wakeups_and_refs_.fetch_or((1 << participant_index) | kAwoken);
  if ((prev_wakeups & kAwoken) == 0) {
    Run();
  }
  Unref();
}

void Party::Wakeup(void* arg) {
  ScheduleWakeup(reinterpret_cast<uintptr_t>(arg));
}

void Party::Drop(void* arg) { Unref(); }

}  // namespace grpc_core
