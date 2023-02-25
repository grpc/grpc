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

#include "absl/strings/string_view.h"
#include "absl/types/variant.h"

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/construct_destruct.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/detail/promise_factory.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/arena.h"

namespace grpc_core {

// A Party is an Activity with multiple participant promises.
class Party : public Activity, private Wakeable {
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

  void IncrementRefCount(DebugLocation whence = {});
  void Unref(DebugLocation whence = {});
  RefCountedPtr<Party> Ref() {
    IncrementRefCount();
    return RefCountedPtr<Party>(this);
  }

 protected:
  explicit Party(Arena* arena, size_t initial_refs)
      : state_(kOneRef * initial_refs), arena_(arena) {}
  ~Party() override;

  // Main run loop. Must be locked.
  // Polls participants and drains the add queue until there is no work left to
  // be done.
  // Derived types will likely want to override this to set up their
  // contexts before polling.
  // Should not be called by derived types except as a tail call to the base
  // class RunParty when overriding this method to add custom context.
  // Returns true if the party is over.
  virtual bool RunParty() GRPC_MUST_USE_RESULT;

  bool RefIfNonZero();

  // Destroy any remaining participants.
  // Should be called by derived types in response to PartyOver.
  // Needs to have normal context setup before calling.
  void CancelRemainingParticipants();

 private:
  // Non-owning wakeup handle.
  class Handle;

  // One participant in the party.
  class Participant {
   public:
    explicit Participant(absl::string_view name) : name_(name) {}
    // Poll the participant. Return true if complete.
    // Participant should take care of its own deallocation in this case.
    virtual bool Poll() = 0;

    // Destroy the participant before finishing.
    virtual void Destroy() = 0;

    // Return a Handle instance for this participant.
    Wakeable* MakeNonOwningWakeable(Party* party);

    absl::string_view name() const { return name_; }

   protected:
    ~Participant();

   private:
    Handle* handle_ = nullptr;
    absl::string_view name_;
  };

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
    }
    ~ParticipantImpl() {
      if (!started_) {
        Destruct(&factory_);
      } else {
        Destruct(&promise_);
      }
    }

    bool Poll() override {
      if (!started_) {
        auto p = factory_.Make();
        Destruct(&factory_);
        Construct(&promise_, std::move(p));
        started_ = true;
      }
      auto p = promise_();
      if (auto* r = absl::get_if<kPollReadyIdx>(&p)) {
        on_complete_(std::move(*r));
        GetContext<Arena>()->DeletePooled(this);
        return true;
      }
      return false;
    }

    void Destroy() override { GetContext<Arena>()->DeletePooled(this); }

   private:
    union {
      GPR_NO_UNIQUE_ADDRESS Factory factory_;
      GPR_NO_UNIQUE_ADDRESS Promise promise_;
    };
    GPR_NO_UNIQUE_ADDRESS OnComplete on_complete_;
    bool started_ = false;
  };

  // Notification that the party has finished and this instance can be deleted.
  // Derived types should arrange to call CancelRemainingParticipants during
  // this sequence.
  virtual void PartyOver() = 0;

  // Run the locked part of the party until it is unlocked.
  void RunLocked();

  // Wakeable implementation
  void Wakeup(WakeupMask wakeup_mask) final;
  void Drop(WakeupMask wakeup_mask) final;

  // Organize to wake up some participants.
  void ScheduleWakeup(WakeupMask mask);
  // Add a participant (backs Spawn, after type erasure to ParticipantFactory).
  void AddParticipant(Participant* participant);

  // Convert a state into a string.
  static std::string StateToString(uint64_t state);

  // Sentinal value for currently_polling_ when no participant is being polled.
  static constexpr uint8_t kNotPolling = 255;

  // State bits:
  // The atomic state_ field is composed of the following:
  //   - 24 bits for ref counts
  //     1 is owned by the party prior to Orphan()
  //     All others are owned by owning wakers
  //   - 1 bit to indicate whether the party is locked
  //     The first thread to set this owns the party until it is unlocked
  //     That thread will run the main loop until no further work needs to be
  //     done.
  //   - 1 bit to indicate whether there are participants waiting to be added
  //   - 16 bits, one per participant, indicating which participants have been
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
  // Number of bits reserved for wakeups gives us the maximum number of
  // participants.
  static constexpr size_t kMaxParticipants = 16;

  std::atomic<uint64_t> state_;
  Arena* const arena_;
  uint8_t currently_polling_ = kNotPolling;
  // All current participants, using a tagged format.
  // If the lower bit is unset, then this is a Participant*.
  // If the lower bit is set, then this is a ParticipantFactory*.
  std::atomic<Participant*> participants_[kMaxParticipants] = {};
};

template <typename Factory, typename OnComplete>
void Party::Spawn(absl::string_view name, Factory promise_factory,
                  OnComplete on_complete) {
  AddParticipant(arena_->NewPooled<ParticipantImpl<Factory, OnComplete>>(
      name, std::move(promise_factory), std::move(on_complete)));
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_PARTY_H
