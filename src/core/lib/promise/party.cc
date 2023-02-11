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

#include <grpc/support/port_platform.h>

#include "src/core/lib/promise/party.h"

#include <inttypes.h>

#include <algorithm>
#include <atomic>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/trace.h"

namespace grpc_core {

// Weak handle to a Party.
// Handle can persist while Party goes away.
class Party::Handle final : public Wakeable {
 public:
  explicit Handle(Party* party) : party_(party) {}

  // Ref the Handle (not the activity).
  void Ref() { refs_.fetch_add(1, std::memory_order_relaxed); }

  // Activity is going away... drop its reference and sever the connection back.
  void DropActivity() ABSL_LOCKS_EXCLUDED(mu_) {
    mu_.Lock();
    GPR_ASSERT(party_ != nullptr);
    party_ = nullptr;
    mu_.Unlock();
    Unref();
  }

  // Activity needs to wake up (if it still exists!) - wake it up, and drop the
  // ref that was kept for this handle.
  void Wakeup(void* arg) override ABSL_LOCKS_EXCLUDED(mu_) {
    mu_.Lock();
    // Note that activity refcount can drop to zero, but we could win the lock
    // against DropActivity, so we need to only increase activities refcount if
    // it is non-zero.
    Party* party = party_;
    if (party != nullptr && party->RefIfNonZero()) {
      mu_.Unlock();
      // Activity still exists and we have a reference: wake it up, which will
      // drop the ref.
      party->Wakeup(reinterpret_cast<void*>(arg));
    } else {
      // Could not get the activity - it's either gone or going. No need to wake
      // it up!
      mu_.Unlock();
    }
    // Drop the ref to the handle (we have one ref = one wakeup semantics).
    Unref();
  }

  void Drop(void*) override { Unref(); }

  std::string ActivityDebugTag(void*) const override {
    MutexLock lock(&mu_);
    return party_ == nullptr ? "<unknown>" : party_->DebugTag();
  }

 private:
  // Unref the Handle (not the activity).
  void Unref() {
    if (1 == refs_.fetch_sub(1, std::memory_order_acq_rel)) {
      delete this;
    }
  }

  // Two initial refs: one for the waiter that caused instantiation, one for the
  // party.
  std::atomic<size_t> refs_{2};
  mutable Mutex mu_;
  Party* party_ ABSL_GUARDED_BY(mu_);
};

Wakeable* Party::Participant::MakeNonOwningWakeable(Party* party) {
  if (handle_ == nullptr) {
    handle_ = new Handle(party);
    return handle_;
  }
  handle_->Ref();
  return handle_;
}

Party::Participant::~Participant() {
  if (handle_ != nullptr) {
    handle_->DropActivity();
  }
}

Party::~Party() {
  participants_.clear();
  arena_->Destroy();
}

void Party::Orphan() { Unref(); }

void Party::Ref() { state_.fetch_add(kOneRef, std::memory_order_relaxed); }

bool Party::RefIfNonZero() {
  auto count = state_.load(std::memory_order_relaxed);
  do {
    // If zero, we are done (without an increment). If not, we must do a CAS
    // to maintain the contract: do not increment the counter if it is already
    // zero
    if (count == 0) {
      return false;
    }
  } while (!state_.compare_exchange_weak(count, count + kOneRef,
                                         std::memory_order_acq_rel,
                                         std::memory_order_relaxed));
  return true;
}

void Party::Unref() {
  auto prev = state_.fetch_sub(kOneRef, std::memory_order_acq_rel);
  if (prev == kOneRef) {
    delete this;
  }
  GPR_DEBUG_ASSERT((prev & kRefMask) != 0);
}

std::string Party::ActivityDebugTag(void* arg) const {
  return absl::StrFormat("%s/%p", DebugTag(), arg);
}

Waker Party::MakeOwningWaker() {
  GPR_DEBUG_ASSERT(currently_polling_ != kNotPolling);
  Ref();
  return Waker(this, reinterpret_cast<void*>(currently_polling_));
}

Waker Party::MakeNonOwningWaker() {
  GPR_DEBUG_ASSERT(currently_polling_ != kNotPolling);
  return Waker(participants_[currently_polling_]->MakeNonOwningWakeable(this),
               reinterpret_cast<void*>(currently_polling_));
}

void Party::ForceImmediateRepoll() {
  GPR_DEBUG_ASSERT(currently_polling_ != kNotPolling);
  // Or in the bit for the currently polling participant.
  // Will be grabbed next round to force a repoll of this promise.
  state_.fetch_or(1 << currently_polling_, std::memory_order_relaxed);
}

void Party::Run() {
  ScopedActivity activity(this);
  uint64_t prev_state;
  do {
    // Grab the current state, and clear the wakeup bits & add flag.
    prev_state =
        state_.fetch_and(kRefMask | kLocked, std::memory_order_acquire);
    if (grpc_trace_promise_primitives.enabled()) {
      gpr_log(GPR_DEBUG, "Party::Run(): prev_state=%s",
              StateToString(prev_state).c_str());
    }
    // From the previous state, extract which participants we're to wakeup.
    uint64_t wakeups = prev_state & kWakeupMask;
    // If there were adds pending, drain them.
    // We pass in wakeups here so that the new participants are polled
    // immediately (draining will situate them).
    if (prev_state & kAddsPending) DrainAdds(wakeups);
    // Now update prev_state to be what we want the CAS to see below.
    prev_state &= kRefMask | kLocked;
    // For each wakeup bit...
    for (size_t i = 0; wakeups != 0; i++, wakeups >>= 1) {
      // If the bit is not set, skip.
      if ((wakeups & 1) == 0) continue;
      // If the participant is null, skip.
      // This allows participants to complete whilst wakers still exist
      // somewhere.
      if (participants_[i] == nullptr) continue;
      // Poll the participant.
      currently_polling_ = i;
      if (participants_[i]->Poll()) participants_[i].reset();
      currently_polling_ = kNotPolling;
    }
    // Try to CAS the state we expected to have (with no wakeups or adds)
    // back to unlocked (by masking in only the ref mask - sans locked bit).
    // If this succeeds then no wakeups were added, no adds were added, and we
    // have successfully unlocked.
    // Otherwise, we need to loop again.
    // Note that if an owning waker is created or the weak cas spuriously fails
    // we will also loop again, but in that case see no wakeups or adds and so
    // will get back here fairly quickly.
    // TODO(ctiller): consider mitigations for the accidental wakeup on owning
    // waker creation case -- I currently expect this will be more expensive
    // than this quick loop.
  } while (!state_.compare_exchange_weak(prev_state, (prev_state & kRefMask),
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire));
}

void Party::DrainAdds(uint64_t& wakeups) {
  // Grab the list of adds.
  AddingParticipant* adding =
      adding_.exchange(nullptr, std::memory_order_acquire);
  // For each add, situate it and add it to the wakeup mask.
  while (adding != nullptr) {
    wakeups |= 1 << SituateNewParticipant(std::move(adding->participant));
    // Don't leak the add request.
    delete std::exchange(adding, adding->next);
  }
}

void Party::AddParticipant(Arena::PoolPtr<Participant> participant) {
  // Lock
  auto prev_state = state_.fetch_or(kLocked, std::memory_order_acquire);
  if (grpc_trace_promise_primitives.enabled()) {
    gpr_log(GPR_DEBUG, "Party::AddParticipant(): prev_state=%s",
            StateToString(prev_state).c_str());
  }
  if ((prev_state & kLocked) == 0) {
    // Lock acquired
    state_.fetch_or(1 << SituateNewParticipant(std::move(participant)),
                    std::memory_order_relaxed);
    Run();
    return;
  }
  // Already locked: add to the list of things to add
  auto* add = new AddingParticipant{std::move(participant), nullptr};
  while (!adding_.compare_exchange_weak(
      add->next, add, std::memory_order_acq_rel, std::memory_order_acquire)) {
  }
  // And signal that there are adds waiting.
  // This needs to happen after the add above: Run() will examine this bit
  // first, and then decide to drain the queue - so if the ordering was reversed
  // it might examine the adds pending bit, and then observe no add to drain.
  prev_state =
      state_.fetch_or(kLocked | kAddsPending, std::memory_order_release);
  if (grpc_trace_promise_primitives.enabled()) {
    gpr_log(GPR_DEBUG, "Party::AddParticipant(): prev_state=%s",
            StateToString(prev_state).c_str());
  }
  if ((prev_state & kLocked) == 0) {
    // We queued the add but the lock was released before we signalled that.
    // We acquired the lock though, so now we can run.
    Run();
  }
}

size_t Party::SituateNewParticipant(Arena::PoolPtr<Participant> participant) {
  // First search for a free index in the participants array.
  // If we find one, use it.
  for (size_t i = 0; i < participants_.size(); i++) {
    if (participants_[i] != nullptr) continue;
    participants_[i] = std::move(participant);
    return i;
  }

  // Otherwise, add it to the end.
  GPR_ASSERT(participants_.size() < kMaxParticipants);
  participants_.emplace_back(std::move(participant));
  return participants_.size() - 1;
}

void Party::ScheduleWakeup(uint64_t participant_index) {
  // Or in the wakeup bit for the participant, AND the locked bit.
  uint64_t prev_state = state_.fetch_or((1 << participant_index) | kLocked,
                                        std::memory_order_acquire);
  if (grpc_trace_promise_primitives.enabled()) {
    gpr_log(GPR_DEBUG, "Party::ScheduleWakeup(%" PRIu64 "): prev_state=%s",
            participant_index, StateToString(prev_state).c_str());
  }
  // If the lock was not held now we hold it, so we need to run.
  if ((prev_state & kLocked) == 0) Run();
}

void Party::Wakeup(void* arg) {
  ScheduleWakeup(reinterpret_cast<uintptr_t>(arg));
  Unref();
}

void Party::Drop(void*) { Unref(); }

std::string Party::StateToString(uint64_t state) {
  std::vector<std::string> parts;
  if (state & kLocked) parts.push_back("locked");
  if (state & kAddsPending) parts.push_back("adds_pending");
  parts.push_back(
      absl::StrFormat("refs=%" PRIuPTR, (state & kRefMask) >> kRefShift));
  std::vector<int> participants;
  for (size_t i = 0; i < kMaxParticipants; i++) {
    if ((state & (1 << i)) != 0) participants.push_back(i);
  }
  if (!participants.empty()) {
    parts.push_back(
        absl::StrFormat("wakeup=%s", absl::StrJoin(participants, ",")));
  }
  return absl::StrCat("{", absl::StrJoin(parts, " "), "}");
}

}  // namespace grpc_core
