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

#include <atomic>
#include <cstdint>
#include <initializer_list>

#include "absl/base/thread_annotations.h"
#include "absl/strings/str_format.h"
#include "party.h"

#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/trace.h"

#ifdef GRPC_MAXIMIZE_THREADYNESS
#include "src/core/lib/gprpp/thd.h"       // IWYU pragma: keep
#include "src/core/lib/iomgr/exec_ctx.h"  // IWYU pragma: keep
#endif

namespace grpc_core {

///////////////////////////////////////////////////////////////////////////////
// PartySyncUsingAtomics

GRPC_MUST_USE_RESULT bool PartySyncUsingAtomics::RefIfNonZero() {
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

bool PartySyncUsingAtomics::UnreffedLast() {
  uint64_t prev_state =
      state_.fetch_or(kDestroying | kLocked, std::memory_order_acq_rel);
  return (prev_state & kLocked) == 0;
}

bool PartySyncUsingAtomics::ScheduleWakeup(WakeupMask mask) {
  // Or in the wakeup bit for the participant, AND the locked bit.
  uint64_t prev_state = state_.fetch_or((mask & kWakeupMask) | kLocked,
                                        std::memory_order_acq_rel);
  // If the lock was not held now we hold it, so we need to run.
  return ((prev_state & kLocked) == 0);
}

///////////////////////////////////////////////////////////////////////////////
// PartySyncUsingMutex

bool PartySyncUsingMutex::ScheduleWakeup(WakeupMask mask) {
  MutexLock lock(&mu_);
  wakeups_ |= mask;
  return !std::exchange(locked_, true);
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
    GPR_ASSERT(party_ != nullptr);
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

Party::LoadedParticipant Party::LoadForDestroy(int i) {
  uintptr_t p = participants_[i].load(std::memory_order_acquire);
  bool first = (p & kNotPolled) == kNotPolled;
  return LoadedParticipant{reinterpret_cast<Participant*>(p & ~kNotPolled),
                           first};
}

void Party::CancelRemainingParticipants() {
  ScopedActivity activity(this);
  promise_detail::Context<Arena> arena_ctx(arena_);
  for (size_t i = 0; i < party_detail::kMaxParticipants; i++) {
    LoadedParticipant p = LoadForDestroy(i);
    if (p.participant == nullptr) continue;
    p.participant->Destroy(!p.first);
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

Party::Participant& Party::LoadForWake(int i) {
  return *reinterpret_cast<Participant*>(
      participants_[i].load(std::memory_order_relaxed));
}

Waker Party::MakeNonOwningWaker() {
  GPR_DEBUG_ASSERT(currently_polling_ != kNotPolling);
  return Waker(LoadForWake(currently_polling_).MakeNonOwningWakeable(this),
               1u << currently_polling_);
}

void Party::ForceImmediateRepoll(WakeupMask mask) {
  GPR_DEBUG_ASSERT(is_current());
  sync_.ForceImmediateRepoll(mask);
}

Party::LoadedParticipant Party::LoadForPoll(int i) {
  uintptr_t p =
      participants_[i].fetch_and(~kNotPolled, std::memory_order_acquire);
  bool first = (p & kNotPolled) == kNotPolled;
  return LoadedParticipant{reinterpret_cast<Participant*>(p & ~kNotPolled),
                           first};
}

void Party::RunLocked() {
  auto body = [this]() {
    if (RunParty()) {
      ScopedActivity activity(this);
      PartyOver();
    }
  };
#ifdef GRPC_MAXIMIZE_THREADYNESS
  Thread thd(
      "RunParty",
      [body]() {
        ApplicationCallbackExecCtx app_exec_ctx;
        ExecCtx exec_ctx;
        body();
      },
      nullptr, Thread::Options().set_joinable(false));
  thd.Start();
#else
  body();
#endif
}

bool Party::RunParty() {
  ScopedActivity activity(this);
  promise_detail::Context<Arena> arena_ctx(arena_);
  return sync_.RunParty([this](int i) {
    // If the participant is null, skip.
    // This allows participants to complete whilst wakers still exist
    // somewhere.
    LoadedParticipant p = LoadForPoll(i);
    if (p.participant == nullptr) {
      if (grpc_trace_promise_primitives.enabled()) {
        gpr_log(GPR_DEBUG, "%s[party] wakeup %d already complete",
                DebugTag().c_str(), i);
      }
      return false;
    }
    absl::string_view name;
    if (grpc_trace_promise_primitives.enabled()) {
      name = p.participant->name();
      gpr_log(GPR_DEBUG, "%s[%s] begin job %d", DebugTag().c_str(),
              std::string(name).c_str(), i);
    }
    // Poll the participant.
    currently_polling_ = i;
    bool done = p.participant->Poll(p.first);
    currently_polling_ = kNotPolling;
    if (done) {
      if (!name.empty()) {
        gpr_log(GPR_DEBUG, "%s[%s] end poll and finish job %d",
                DebugTag().c_str(), std::string(name).c_str(), i);
      }
      participants_[i].store(0, std::memory_order_relaxed);
    } else if (!name.empty()) {
      gpr_log(GPR_DEBUG, "%s[%s] end poll", DebugTag().c_str(),
              std::string(name).c_str());
    }
    return done;
  });
}

void Party::AddParticipants(Participant** participants, size_t count) {
  bool run_party = sync_.AddParticipantsAndRef(
      count, [this, participants, count](size_t* slots) {
        for (size_t i = 0; i < count; i++) {
          participants_[slots[i]].store(
              reinterpret_cast<uintptr_t>(participants[i]) | kNotPolled,
              std::memory_order_release);
        }
      });
  if (run_party) RunLocked();
  Unref();
}

void Party::Wakeup(WakeupMask wakeup_mask) {
  if (sync_.ScheduleWakeup(wakeup_mask)) RunLocked();
  Unref();
}

void Party::WakeupAsync(WakeupMask wakeup_mask) {
  if (sync_.ScheduleWakeup(wakeup_mask)) {
    event_engine()->Run([this]() {
      ApplicationCallbackExecCtx app_exec_ctx;
      ExecCtx exec_ctx;
      RunLocked();
      Unref();
    });
  } else {
    Unref();
  }
}

void Party::Drop(WakeupMask) { Unref(); }

void Party::PartyIsOver() {
  ScopedActivity activity(this);
  PartyOver();
}

}  // namespace grpc_core
