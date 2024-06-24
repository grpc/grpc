// Copyright 2024 gRPC authors.
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

#include "absl/functional/any_invocable.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_core {

class StepTimer : public InternallyRefCounted<StepTimer> {
 public:
  struct Handle {
    uint64_t epoch;
    size_t id;
  };

  Handle Add(absl::AnyInvocable<void()> cb) {
    MutexLock lock(&mu_);
    queueing_.push_back(std::move(cb));
    if (next_tick_ ==
        grpc_event_engine::experimental::EventEngine::TaskHandle::kInvalid) {
      next_tick_ =
          engine_->RunAfter(interval_, [self = Ref()]() { self->Run(); });
    }
    return Handle{epoch_ + 1, queueing_.size() - 1};
  }

  bool Cancel(Handle handle) {
    absl::AnyInvocable<void()> going;
    CHECK_LE(handle.epoch, epoch_ + 1);  // epoch_ + 1 is the next epoch
    MutexLock lock(&mu_);
    if (handle.epoch == epoch_) {
      going.swap(imminent_[handle.id]);
      return going != nullptr;
    } else if (handle.epoch == epoch_ + 1) {
      going.swap(queueing_[handle.id]);
      return going != nullptr;
    }
    return false;
  }

  void Orphan() override {
    engine_->Cancel(next_tick_);
    Unref();
  }

 private:
  void Run() {
    std::vector<absl::AnyInvocable<void()>> running;
    {
      MutexLock lock(&mu_);
      imminent_.swap(running);
      imminent_.swap(queueing_);
      if (!imminent_.empty()) {
        next_tick_ =
            engine_->RunAfter(interval_, [self = Ref()]() { self->Run(); });
      } else {
        next_tick_ =
            grpc_event_engine::experimental::EventEngine::TaskHandle::kInvalid;
      }
    }
    for (auto& cb : running) {
      if (cb == nullptr) continue;
      cb();
    }
  }

  Mutex mu_;
  uint64_t epoch_ = 1;
  std::vector<absl::AnyInvocable<void()>> imminent_;
  std::vector<absl::AnyInvocable<void()>> queueing_;
  grpc_event_engine::experimental::EventEngine* engine_;
  grpc_event_engine::experimental::EventEngine::TaskHandle next_tick_ =
      grpc_event_engine::experimental::EventEngine::TaskHandle::kInvalid;
  const grpc_event_engine::experimental::EventEngine::Duration interval_;
};

}  // namespace grpc_core
