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

// #define GRPC_PARTY_MAXIMIZE_THREADS

#ifdef GRPC_PARTY_MAXIMIZE_THREADS
#include <thread>  // IWYU pragma: keep

#include "src/core/lib/iomgr/exec_ctx.h"  // IWYU pragma: keep
#endif

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
  void Wakeup(WakeupMask wakeup_mask) override ABSL_LOCKS_EXCLUDED(mu_) {
    mu_.Lock();
    // Note that activity refcount can drop to zero, but we could win the lock
    // against DropActivity, so we need to only increase activities refcount if
    // it is non-zero.
    Party* party = party_;
    if (party != nullptr && party->RefIfNonZero()) {
      mu_.Unlock();
      // Activity still exists and we have a reference: wake it up, which will
      // drop the ref.
      party->Wakeup(wakeup_mask);
    } else {
      // Could not get the activity - it's either gone or going. No need to wake
      // it up!
      mu_.Unlock();
    }
    // Drop the ref to the handle (we have one ref = one wakeup semantics).
    Unref();
  }

  void Drop(WakeupMask wakeup_mask) override { Unref(); }

  std::string ActivityDebugTag(WakeupMask) const override {
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

Party::~Party() {}

void Party::IncrementRefCount(DebugLocation whence) {
  auto prev_state = state_.fetch_add(kOneRef, std::memory_order_relaxed);
  if (grpc_trace_promise_primitives.enabled()) {
    gpr_log(GPR_DEBUG, "%s[party] Ref: prev_state=%s from %s:%d",
            DebugTag().c_str(), StateToString(prev_state).c_str(),
            whence.file(), whence.line());
  }
}

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

void Party::Unref(DebugLocation whence) {
  uint64_t prev_state;
  auto do_unref = [&prev_state, this]() {
    prev_state = state_.fetch_sub(kOneRef, std::memory_order_acq_rel);
  };
  if (grpc_trace_promise_primitives.enabled()) {
    auto debug_tag = DebugTag();
    do_unref();
    gpr_log(GPR_DEBUG, "%s[party] Unref: prev_state=%s from %s:%d",
            debug_tag.c_str(), StateToString(prev_state).c_str(), whence.file(),
            whence.line());
  } else {
    do_unref();
  }
  if ((prev_state & kRefMask) == kOneRef) {
    prev_state =
        state_.fetch_or(kDestroying | kLocked, std::memory_order_acq_rel);
    if (prev_state & kLocked) {
      // Already locked: RunParty will call PartyOver.
    } else {
      ScopedActivity activity(this);
      PartyOver();
    }
    return;
  }
}

void Party::CancelRemainingParticipants() {
  ScopedActivity activity(this);
  promise_detail::Context<Arena> arena_ctx(arena_);
  for (size_t i = 0; i < kMaxParticipants; i++) {
    if (auto* p =
            participants_[i].exchange(nullptr, std::memory_order_acquire)) {
      p->Destroy();
    }
  }
}

std::string Party::ActivityDebugTag(WakeupMask wakeup_mask) const {
  return absl::StrFormat("%s [parts:%x]", DebugTag(), wakeup_mask);
}

Waker Party::MakeOwningWaker() {
  GPR_DEBUG_ASSERT(currently_polling_ != kNotPolling);
  IncrementRefCount();
  return Waker(this, 1u << currently_polling_);
}

Waker Party::MakeNonOwningWaker() {
  GPR_DEBUG_ASSERT(currently_polling_ != kNotPolling);
  return Waker(participants_[currently_polling_]
                   .load(std::memory_order_relaxed)
                   ->MakeNonOwningWakeable(this),
               1u << currently_polling_);
}

void Party::ForceImmediateRepoll(WakeupMask mask) {
  GPR_DEBUG_ASSERT(is_current());
  // Or in the bit for the currently polling participant.
  // Will be grabbed next round to force a repoll of this promise.
  auto prev_state =
      state_.fetch_or(mask & kWakeupMask, std::memory_order_relaxed);

  if (grpc_trace_promise_primitives.enabled()) {
    std::vector<int> wakeups;
    for (int i = 0; i < 8 * sizeof(WakeupMask); i++) {
      if (mask & (1 << i)) wakeups.push_back(i);
    }
    gpr_log(GPR_DEBUG, "%s[party] ForceImmediateRepoll({%s}): prev_state=%s",
            DebugTag().c_str(), absl::StrJoin(wakeups, ",").c_str(),
            StateToString(prev_state).c_str());
  }
}

void Party::RunLocked() {
  auto body = [this]() {
    if (RunParty()) {
      ScopedActivity activity(this);
      PartyOver();
    }
  };
#ifdef GRPC_PARTY_MAXIMIZE_THREADS
  std::thread([body]() {
    ApplicationCallbackExecCtx app_exec_ctx;
    ExecCtx exec_ctx;
    body();
  }).detach();
#else
  body();
#endif
}

bool Party::RunParty() {
  ScopedActivity activity(this);
  promise_detail::Context<Arena> arena_ctx(arena_);
  uint64_t prev_state;
  do {
    // Grab the current state, and clear the wakeup bits & add flag.
    prev_state = state_.fetch_and(kRefMask | kLocked | kAllocatedMask,
                                  std::memory_order_acquire);
    if (grpc_trace_promise_primitives.enabled()) {
      gpr_log(GPR_DEBUG, "%s[party] Run prev_state=%s", DebugTag().c_str(),
              StateToString(prev_state).c_str());
    }
    GPR_ASSERT(prev_state & kLocked);
    if (prev_state & kDestroying) return true;
    // From the previous state, extract which participants we're to wakeup.
    uint64_t wakeups = prev_state & kWakeupMask;
    // Now update prev_state to be what we want the CAS to see below.
    prev_state &= kRefMask | kLocked | kAllocatedMask;
    // For each wakeup bit...
    for (size_t i = 0; wakeups != 0; i++, wakeups >>= 1) {
      // If the bit is not set, skip.
      if ((wakeups & 1) == 0) continue;
      // If the participant is null, skip.
      // This allows participants to complete whilst wakers still exist
      // somewhere.
      auto* participant = participants_[i].load(std::memory_order_acquire);
      if (participant == nullptr) {
        if (grpc_trace_promise_primitives.enabled()) {
          gpr_log(GPR_DEBUG, "%s[party] wakeup %" PRIdPTR " already complete",
                  DebugTag().c_str(), i);
        }
        continue;
      }
      absl::string_view name;
      if (grpc_trace_promise_primitives.enabled()) {
        name = participant->name();
        gpr_log(GPR_DEBUG, "%s[%s] begin job %" PRIdPTR, DebugTag().c_str(),
                std::string(name).c_str(), i);
      }
      // Poll the participant.
      currently_polling_ = i;
      if (participant->Poll()) {
        if (!name.empty()) {
          gpr_log(GPR_DEBUG, "%s[%s] end poll and finish job %" PRIdPTR,
                  DebugTag().c_str(), std::string(name).c_str(), i);
        }
        participants_[i] = nullptr;
        const uint64_t allocated_bit = (1u << i << kAllocatedShift);
        prev_state &= ~allocated_bit;
        state_.fetch_and(~allocated_bit, std::memory_order_release);
      } else if (!name.empty()) {
        gpr_log(GPR_DEBUG, "%s[%s] end poll", DebugTag().c_str(),
                std::string(name).c_str());
      }
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
  } while (!state_.compare_exchange_weak(
      prev_state, (prev_state & (kRefMask | kAllocatedMask)),
      std::memory_order_acq_rel, std::memory_order_acquire));
  return false;
}

void Party::AddParticipant(Participant* participant) {
  uint64_t state = state_.load(std::memory_order_acquire);
  uint64_t allocated;

  int slot;

  // Find slots for each new participant, ordering them from lowest available
  // slot upwards to ensure the same poll ordering as presentation ordering to
  // this function.
  do {
    slot = -1;
    allocated = (state & kAllocatedMask) >> kAllocatedShift;
    for (int bit = 0; bit < kMaxParticipants; bit++) {
      if (allocated & (1 << bit)) continue;
      slot = bit;
      allocated |= 1 << bit;
      break;
    }
    GPR_ASSERT(slot != -1);
  } while (!state_.compare_exchange_weak(
      state, state | (allocated << kAllocatedShift), std::memory_order_acq_rel,
      std::memory_order_acquire));

  if (grpc_trace_promise_primitives.enabled()) {
    gpr_log(GPR_DEBUG, "%s[party] Welcome %s@%d", DebugTag().c_str(),
            std::string(participant->name()).c_str(), slot);
  }

  // We've allocated the slot, next we need to populate it.
  participants_[slot].store(participant, std::memory_order_release);

  // Now we need to wake up the party.
  state = state_.fetch_or((1 << slot) | kLocked, std::memory_order_relaxed);

  // If the party was already locked, we're done.
  if ((state & kLocked) != 0) return;

  // Otherwise, we need to run the party.
  RunLocked();
}

void Party::ScheduleWakeup(WakeupMask mask) {
  // Or in the wakeup bit for the participant, AND the locked bit.
  uint64_t prev_state = state_.fetch_or((mask & kWakeupMask) | kLocked,
                                        std::memory_order_acquire);
  if (grpc_trace_promise_primitives.enabled()) {
    std::vector<int> wakeups;
    for (int i = 0; i < 8 * sizeof(WakeupMask); i++) {
      if (mask & (1 << i)) wakeups.push_back(i);
    }
    gpr_log(GPR_DEBUG, "%s[party] ScheduleWakeup({%s}): prev_state=%s",
            DebugTag().c_str(), absl::StrJoin(wakeups, ",").c_str(),
            StateToString(prev_state).c_str());
  }
  // If the lock was not held now we hold it, so we need to run.
  if ((prev_state & kLocked) == 0) RunLocked();
}

void Party::Wakeup(WakeupMask wakeup_mask) {
  ScheduleWakeup(wakeup_mask);
  Unref();
}

void Party::Drop(WakeupMask) { Unref(); }

std::string Party::StateToString(uint64_t state) {
  std::vector<std::string> parts;
  if (state & kLocked) parts.push_back("locked");
  if (state & kDestroying) parts.push_back("over");
  parts.push_back(
      absl::StrFormat("refs=%" PRIuPTR, (state & kRefMask) >> kRefShift));
  std::vector<int> allocated;
  std::vector<int> participants;
  for (size_t i = 0; i < kMaxParticipants; i++) {
    if ((state & (1ull << i)) != 0) participants.push_back(i);
    if ((state & (1ull << (i + kAllocatedShift))) != 0) allocated.push_back(i);
  }
  if (!allocated.empty()) {
    parts.push_back(
        absl::StrFormat("allocated={%s}", absl::StrJoin(allocated, ",")));
  }
  if (!participants.empty()) {
    parts.push_back(
        absl::StrFormat("wakeup={%s}", absl::StrJoin(participants, ",")));
  }
  return absl::StrCat("{", absl::StrJoin(parts, " "), "}");
}

}  // namespace grpc_core
