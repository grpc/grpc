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

#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"
#include "absl/types/variant.h"
#include "detail/promise_factory.h"

#include <grpc/support/log.h>

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/detail/promise_like.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/arena.h"

namespace grpc_core {

// A Party is an Activity with multiple participant promises.
class Party : public Activity, private Wakeable {
 public:
  Party(const Party&) = delete;
  Party& operator=(const Party&) = delete;

  // Spawn one promise onto the arena.
  // The promise will be polled until it is resolved, or until the party is shut
  // down.
  // The on_complete callback will be called with the result of the promise if
  // it completes.
  // A maximum of sixteen promises can be spawned onto a party.
  template <typename Promise, typename OnComplete>
  void Spawn(absl::string_view name, Promise promise, OnComplete on_complete);

  void Orphan() final;

  // Activity implementation: not allowed to be overridden by derived types.
  void ForceImmediateRepoll(WakeupMask arg) final;
  WakeupMask CurrentParticipant() const final {
    GPR_DEBUG_ASSERT(currently_polling_ != kNotPolling);
    return 1u << currently_polling_;
  }
  Waker MakeOwningWaker() final;
  Waker MakeNonOwningWaker() final;
  std::string ActivityDebugTag(WakeupMask arg) const final;

 protected:
  Party() = default;
  ~Party() override;

  // Non-owning wakeup handle.
  class Handle;

  // One participant in the party.
  class Participant {
   public:
    explicit Participant(absl::string_view name) : name_(name) {}
    virtual ~Participant();
    // Poll the participant. Return true if complete.
    virtual bool Poll() = 0;

    // Return a Handle instance for this participant.
    Wakeable* MakeNonOwningWakeable(Party* party);

    absl::string_view name() const { return name_; }

   private:
    Handle* handle_ = nullptr;
    absl::string_view name_;
  };

  // Make a participant
  class ParticipantFactory {
   public:
    virtual ~ParticipantFactory() = default;
    virtual absl::string_view name() const = 0;
    virtual Arena::PoolPtr<Participant> Make(Arena* arena) = 0;
    virtual ParticipantFactory* TakeNew() = 0;
  };

  // Main run loop. Must be locked.
  // Polls participants and drains the add queue until there is no work left to
  // be done.
  // Derived types will likely want to override this to set up their
  // contexts before polling.
  virtual void RunParty(ParticipantFactory* new_participant);

  // Internal ref counting
  void Ref();
  void Unref();
  bool RefIfNonZero();

 private:
  // Concrete implementation of a participant for some promise & oncomplete
  // type.
  template <typename Promise, typename OnComplete>
  class ParticipantImpl final : public Participant {
   public:
    ParticipantImpl(absl::string_view name, Promise promise,
                    OnComplete on_complete)
        : Participant(name),
          promise_(std::move(promise)),
          on_complete_(std::move(on_complete)) {}

    bool Poll() override {
      auto p = promise_();
      if (auto* r = absl::get_if<kPollReadyIdx>(&p)) {
        on_complete_(std::move(*r));
        return true;
      }
      return false;
    }

   private:
    GPR_NO_UNIQUE_ADDRESS promise_detail::PromiseLike<Promise> promise_;
    GPR_NO_UNIQUE_ADDRESS OnComplete on_complete_;
  };

  // One participant that's been spawned, but has not yet made it into
  // participants_.
  // Since it's impossible to block on locking this type, we form a queue of
  // participants waiting and drain that prior to polling.
  struct AddingParticipant {
    ParticipantFactory* factory;
    AddingParticipant* next;
  };

  // Wakeable implementation
  void Wakeup(WakeupMask arg) final;
  void Drop(WakeupMask arg) final;

  // Organize to wake up some participants.
  void ScheduleWakeup(WakeupMask mask);
  // Drain the add queue.
  void DrainAdds(uint64_t& wakeups);
  // Take a new participant, and add it to the participants_ array.
  // Returns the index of the participant in the array.
  size_t SituateNewParticipant(Arena::PoolPtr<Participant> new_participant);
  // Add a participant (backs Spawn, after type erasure to ParticipantFactory).
  void AddParticipant(ParticipantFactory* factory);

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
  static constexpr uint64_t kWakeupMask  = 0x0000'0000'0000'ffff;
  // Bit indicating orphaned or not
  static constexpr uint64_t kOrphaned    = 0x0000'0000'0001'0000;
  // Bit indicating locked or not
  static constexpr uint64_t kLocked      = 0x0000'0000'0100'0000;
  // Bit indicating whether there are adds pending
  static constexpr uint64_t kAddsPending = 0x0000'0000'1000'0000;
  // Bits used to store 24 bits of ref counts
  static constexpr uint64_t kRefMask     = 0xffff'ff00'0000'0000;
  // clang-format on

  // Number of bits reserved for wakeups gives us the maximum number of
  // participants.
  static constexpr size_t kMaxParticipants = 16;
  // How far to shift to get the refcount
  static constexpr size_t kRefShift = 40;
  // One ref count
  static constexpr uint64_t kOneRef = 1ull << kRefShift;

  absl::InlinedVector<Arena::PoolPtr<Participant>, 1> participants_;
  std::atomic<uint64_t> state_{kOneRef};
  std::atomic<AddingParticipant*> adding_{nullptr};
  uint8_t currently_polling_ = kNotPolling;
};

template <typename Promise, typename OnComplete>
void Party::Spawn(absl::string_view name, Promise promise,
                  OnComplete on_complete) {
  using FactoryType = promise_detail::OncePromiseFactory<void, Promise>;
  using PromiseType = typename FactoryType::Promise;

  class Factory final : public ParticipantFactory {
   public:
    struct ConstructWithFactory {};
    Factory(ConstructWithFactory, absl::string_view name, FactoryType promise,
            OnComplete on_complete)
        : name_(name),
          promise_(std::move(promise)),
          on_complete_(std::move(on_complete)) {}
    Factory(absl::string_view name, Promise promise, OnComplete on_complete)
        : Factory(ConstructWithFactory{}, name, FactoryType(std::move(promise)),
                  std::move(on_complete)) {}
    absl::string_view name() const override { return name_; }
    Arena::PoolPtr<Participant> Make(Arena* arena) override {
      return arena->MakePooled<ParticipantImpl<PromiseType, OnComplete>>(
          name_, promise_.Make(), std::move(on_complete_));
    }
    ParticipantFactory* TakeNew() override {
      return new Factory(ConstructWithFactory{}, name_, std::move(promise_),
                         std::move(on_complete_));
    }

   private:
    absl::string_view name_;
    GPR_NO_UNIQUE_ADDRESS FactoryType promise_;
    GPR_NO_UNIQUE_ADDRESS OnComplete on_complete_;
  };

  Factory factory(name, std::move(promise), std::move(on_complete));
  AddParticipant(&factory);
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_PARTY_H
