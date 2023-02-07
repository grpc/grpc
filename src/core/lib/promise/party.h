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

#include "activity.h"

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"

namespace grpc_core {

// A Party is an Activity with multiple participant promises.
class Party : public Activity, private Wakeable {
 public:
  Party(Arena* arena,
        grpc_event_engine::experimental::EventEngine* event_engine)
      : arena_(arena), event_engine_(event_engine) {}

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
    virtual void Poll() = 0;
  };

  template <typename Promise, typename OnComplete>
  class ParticipantImpl final : public Participant {
   public:
    ParticipantImpl(Promise promise, OnComplete on_complete)
        : promise_(std::move(promise)), on_complete_(std::move(on_complete)) {}

    void Poll() override {
      auto p = promise_();
      if (auto* r = absl::get_if<kPollReadyIdx>(&p)) {
        on_complete_(std::move(*r));
      }
    }

   private:
    Promise promise_;
    OnComplete on_complete_;
  };

  void Wakeup(void* arg) final;
  void Drop(void* arg) final;

  void Ref();
  void Unref();
  void ScheduleWakeup(uint32_t participant_index);

  // Derived types will likely want to override this to set up their contexts
  // before polling.
  virtual void Run();

  static constexpr uint8_t kNotPolling = 255;

  static constexpr uint32_t kParticipantMask = 0x7fff;
  static constexpr uint32_t kAwoken = 0x8000;
  static constexpr uint32_t kRefMask = 0xffff0000;
  static constexpr uint32_t kOneRef = 0x00010000;

  Arena* const arena_;
  grpc_event_engine::experimental::EventEngine* const event_engine_;
  Mutex mu_;
  absl::InlinedVector<Arena::PoolPtr<Participant>, 1> participants_
      ABSL_GUARDED_BY(mu_);
  std::atomic<uint32_t> wakeups_and_refs_{0};
  uint16_t repoll_participants_ = 0;
  uint8_t currently_polling_ = kNotPolling;
};

template <typename Promise, typename OnComplete>
void Party::Spawn(Promise promise, OnComplete on_complete) {
  for (size_t i = 0; i < participants_.size(); i++) {
    if (participants_[i] == nullptr) {
      participants_[i] =
          arena_->MakePooled<ParticipantImpl<Promise, OnComplete>>(
              std::move(promise), std::move(on_complete));
      return;
    }
  }
  participants_.push_back(
      arena_->MakePooled<ParticipantImpl<Promise, OnComplete>>(
          std::move(promise), std::move(on_complete)));
}

}  // namespace grpc_core

#endif
