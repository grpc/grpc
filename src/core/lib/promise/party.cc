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

#include <grpc/support/port_platform.h>

#include <atomic>
#include <cstdint>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/util/latent_see.h"
#include "src/core/util/sync.h"

#ifdef GRPC_MAXIMIZE_THREADYNESS
#include "src/core/lib/iomgr/exec_ctx.h"  // IWYU pragma: keep
#include "src/core/util/thd.h"            // IWYU pragma: keep
#endif

namespace grpc_core {

///////////////////////////////////////////////////////////////////////////////
// PartySyncUsingAtomics

GRPC_MUST_USE_RESULT bool Party::RefIfNonZero() {
  auto state = state_.load(std::memory_order_relaxed);
  do {
    // If zero, we are done (without an increment). If not, we must do a CAS
    // to maintain the contract: do not increment the counter if it is already
    // zero
    if ((state & kRefMask) == 0) {
      return false;
    }
  } while (!state_.compare_exchange_weak(state, state + kOneRef,
                                         std::memory_order_acq_rel,
                                         std::memory_order_relaxed));
  LogStateChange("RefIfNonZero", state, state + kOneRef);
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// Party::Handle

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
    CHECK_NE(party_, nullptr);
    party_ = nullptr;
    mu_.Unlock();
    Unref();
  }

  void WakeupGeneric(WakeupMask wakeup_mask,
                     void (Party::*wakeup_method)(WakeupMask))
      ABSL_LOCKS_EXCLUDED(mu_) {
    mu_.Lock();
    // Note that activity refcount can drop to zero, but we could win the lock
    // against DropActivity, so we need to only increase activities refcount if
    // it is non-zero.
    Party* party = party_;
    if (party != nullptr && party->RefIfNonZero()) {
      mu_.Unlock();
      // Activity still exists and we have a reference: wake it up, which will
      // drop the ref.
      (party->*wakeup_method)(wakeup_mask);
    } else {
      // Could not get the activity - it's either gone or going. No need to wake
      // it up!
      mu_.Unlock();
    }
    // Drop the ref to the handle (we have one ref = one wakeup semantics).
    Unref();
  }

  // Activity needs to wake up (if it still exists!) - wake it up, and drop the
  // ref that was kept for this handle.
  void Wakeup(WakeupMask wakeup_mask) override ABSL_LOCKS_EXCLUDED(mu_) {
    WakeupGeneric(wakeup_mask, &Party::Wakeup);
  }

  void WakeupAsync(WakeupMask wakeup_mask) override ABSL_LOCKS_EXCLUDED(mu_) {
    WakeupGeneric(wakeup_mask, &Party::WakeupAsync);
  }

  void Drop(WakeupMask) override { Unref(); }

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

void Party::CancelRemainingParticipants() {
  uint64_t prev_state = state_.load(std::memory_order_relaxed);
  if ((prev_state & kAllocatedMask) == 0) return;
  ScopedActivity activity(this);
  promise_detail::Context<Arena> arena_ctx(arena_.get());
  uint64_t clear_state = 0;
  do {
    for (size_t i = 0; i < party_detail::kMaxParticipants; i++) {
      if (auto* p =
              participants_[i].exchange(nullptr, std::memory_order_acquire)) {
        clear_state |= 1ull << i << kAllocatedShift;
        p->Destroy();
      }
    }
    if (clear_state == 0) return;
  } while (!state_.compare_exchange_weak(prev_state, prev_state & ~clear_state,
                                         std::memory_order_acq_rel));
  LogStateChange("CancelRemainingParticipants", prev_state,
                 prev_state & ~clear_state);
}

std::string Party::ActivityDebugTag(WakeupMask wakeup_mask) const {
  return absl::StrFormat("%s [parts:%x]", DebugTag(), wakeup_mask);
}

Waker Party::MakeOwningWaker() {
  DCHECK(currently_polling_ != kNotPolling);
  IncrementRefCount();
  return Waker(this, 1u << currently_polling_);
}

Waker Party::MakeNonOwningWaker() {
  DCHECK(currently_polling_ != kNotPolling);
  return Waker(participants_[currently_polling_]
                   .load(std::memory_order_relaxed)
                   ->MakeNonOwningWakeable(this),
               1u << currently_polling_);
}

void Party::ForceImmediateRepoll(WakeupMask mask) {
  DCHECK(is_current());
  wakeup_mask_ |= mask;
}

void Party::RunLockedAndUnref(Party* party, uint64_t prev_state) {
  GRPC_LATENT_SEE_PARENT_SCOPE("Party::RunLocked");
#ifdef GRPC_MAXIMIZE_THREADYNESS
  Thread thd(
      "RunParty",
      [party, prev_state]() {
        ApplicationCallbackExecCtx app_exec_ctx;
        ExecCtx exec_ctx;
        party->RunPartyAndUnref(prev_state);
      },
      nullptr, Thread::Options().set_joinable(false));
  thd.Start();
#else
  struct RunState;
  static thread_local RunState* g_run_state = nullptr;
  struct PartyWakeup {
    PartyWakeup() : party{nullptr} {}
    PartyWakeup(Party* party, uint64_t prev_state)
        : party{party}, prev_state{prev_state} {}
    Party* party;
    uint64_t prev_state;
  };
  struct RunState {
    explicit RunState(PartyWakeup first) : first{first}, next{} {}
    PartyWakeup first;
    PartyWakeup next;
    GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void Run() {
      g_run_state = this;
      do {
        GRPC_LATENT_SEE_INNER_SCOPE("run_one_party");
        first.party->RunPartyAndUnref(first.prev_state);
        first = std::exchange(next, PartyWakeup{});
      } while (first.party != nullptr);
      DCHECK(g_run_state == this);
      g_run_state = nullptr;
    }
  };
  // If there is a party running, then we don't run it immediately
  // but instead add it to the end of the list of parties to run.
  // This enables a fairly straightforward batching of work from a
  // call to a transport (or back again).
  if (GPR_UNLIKELY(g_run_state != nullptr)) {
    if (g_run_state->first.party == party) {
      g_run_state->first.prev_state = prev_state;
      party->Unref();
      return;
    }
    if (g_run_state->next.party == party) {
      g_run_state->next.prev_state = prev_state;
      party->Unref();
      return;
    }
    if (g_run_state->next.party != nullptr) {
      // If there's already a different party queued, we're better off asking
      // event engine to run it so we can spread load.
      // We swap the oldest party to run on the event engine so that we don't
      // accidentally end up with a tail latency problem whereby one party
      // gets held for a really long time.
      auto wakeup =
          std::exchange(g_run_state->next, PartyWakeup{party, prev_state});
      auto arena = party->arena_.get();
      auto* event_engine =
          arena->GetContext<grpc_event_engine::experimental::EventEngine>();
      CHECK(event_engine != nullptr) << "; " << GRPC_DUMP_ARGS(party, arena);
      event_engine->Run([wakeup]() {
        GRPC_LATENT_SEE_PARENT_SCOPE("Party::RunLocked offload");
        ApplicationCallbackExecCtx app_exec_ctx;
        ExecCtx exec_ctx;
        RunState{wakeup}.Run();
      });
      return;
    }
    g_run_state->next = PartyWakeup{party, prev_state};
    return;
  }
  RunState{{party, prev_state}}.Run();
#endif
}

void Party::RunPartyAndUnref(uint64_t prev_state) {
  ScopedActivity activity(this);
  promise_detail::Context<Arena> arena_ctx(arena_.get());
  DCHECK_EQ(prev_state & kLocked, 0u)
      << "Party should be unlocked prior to first wakeup";
  DCHECK_GE(prev_state & kRefMask, kOneRef);
  // Now update prev_state to be what we want the CAS to see below.
  DCHECK_EQ(prev_state & ~(kRefMask | kAllocatedMask), 0u)
      << "Party should have contained no wakeups on lock";
  prev_state |= kLocked;
  absl::optional<ScopedTimeCache> time_cache;
#if !TARGET_OS_IPHONE
  if (IsTimeCachingInPartyEnabled()) {
    time_cache.emplace();
  }
#endif
  for (;;) {
    uint64_t keep_allocated_mask = kAllocatedMask;
    // For each wakeup bit...
    while (wakeup_mask_ != 0) {
      auto wakeup_mask = std::exchange(wakeup_mask_, 0);
      while (wakeup_mask != 0) {
        const uint64_t t = LowestOneBit(wakeup_mask);
        const int i = absl::countr_zero(t);
        wakeup_mask ^= t;
        // If the participant is null, skip.
        // This allows participants to complete whilst wakers still exist
        // somewhere.
        auto* participant = participants_[i].load(std::memory_order_acquire);
        if (GPR_UNLIKELY(participant == nullptr)) {
          GRPC_TRACE_LOG(promise_primitives, INFO)
              << "Party " << this << "                 Run:Wakeup " << i
              << " already complete";
          continue;
        }
        GRPC_TRACE_LOG(promise_primitives, INFO)
            << "Party " << this << "                 Run:Wakeup " << i;
        // Poll the participant.
        currently_polling_ = i;
        if (participant->PollParticipantPromise()) {
          participants_[i].store(nullptr, std::memory_order_relaxed);
          const uint64_t allocated_bit = (1u << i << kAllocatedShift);
          keep_allocated_mask &= ~allocated_bit;
        }
      }
    }
    currently_polling_ = kNotPolling;
    // Try to CAS the state we expected to have (with no wakeups or adds)
    // back to unlocked (by masking in only the ref mask - sans locked bit).
    // If this succeeds then no wakeups were added, no adds were added, and we
    // have successfully unlocked.
    // Otherwise, we need to loop again.
    // Note that if an owning waker is created or the weak cas spuriously
    // fails we will also loop again, but in that case see no wakeups or adds
    // and so will get back here fairly quickly.
    // TODO(ctiller): consider mitigations for the accidental wakeup on owning
    // waker creation case -- I currently expect this will be more expensive
    // than this quick loop.
    if (state_.compare_exchange_weak(
            prev_state,
            (prev_state & (kRefMask | keep_allocated_mask)) - kOneRef,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
      LogStateChange("Run:End", prev_state,
                     (prev_state & (kRefMask | keep_allocated_mask)) - kOneRef);
      if ((prev_state & kRefMask) == kOneRef) {
        // We're done with the party.
        PartyIsOver();
      }
      return;
    }
    // CAS out (but retrieve) any allocations and wakeups that occurred during
    // the run.
    while (!state_.compare_exchange_weak(
        prev_state, prev_state & (kRefMask | kLocked | keep_allocated_mask))) {
      // Nothing to do here.
    }
    LogStateChange("Run:Continue", prev_state,
                   prev_state & (kRefMask | kLocked | keep_allocated_mask));
    DCHECK(prev_state & kLocked)
        << "Party should be locked; prev_state=" << prev_state;
    DCHECK_GE(prev_state & kRefMask, kOneRef);
    // From the previous state, extract which participants we're to wakeup.
    wakeup_mask_ |= prev_state & kWakeupMask;
    // Now update prev_state to be what we want the CAS to see once wakeups
    // complete next iteration.
    prev_state &= kRefMask | kLocked | keep_allocated_mask;
  }
}

void Party::AddParticipant(Participant* participant) {
  GRPC_LATENT_SEE_INNER_SCOPE("Party::AddParticipant");
  uint64_t state = state_.load(std::memory_order_acquire);
  uint64_t allocated;
  size_t slot;

  // Find slots for each new participant, ordering them from lowest available
  // slot upwards to ensure the same poll ordering as presentation ordering to
  // this function.
  uint64_t wakeup_mask;
  uint64_t new_state;
  do {
    allocated = (state & kAllocatedMask) >> kAllocatedShift;
    wakeup_mask = LowestOneBit(~allocated);
    if (GPR_UNLIKELY((wakeup_mask & kWakeupMask) == 0)) {
      DelayAddParticipant(participant);
      return;
    }
    DCHECK_NE(wakeup_mask & kWakeupMask, 0u)
        << "No available slots for new participant; allocated=" << allocated
        << " state=" << state << " wakeup_mask=" << wakeup_mask;
    allocated |= wakeup_mask;
    slot = absl::countr_zero(wakeup_mask);
    // Try to allocate this slot and take a ref (atomically).
    // Ref needs to be taken because once we store the participant it could be
    // spuriously woken up and unref the party.
    new_state = (state | (allocated << kAllocatedShift)) + kOneRef;
  } while (!state_.compare_exchange_weak(
      state, new_state, std::memory_order_acq_rel, std::memory_order_acquire));
  LogStateChange("AddParticipantsAndRef", state, new_state);
  GRPC_TRACE_LOG(party_state, INFO)
      << "Party " << this << "                 AddParticipant: " << slot
      << " [participant=" << participant << "]";
  participants_[slot].store(participant, std::memory_order_release);
  // Now we need to wake up the party.
  WakeupFromState(new_state, wakeup_mask);
}

void Party::DelayAddParticipant(Participant* participant) {
  // We need to delay the addition of participants.
  IncrementRefCount();
  VLOG_EVERY_N_SEC(2, 10) << "Delaying addition of participant to party "
                          << this << " because it is full.";
  arena_->GetContext<grpc_event_engine::experimental::EventEngine>()->Run(
      [this, participant]() mutable {
        ApplicationCallbackExecCtx app_exec_ctx;
        ExecCtx exec_ctx;
        AddParticipant(participant);
        Unref();
      });
}

void Party::WakeupAsync(WakeupMask wakeup_mask) {
  // Or in the wakeup bit for the participant, AND the locked bit.
  uint64_t prev_state = state_.load(std::memory_order_relaxed);
  LogStateChange("ScheduleWakeup", prev_state,
                 prev_state | (wakeup_mask & kWakeupMask) | kLocked);
  while (true) {
    if ((prev_state & kLocked) == 0) {
      if (state_.compare_exchange_weak(prev_state, prev_state | kLocked,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire)) {
        LogStateChange("WakeupAsync", prev_state, prev_state | kLocked);
        wakeup_mask_ |= wakeup_mask;
        arena_->GetContext<grpc_event_engine::experimental::EventEngine>()->Run(
            [this, prev_state]() {
              GRPC_LATENT_SEE_PARENT_SCOPE("Party::WakeupAsync");
              ApplicationCallbackExecCtx app_exec_ctx;
              ExecCtx exec_ctx;
              RunLockedAndUnref(this, prev_state);
            });
        return;
      }
    } else {
      if (state_.compare_exchange_weak(
              prev_state, (prev_state | wakeup_mask) - kOneRef,
              std::memory_order_acq_rel, std::memory_order_acquire)) {
        LogStateChange("WakeupAsync", prev_state, prev_state | wakeup_mask);
        return;
      }
    }
  }
}

void Party::Drop(WakeupMask) { Unref(); }

void Party::PartyIsOver() {
  CancelRemainingParticipants();
  auto arena = std::move(arena_);
  this->~Party();
}

}  // namespace grpc_core
