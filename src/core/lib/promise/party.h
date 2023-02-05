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

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"

namespace grpc_core {

// A Party is an Activity with multiple participant promises.
class Party : public Activity {
 public:
  template <typename Promise, typename OnComplete>
  void Spawn(Promise promise, OnComplete on_complete);

  void ForceImmediateRepoll() final;
  Waker MakeOwningWaker() final;
  Waker MakeNonOwningWaker() final;

 private:
  class Participant : public Wakeable {
   public:
    explicit Participant(Party* party);
    // Poll the participant.
    virtual void Poll() = 0;

    void Ref();
    void Unref();
    void Wakeup() final;
    void Drop() final;
    std::string ActivityDebugTag() const final;

   private:
    RefCount refs_;
    Party* const party_;
  };

  template <typename Promise, typename OnComplete>
  class ParticipantImpl final : public Participant {
   public:
    ParticipantImpl(Party* party, Promise promise, OnComplete on_complete)
        : Participant(party),
          promise_(std::move(promise)),
          on_complete_(std::move(on_complete)) {}

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

  void Farewell(Participant* participant);
  void ScheduleWakeupFor(Participant* participant);
  // Derived types will likely want to override this to set up their contexts
  // before polling.
  virtual void Run();

  Arena* const arena_;
  absl::InlinedVector<Participant*, 1> active_participants_;
  absl::InlinedVector<Participant*, 1> wakeup_participants_;
  absl::InlinedVector<Participant*, 1> farewell_participants_;
  Participant* polling_participant_ = nullptr;
  grpc_event_engine::experimental::EventEngine* event_engine_ = nullptr;
};

template <typename Promise, typename OnComplete>
void Party::Spawn(Promise promise, OnComplete on_complete) {
  active_participants_.push_back(
      arena_->New<ParticipantImpl<Promise, OnComplete>>(
          this, std::move(promise), std::move(on_complete)));
}

}  // namespace grpc_core

#endif
