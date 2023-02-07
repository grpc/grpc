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

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"

namespace grpc_core {

// A Party is an Activity with multiple participant promises.
class Party : public Activity, private Wakeable {
 public:
  explicit Party(Arena* arena) : arena_(arena) {}

  template <typename Promise, typename OnComplete>
  void Spawn(Promise promise, OnComplete on_complete);

  void Orphan() final;

  void ForceImmediateRepoll() final;
  Waker MakeOwningWaker() final;
  Waker MakeNonOwningWaker() final;

 private:
  class Participant {
   public:
    // Poll the participant.
    virtual bool Poll() = 0;
  };

  template <typename Promise, typename OnComplete>
  class ParticipantImpl final : public Participant {
   public:
    ParticipantImpl(Promise promise, OnComplete on_complete)
        : promise_(std::move(promise)), on_complete_(std::move(on_complete)) {}

    bool Poll() override {
      auto p = promise_();
      if (auto* r = absl::get_if<kPollReadyIdx>(&p)) {
        on_complete_(std::move(*r));
        return true;
      }
      return false;
    }

   private:
    GPR_NO_UNIQUE_ADDRESS Promise promise_;
    GPR_NO_UNIQUE_ADDRESS OnComplete on_complete_;
  };

  struct AddingParticipant {
    Arena::PoolPtr<Participant> participant;
    AddingParticipant* next;
  };

  void Wakeup(void* arg) final;
  void Drop(void* arg) final;

  void Ref();
  void Unref();
  void ScheduleWakeup(uint64_t participant_index);
  void AddParticipant(Arena::PoolPtr<Participant> participant);
  void DrainAdds(uint64_t& wakeups);
  size_t SituateNewParticipant(Arena::PoolPtr<Participant> new_participant);

  // Derived types will likely want to override this to set up their contexts
  // before polling.
  virtual void Run();

  static constexpr uint8_t kNotPolling = 255;

  // clang-format off
  static constexpr uint64_t kParticipantMask = 0x0000'0000'0000'ffff;
  static constexpr uint64_t kAwoken          = 0x0000'0000'0100'0000;
  static constexpr uint64_t kAddsPending     = 0x0000'0000'1000'0000;
  static constexpr uint64_t kRefMask         = 0xffff'ff00'0000'0000;
  static constexpr uint64_t kOneRef          = 0x0000'0100'0000'0000;
  // clang-format on

  Arena* const arena_;
  absl::InlinedVector<Arena::PoolPtr<Participant>, 1> participants_;
  std::atomic<uint64_t> wakeups_and_refs_{0};
  std::atomic<AddingParticipant*> adding_{nullptr};
  uint8_t currently_polling_ = kNotPolling;
};

template <typename Promise, typename OnComplete>
void Party::Spawn(Promise promise, OnComplete on_complete) {
  AddParticipant(arena_->MakePooled<ParticipantImpl<Promise, OnComplete>>(
      std::move(promise), std::move(on_complete)));
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_PARTY_H
