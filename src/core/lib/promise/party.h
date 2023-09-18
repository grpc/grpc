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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_PARTY_H
#define GRPC_SRC_CORE_LIB_PROMISE_PARTY_H

#include <grpc/support/port_platform.h>

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/strings/string_view.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/construct_destruct.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/detail/promise_factory.h"
#include "src/core/lib/promise/trace.h"
#include "src/core/lib/resource_quota/arena.h"

// Two implementations of party synchronization are provided: one using a single
// atomic, the other using a mutex and a set of state variables.
// Originally the atomic implementation was implemented, but we found some race
// conditions on Arm that were not reported by our default TSAN implementation.
// The mutex implementation was added to see if it would fix the problem, and
// it did. Later we found the race condition, so there's no known reason to use
// the mutex version - however we keep it around as a just in case measure.
// There's a thought of fuzzing the two implementations against each other as
// a correctness check of both, but that's not implemented yet.

#define GRPC_PARTY_SYNC_USING_ATOMICS
// #define GRPC_PARTY_SYNC_USING_MUTEX

#if defined(GRPC_PARTY_SYNC_USING_ATOMICS) +    \
        defined(GRPC_PARTY_SYNC_USING_MUTEX) != \
    1
#error Must define a party sync mechanism
#endif

namespace grpc_core {

namespace party_detail {

// Number of bits reserved for wakeups gives us the maximum number of
// participants.
static constexpr size_t kMaxParticipants = 16;

}  // namespace party_detail

class PartySyncUsingAtomics {
 public:
  explicit PartySyncUsingAtomics(size_t initial_refs)
      : state_(kOneRef * initial_refs) {}

  void IncrementRefCount() {
    state_.fetch_add(kOneRef, std::memory_order_relaxed);
  }
  GRPC_MUST_USE_RESULT bool RefIfNonZero();
  // Returns true if the ref count is now zero and the caller should call
  // PartyOver
  GRPC_MUST_USE_RESULT bool Unref() {
    uint64_t prev_state = state_.fetch_sub(kOneRef, std::memory_order_acq_rel);
    if ((prev_state & kRefMask) == kOneRef) {
      return UnreffedLast();
    }
    return false;
  }
  void ForceImmediateRepoll(WakeupMask mask) {
    // Or in the bit for the currently polling participant.
    // Will be grabbed next round to force a repoll of this promise.
    state_.fetch_or(mask, std::memory_order_relaxed);
  }

  // Run the update loop: poll_one_participant is called with an integral index
  // for the participant that should be polled. It should return true if the
  // participant completed and should be removed from the allocated set.
  template <typename F>
  GRPC_MUST_USE_RESULT bool RunParty(F poll_one_participant) {
    uint64_t prev_state;
    do {
      // Grab the current state, and clear the wakeup bits & add flag.
      prev_state = state_.fetch_and(kRefMask | kLocked | kAllocatedMask,
                                    std::memory_order_acquire);
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
        if (poll_one_participant(i)) {
          const uint64_t allocated_bit = (1u << i << kAllocatedShift);
          prev_state &= ~allocated_bit;
          state_.fetch_and(~allocated_bit, std::memory_order_release);
        }
      }
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
    } while (!state_.compare_exchange_weak(
        prev_state, (prev_state & (kRefMask | kAllocatedMask)),
        std::memory_order_acq_rel, std::memory_order_acquire));
    return false;
  }

  // Add new participants to the party. Returns true if the caller should run
  // the party. store is called with an array of indices of the new
  // participants. Adds a ref that should be dropped by the caller after
  // RunParty has been called (if that was required).
  template <typename F>
  GRPC_MUST_USE_RESULT bool AddParticipantsAndRef(size_t count, F store) {
    uint64_t state = state_.load(std::memory_order_acquire);
    uint64_t allocated;

    size_t slots[party_detail::kMaxParticipants];

    // Find slots for each new participant, ordering them from lowest available
    // slot upwards to ensure the same poll ordering as presentation ordering to
    // this function.
    WakeupMask wakeup_mask;
    do {
      wakeup_mask = 0;
      allocated = (state & kAllocatedMask) >> kAllocatedShift;
      size_t n = 0;
      for (size_t bit = 0; n < count && bit < party_detail::kMaxParticipants;
           bit++) {
        if (allocated & (1 << bit)) continue;
        wakeup_mask |= (1 << bit);
        slots[n++] = bit;
        allocated |= 1 << bit;
      }
      GPR_ASSERT(n == count);
      // Try to allocate this slot and take a ref (atomically).
      // Ref needs to be taken because once we store the participant it could be
      // spuriously woken up and unref the party.
    } while (!state_.compare_exchange_weak(
        state, (state | (allocated << kAllocatedShift)) + kOneRef,
        std::memory_order_acq_rel, std::memory_order_acquire));

    store(slots);

    // Now we need to wake up the party.
    state = state_.fetch_or(wakeup_mask | kLocked, std::memory_order_release);

    // If the party was already locked, we're done.
    return ((state & kLocked) == 0);
  }

  // Schedule a wakeup for the given participant.
  // Returns true if the caller should run the party.
  GRPC_MUST_USE_RESULT bool ScheduleWakeup(WakeupMask mask);

 private:
  bool UnreffedLast();

  // State bits:
  // The atomic state_ field is composed of the following:
  //   - 24 bits for ref counts
  //     1 is owned by the party prior to Orphan()
  //     All others are owned by owning wakers
  //   - 1 bit to indicate whether the party is locked
  //     The first thread to set this owns the party until it is unlocked
  //     That thread will run the main loop until no further work needs to
  //     be done.
  //   - 1 bit to indicate whether there are participants waiting to be
  //   added
  //   - 16 bits, one per participant, indicating which participants have
  //   been
  //     woken up and should be polled next time the main loop runs.

  // clang-format off
  // Bits used to store 16 bits of wakeups
  static constexpr uint64_t kWakeupMask    = 0x0000'0000'0000'ffff;
  // Bits used to store 16 bits of allocated participant slots.
  static constexpr uint64_t kAllocatedMask = 0x0000'0000'ffff'0000;
  // Bit indicating destruction has begun (refs went to zero)
  static constexpr uint64_t kDestroying    = 0x0000'0001'0000'0000;
  // Bit indicating locked or not
  static constexpr uint64_t kLocked        = 0x0000'0008'0000'0000;
  // Bits used to store 24 bits of ref counts
  static constexpr uint64_t kRefMask       = 0xffff'ff00'0000'0000;
  // clang-format on

  // Shift to get from a participant mask to an allocated mask.
  static constexpr size_t kAllocatedShift = 16;
  // How far to shift to get the refcount
  static constexpr size_t kRefShift = 40;
  // One ref count
  static constexpr uint64_t kOneRef = 1ull << kRefShift;

  std::atomic<uint64_t> state_;
};

class PartySyncUsingMutex {
 public:
  explicit PartySyncUsingMutex(size_t initial_refs) : refs_(initial_refs) {}

  void IncrementRefCount() { refs_.Ref(); }
  GRPC_MUST_USE_RESULT bool RefIfNonZero() { return refs_.RefIfNonZero(); }
  GRPC_MUST_USE_RESULT bool Unref() { return refs_.Unref(); }
  void ForceImmediateRepoll(WakeupMask mask) {
    MutexLock lock(&mu_);
    wakeups_ |= mask;
  }
  template <typename F>
  GRPC_MUST_USE_RESULT bool RunParty(F poll_one_participant) {
    WakeupMask freed = 0;
    while (true) {
      ReleasableMutexLock lock(&mu_);
      GPR_ASSERT(locked_);
      allocated_ &= ~std::exchange(freed, 0);
      auto wakeup = std::exchange(wakeups_, 0);
      if (wakeup == 0) {
        locked_ = false;
        return false;
      }
      lock.Release();
      for (size_t i = 0; wakeup != 0; i++, wakeup >>= 1) {
        if ((wakeup & 1) == 0) continue;
        if (poll_one_participant(i)) freed |= 1 << i;
      }
    }
  }

  template <typename F>
  GRPC_MUST_USE_RESULT bool AddParticipantsAndRef(size_t count, F store) {
    IncrementRefCount();
    MutexLock lock(&mu_);
    size_t slots[party_detail::kMaxParticipants];
    WakeupMask wakeup_mask = 0;
    size_t n = 0;
    for (size_t bit = 0; n < count && bit < party_detail::kMaxParticipants;
         bit++) {
      if (allocated_ & (1 << bit)) continue;
      slots[n++] = bit;
      wakeup_mask |= 1 << bit;
      allocated_ |= 1 << bit;
    }
    GPR_ASSERT(n == count);
    store(slots);
    wakeups_ |= wakeup_mask;
    return !std::exchange(locked_, true);
  }

  GRPC_MUST_USE_RESULT bool ScheduleWakeup(WakeupMask mask);

 private:
  RefCount refs_;
  Mutex mu_;
  WakeupMask allocated_ ABSL_GUARDED_BY(mu_) = 0;
  WakeupMask wakeups_ ABSL_GUARDED_BY(mu_) = 0;
  bool locked_ ABSL_GUARDED_BY(mu_) = false;
};

// A Party is an Activity with multiple participant promises.
class Party : public Activity, private Wakeable {
 private:
  // Non-owning wakeup handle.
  class Handle;

  // One participant in the party.
  class Participant {
   public:
    explicit Participant(absl::string_view name)
#ifndef NDEBUG
        : name_(name)
#endif
    {
    }
    // Poll the participant. Return true if complete.
    // Participant should take care of its own deallocation in this case.
    virtual bool Poll(bool first) = 0;

    // Destroy the participant before finishing.
    virtual void Destroy(bool polled) = 0;

    // Return a Handle instance for this participant.
    Wakeable* MakeNonOwningWakeable(Party* party);

    std::string name() const {
#ifndef NDEBUG
      return std::string(name_);
#else
      return absl::StrFormat("P[%p]", this);
#endif
    }

   protected:
    ~Participant();

   private:
    Handle* handle_ = nullptr;
#ifndef NDEBUG
    absl::string_view name_;
#endif
  };

 public:
  Party(const Party&) = delete;
  Party& operator=(const Party&) = delete;

  // Spawn one promise into the party.
  // The promise will be polled until it is resolved, or until the party is shut
  // down.
  // The on_complete callback will be called with the result of the promise if
  // it completes.
  // A maximum of sixteen promises can be spawned onto a party.
  template <typename Factory, typename OnComplete>
  void Spawn(absl::string_view name, Factory promise_factory,
             OnComplete on_complete);

  void Orphan() final { Crash("unused"); }

  // Activity implementation: not allowed to be overridden by derived types.
  void ForceImmediateRepoll(WakeupMask mask) final;
  WakeupMask CurrentParticipant() const final {
    GPR_DEBUG_ASSERT(currently_polling_ != kNotPolling);
    return 1u << currently_polling_;
  }
  Waker MakeOwningWaker() final;
  Waker MakeNonOwningWaker() final;
  std::string ActivityDebugTag(WakeupMask wakeup_mask) const final;

  void IncrementRefCount() { sync_.IncrementRefCount(); }
  void Unref() {
    if (sync_.Unref()) PartyIsOver();
  }
  RefCountedPtr<Party> Ref() {
    IncrementRefCount();
    return RefCountedPtr<Party>(this);
  }

  Arena* arena() const { return arena_; }

  class BulkSpawner {
   public:
    explicit BulkSpawner(Party* party) : party_(party) {}
    ~BulkSpawner() {
      party_->AddParticipants(participants_, num_participants_);
    }

    template <typename Factory, typename OnComplete>
    void Spawn(absl::string_view name, Factory promise_factory,
               OnComplete on_complete);

   private:
    Party* const party_;
    size_t num_participants_ = 0;
    Participant* participants_[party_detail::kMaxParticipants];
  };

 protected:
  explicit Party(Arena* arena, size_t initial_refs)
      : sync_(initial_refs), arena_(arena) {}
  ~Party() override;

  // Main run loop. Must be locked.
  // Polls participants and drains the add queue until there is no work left to
  // be done.
  // Derived types will likely want to override this to set up their
  // contexts before polling.
  // Should not be called by derived types except as a tail call to the base
  // class RunParty when overriding this method to add custom context.
  // Returns true if the party is over.
  GRPC_MUST_USE_RESULT virtual bool RunParty();

  bool RefIfNonZero() { return sync_.RefIfNonZero(); }

  // Destroy any remaining participants.
  // Should be called by derived types in response to PartyOver.
  // Needs to have normal context setup before calling.
  void CancelRemainingParticipants();

 private:
  // Concrete implementation of a participant for some promise & oncomplete
  // type.
  template <typename SuppliedFactory, typename OnComplete>
  class ParticipantImpl final : public Participant {
    using Factory = promise_detail::OncePromiseFactory<void, SuppliedFactory>;
    using Promise = typename Factory::Promise;

   public:
    ParticipantImpl(absl::string_view name, SuppliedFactory promise_factory,
                    OnComplete on_complete)
        : Participant(name), on_complete_(std::move(on_complete)) {
      Construct(&factory_, std::move(promise_factory));
      gpr_log(GPR_ERROR, "%p: factory=%zu; promise=%zu; on_complete=%zu", this,
              sizeof(factory_), sizeof(promise_), sizeof(on_complete_));
    }
    ~ParticipantImpl() {}

    bool Poll(bool first) override {
      gpr_log(GPR_ERROR, "%p poll first=%d", this, first);
      if (first) {
        auto p = factory_.Make();
        Destruct(&factory_);
        Construct(&promise_, std::move(p));
      }
      auto p = promise_();
      if (auto* r = p.value_if_ready()) {
        on_complete_(std::move(*r));
        Destroy(true);
        return true;
      }
      return false;
    }

    void Destroy(bool polled) override {
      if (polled) {
        Destruct(&promise_);
      } else {
        Destruct(&factory_);
      }
      GetContext<Arena>()->DeletePooled(this);
    }

   private:
    GPR_NO_UNIQUE_ADDRESS OnComplete on_complete_;
    union {
      GPR_NO_UNIQUE_ADDRESS Factory factory_;
      GPR_NO_UNIQUE_ADDRESS Promise promise_;
    };
  };

  struct LoadedParticipant {
    Participant* participant;
    bool first;
  };

  LoadedParticipant LoadForPoll(int i);
  LoadedParticipant LoadForDestroy(int i);
  Participant& LoadForWake(int i);

  // Notification that the party has finished and this instance can be deleted.
  // Derived types should arrange to call CancelRemainingParticipants during
  // this sequence.
  virtual void PartyOver() = 0;

  // Run the locked part of the party until it is unlocked.
  void RunLocked();
  // Called in response to Unref() hitting zero - ultimately calls PartyOver,
  // but needs to set some stuff up.
  // Here so it gets compiled out of line.
  void PartyIsOver();

  // Wakeable implementation
  void Wakeup(WakeupMask wakeup_mask) final;
  void WakeupAsync(WakeupMask wakeup_mask) final;
  void Drop(WakeupMask wakeup_mask) final;

  // Add a participant (backs Spawn, after type erasure to ParticipantFactory).
  void AddParticipants(Participant** participant, size_t count);

  virtual grpc_event_engine::experimental::EventEngine* event_engine()
      const = 0;

  // Sentinal value for currently_polling_ when no participant is being polled.
  static constexpr uint8_t kNotPolling = 255;

#ifdef GRPC_PARTY_SYNC_USING_ATOMICS
  PartySyncUsingAtomics sync_;
#elif defined(GRPC_PARTY_SYNC_USING_MUTEX)
  PartySyncUsingMutex sync_;
#else
#error No synchronization method defined
#endif

  static constexpr uintptr_t kNotPolled = 1;

  Arena* const arena_;
  uint8_t currently_polling_ = kNotPolling;
  // All current participants, using a tagged format.
  // If the lower bit is unset, then this is a Participant*.
  // If the lower bit is set, then this is a ParticipantFactory*.
  std::atomic<uintptr_t> participants_[party_detail::kMaxParticipants] = {};
};

template <typename Factory, typename OnComplete>
void Party::BulkSpawner::Spawn(absl::string_view name, Factory promise_factory,
                               OnComplete on_complete) {
  if (grpc_trace_promise_primitives.enabled()) {
    gpr_log(GPR_DEBUG, "%s[bulk_spawn] On %p queue %s",
            party_->DebugTag().c_str(), this, std::string(name).c_str());
  }
  gpr_log(GPR_ERROR, "PARTICIPANT_SIZE[%s]:%zu", std::string(name).c_str(),
          sizeof(ParticipantImpl<Factory, OnComplete>));
  participants_[num_participants_++] =
      party_->arena_->NewPooled<ParticipantImpl<Factory, OnComplete>>(
          name, std::move(promise_factory), std::move(on_complete));
}

template <typename Factory, typename OnComplete>
void Party::Spawn(absl::string_view name, Factory promise_factory,
                  OnComplete on_complete) {
  BulkSpawner(this).Spawn(name, std::move(promise_factory),
                          std::move(on_complete));
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_PARTY_H
