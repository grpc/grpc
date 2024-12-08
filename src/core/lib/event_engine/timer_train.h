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
//
#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_TIMER_TRAIN_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_TIMER_TRAIN_H

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/cpu.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>
#include <stddef.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "src/core/lib/event_engine/handle_containers.h"
#include "src/core/lib/event_engine/posix_engine/timer.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "src/core/util/useful.h"

namespace grpc_event_engine {
namespace experimental {

// A TimerTrain is a thread-safe class that allows users to schedule closures
// with a delay on a timer list.
//
// The timer list is implemented using a SlackedTimerList.
//
// The TimerTrain is responsible for periodically checking the timer list for
// expired timers and running them.
//
// The TimerTrain is driven by an EventEngine::Closure that is scheduled on the
// EventEngine with a delay equal to the timer list check period. Upon
// executing, the TimerTrain schedules itself again on the EventEngine with the
// same delay.
//
// The TimerTrain also provides an API to quickly cancel and extend the delay
// of scheduled closures.
//
class TimerTrain {
 private:
  class DefaultHost final : public TimerListHost {
   public:
    void Kick() override {}
    grpc_core::Timestamp Now() override {
      return grpc_core::Timestamp::FromTimespecRoundDown(
          gpr_now(GPR_CLOCK_MONOTONIC));
    }
    ~DefaultHost() override = default;
  };

 public:
  struct Options {
    grpc_core::Duration period;
    int num_shards;
    std::shared_ptr<EventEngine> event_engine;
  };

  TimerTrain(std::unique_ptr<TimerListHost> host, Options options)
      : impl_(std::make_shared<Impl>(std::move(host), std::move(options))) {
    impl_->StartTrain();
  }

  explicit TimerTrain(Options options)
      : impl_(std::make_shared<Impl>(std::make_unique<DefaultHost>(),
                                     std::move(options))) {
    impl_->StartTrain();
  }

  ~TimerTrain() { impl_->StopTrain(); }

  EventEngine::TaskHandle RunAfter(EventEngine::Duration delay,
                                   absl::AnyInvocable<void()> callback) {
    return impl_->RunAfter(delay, std::move(callback));
  }

  bool Cancel(EventEngine::TaskHandle handle) { return impl_->Cancel(handle); }

  bool Extend(EventEngine::TaskHandle handle, EventEngine::Duration delay) {
    return impl_->Extend(handle, delay);
  }

 private:
  class Impl : public std::enable_shared_from_this<Impl> {
   private:
    struct Shard {
      grpc_core::Mutex mu;
      TaskHandleSet known_handles ABSL_GUARDED_BY(mu);
    };

   public:
    Impl(std::unique_ptr<TimerListHost> host, TimerTrain::Options&& options)
        : host_(std::move(host)),
          timer_list_(std::make_unique<SlackedTimerList>(
              host_.get(),
              SlackedTimerList::Options{.num_shards = options.num_shards,
                                        .resolution = options.period})),
          num_shards_(options.num_shards),
          period_(options.period),
          event_engine_(std::move(options.event_engine)) {
      if (num_shards_ < 1) {
        num_shards_ = grpc_core::Clamp(2 * gpr_cpu_num_cores(), 1u, 32u);
      }
      shards_ = std::unique_ptr<Shard[]>(new Shard[num_shards_]);
    }

    ~Impl();

    void StartTrain();

    void StopTrain();

    bool Cancel(EventEngine::TaskHandle handle);

    bool Extend(EventEngine::TaskHandle handle, EventEngine::Duration delay);

    EventEngine::TaskHandle RunAfter(EventEngine::Duration delay,
                                     absl::AnyInvocable<void()> callback);

    void ExecuteStep();

   private:
    Shard* GetShard(EventEngine::TaskHandle handle);

    void RunSomeClosures(
        std::vector<experimental::EventEngine::Closure*> closures);

    struct ClosureData;
    friend struct ClosureData;
    grpc_core::Mutex shutdown_mu_;
    bool shutdown_ ABSL_GUARDED_BY(shutdown_mu_) = false;
    std::atomic<int32_t> aba_token_{0};
    std::unique_ptr<TimerListHost> host_;
    std::unique_ptr<TimerListInterface> timer_list_;
    int num_shards_;
    EventEngine::Duration period_;
    EventEngine::TaskHandle train_control_handle_;
    std::shared_ptr<EventEngine> event_engine_;
    std::unique_ptr<Shard[]> shards_;
  };

  std::shared_ptr<Impl> impl_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_TIMER_TRAIN_H
