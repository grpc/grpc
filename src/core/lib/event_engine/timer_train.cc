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
#include "src/core/lib/event_engine/timer_train.h"

#include <grpc/event_engine/event_engine.h>

#include <atomic>
#include <cstdint>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/types/optional.h"
#include "src/core/lib/event_engine/posix_engine/timer.h"
#include "src/core/lib/event_engine/utils.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "src/core/util/useful.h"

namespace grpc_event_engine {
namespace experimental {

struct TimerTrain::Impl::ClosureData final : public EventEngine::Closure {
  absl::AnyInvocable<void()> cb;
  Timer timer;
  Impl::Shard* shard;
  EventEngine::TaskHandle handle;

  void Run() override {
    {
      grpc_core::MutexLock lock(&shard->mu);
      shard->known_handles.erase(handle);
    }
    cb();
    delete this;
  }
};

EventEngine::TaskHandle TimerTrain::Impl::RunAfter(
    EventEngine::Duration delay, absl::AnyInvocable<void()> callback) {
  auto* cd = new ClosureData;
  int64_t shard_idx = grpc_core::HashPointer(cd, num_shards_);
  Shard* shard = &shards_[shard_idx];
  cd->cb = std::move(callback);
  cd->shard = shard;
  auto when_ts = ToTimestamp(host_->Now(), delay);
  EventEngine::TaskHandle handle{
      reinterpret_cast<intptr_t>(cd),
      shard_idx << 32 | aba_token_.fetch_add(1, std::memory_order_relaxed)};
  cd->handle = handle;

  grpc_core::MutexLock lock(&shard->mu);
  shard->known_handles.insert(handle);
  timer_list_->TimerInit(&cd->timer, when_ts, cd);
  return handle;
}

TimerTrain::Impl::Shard* TimerTrain::Impl::GetShard(
    EventEngine::TaskHandle handle) {
  int shard_idx = handle.keys[1] >> 32;
  CHECK(shard_idx >= 0 && shard_idx < num_shards_);
  return &shards_[shard_idx];
}

bool TimerTrain::Impl::Cancel(EventEngine::TaskHandle handle) {
  Shard* shard = GetShard(handle);
  if (shard == nullptr) {
    return false;
  }
  grpc_core::MutexLock lock(&shard->mu);
  if (!shard->known_handles.erase(handle)) return false;
  auto* cd = reinterpret_cast<ClosureData*>(handle.keys[0]);
  if (timer_list_->TimerCancel(&cd->timer)) {
    delete cd;
    return true;
  }
  return false;
}

bool TimerTrain::Impl::Extend(EventEngine::TaskHandle handle,
                              EventEngine::Duration delay) {
  Shard* shard = GetShard(handle);
  if (shard == nullptr) {
    return false;
  }
  grpc_core::MutexLock lock(&shard->mu);
  if (!shard->known_handles.contains(handle)) return false;
  auto* cd = reinterpret_cast<ClosureData*>(handle.keys[0]);
  return timer_list_->TimerExtend(
      &cd->timer, grpc_core::Duration::NanosecondsRoundUp(delay.count()));
}

void TimerTrain::Impl::RunSomeClosures(
    std::vector<experimental::EventEngine::Closure*> closures) {
  for (auto* closure : closures) {
    event_engine_->Run(closure);
  }
}

void TimerTrain::Impl::StartTrain() {
  train_control_handle_ = event_engine_->RunAfter(
      period_, [self = shared_from_this()]() { self->ExecuteStep(); });
}

void TimerTrain::Impl::StopTrain() {
  grpc_core::MutexLock lock(&shutdown_mu_);
  CHECK_NE(std::exchange(shutdown_, true), true);
  event_engine_->Cancel(train_control_handle_);
}

TimerTrain::Impl::~Impl() {
  // Delete all pending closures in all shards.
  for (int i = 0; i < num_shards_; ++i) {
    Shard& shard = shards_[i];
    grpc_core::MutexLock lock(&shard.mu);
    for (auto& handle : shard.known_handles) {
      auto* cd = reinterpret_cast<ClosureData*>(handle.keys[0]);
      delete cd;
    }
  }
}

void TimerTrain::Impl::ExecuteStep() {
  grpc_core::MutexLock lock(&shutdown_mu_);
  if (shutdown_) return;
  grpc_core::Timestamp next = host_->Now();
  absl::optional<std::vector<EventEngine::Closure*>> check_result =
      timer_list_->TimerCheck(&next);
  CHECK(check_result.has_value())
      << "ERROR: More than one MainLoop is running.";
  if (!check_result->empty()) {
    RunSomeClosures(std::move(*check_result));
  }
  train_control_handle_ = event_engine_->RunAfter(
      period_, [self = shared_from_this()]() { self->ExecuteStep(); });
}

}  // namespace experimental
}  // namespace grpc_event_engine
