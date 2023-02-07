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
  repoll_participants_ |= 1 << currently_polling_;
}

void Party::Run() {
  MutexLock lock(&mu_);
  ScopedActivity activity(this);
  uint32_t prev_wakeups_and_refs;
  do {
    prev_wakeups_and_refs = wakeups_and_refs_.fetch_and(
                                kRefMask | kAwoken, std::memory_order_relaxed) |
                            std::exchange(repoll_participants_, 0);
    const uint32_t wakeups = prev_wakeups_and_refs & kParticipantMask;
    prev_wakeups_and_refs &= kRefMask | kAwoken;
    if (wakeups != 0) {
      for (uint32_t i = 0; i < 16; i++) {
        if ((wakeups & (1 << i)) != 0 && participants_[i] != nullptr) {
          currently_polling_ = i;
          participants_[i]->Poll();
          currently_polling_ = kNotPolling;
        }
      }
    }
  } while (repoll_participants_ != 0 ||
           !wakeups_and_refs_.compare_exchange_weak(
               prev_wakeups_and_refs,
               (prev_wakeups_and_refs & kRefMask) - kOneRef,
               std::memory_order_acq_rel, std::memory_order_relaxed));
}

void Party::Wakeup(void* arg) {
  const uint32_t i = reinterpret_cast<uintptr_t>(arg);
  uint32_t prev_wakeups =
      wakeups_and_refs_.fetch_or((1 << i) | kAwoken) & kAwoken;
  if (prev_wakeups == 0) {
    event_engine_->Run([this]() { Run(); });
  } else {
    Unref();
  }
}

void Party::Drop(void* arg) { Unref(); }

}  // namespace grpc_core
