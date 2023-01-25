// Copyright 2022 gRPC authors.
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

#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"

#include <stdlib.h>

#include <algorithm>
#include <chrono>
#include <ratio>
#include <vector>

#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/time.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"

extern gpr_timespec (*gpr_now_impl)(gpr_clock_type clock_type);

namespace grpc_event_engine {
namespace experimental {

namespace {
const intptr_t kTaskHandleSalt = 12345;
FuzzingEventEngine* g_fuzzing_event_engine = nullptr;
gpr_timespec (*g_orig_gpr_now_impl)(gpr_clock_type clock_type);
}  // namespace

FuzzingEventEngine::FuzzingEventEngine(
    Options options, const fuzzing_event_engine::Actions& actions)
    : final_tick_length_(options.final_tick_length) {
  tick_increments_.clear();
  task_delays_.clear();
  tasks_by_id_.clear();
  tasks_by_time_.clear();
  next_task_id_ = 1;
  current_tick_ = 0;
  // Start at 5 seconds after the epoch.
  // This needs to be more than 1, and otherwise is kind of arbitrary.
  // The grpc_core::Timer code special cases the zero second time period after
  // epoch to allow for some fancy atomic stuff.
  now_ = Time() + std::chrono::seconds(5);

  // Whilst a fuzzing EventEngine is active we override grpc's now function.
  grpc_core::TestOnlySetProcessEpoch(NowAsTimespec(GPR_CLOCK_MONOTONIC));

  auto update_delay = [](std::map<intptr_t, Duration>* map,
                         const fuzzing_event_engine::Delay& delay,
                         Duration max) {
    auto& value = (*map)[delay.id()];
    if (delay.delay_us() > static_cast<uint64_t>(max.count() / GPR_NS_PER_US)) {
      value = max;
      return;
    }
    Duration add = std::chrono::microseconds(delay.delay_us());
    if (add >= max - value) {
      value = max;
    } else {
      value += add;
    }
  };

  for (const auto& delay : actions.tick_lengths()) {
    update_delay(&tick_increments_, delay, std::chrono::hours(24));
  }
  for (const auto& delay : actions.run_delay()) {
    update_delay(&task_delays_, delay, std::chrono::seconds(30));
  }
}

void FuzzingEventEngine::FuzzingDone() {
  grpc_core::MutexLock lock(&mu_);
  tick_increments_.clear();
}

gpr_timespec FuzzingEventEngine::NowAsTimespec(gpr_clock_type clock_type) {
  // TODO(ctiller): add a facility to track realtime and monotonic clocks
  // separately to simulate divergence.
  GPR_ASSERT(clock_type != GPR_TIMESPAN);
  const Duration d = now_.time_since_epoch();
  auto secs = std::chrono::duration_cast<std::chrono::seconds>(d);
  return {secs.count(), static_cast<int32_t>((d - secs).count()), clock_type};
}

void FuzzingEventEngine::Tick() {
  std::vector<absl::AnyInvocable<void()>> to_run;
  {
    grpc_core::MutexLock lock(&mu_);
    // Increment time
    auto tick_it = tick_increments_.find(current_tick_);
    if (tick_it != tick_increments_.end()) {
      now_ += tick_it->second;
      GPR_ASSERT(now_.time_since_epoch().count() >= 0);
      tick_increments_.erase(tick_it);
    } else if (tick_increments_.empty()) {
      now_ += final_tick_length_;
      GPR_ASSERT(now_.time_since_epoch().count() >= 0);
    }
    ++current_tick_;
    // Find newly expired timers.
    while (!tasks_by_time_.empty() && tasks_by_time_.begin()->first <= now_) {
      auto& task = *tasks_by_time_.begin()->second;
      tasks_by_id_.erase(task.id);
      if (task.closure != nullptr) {
        to_run.push_back(std::move(task.closure));
      }
      tasks_by_time_.erase(tasks_by_time_.begin());
    }
  }
  for (auto& closure : to_run) {
    closure();
  }
}

FuzzingEventEngine::Time FuzzingEventEngine::Now() {
  grpc_core::MutexLock lock(&mu_);
  return now_;
}

absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
FuzzingEventEngine::CreateListener(Listener::AcceptCallback,
                                   absl::AnyInvocable<void(absl::Status)>,
                                   const EndpointConfig&,
                                   std::unique_ptr<MemoryAllocatorFactory>) {
  abort();
}

EventEngine::ConnectionHandle FuzzingEventEngine::Connect(
    OnConnectCallback, const ResolvedAddress&, const EndpointConfig&,
    MemoryAllocator, Duration) {
  abort();
}

bool FuzzingEventEngine::CancelConnect(ConnectionHandle) { abort(); }

bool FuzzingEventEngine::IsWorkerThread() { abort(); }

std::unique_ptr<EventEngine::DNSResolver> FuzzingEventEngine::GetDNSResolver(
    const DNSResolver::ResolverOptions&) {
  abort();
}

void FuzzingEventEngine::Run(Closure* closure) {
  RunAfter(Duration::zero(), closure);
}

void FuzzingEventEngine::Run(absl::AnyInvocable<void()> closure) {
  RunAfter(Duration::zero(), std::move(closure));
}

EventEngine::TaskHandle FuzzingEventEngine::RunAfter(Duration when,
                                                     Closure* closure) {
  return RunAfter(when, [closure]() { closure->Run(); });
}

EventEngine::TaskHandle FuzzingEventEngine::RunAfter(
    Duration when, absl::AnyInvocable<void()> closure) {
  grpc_core::MutexLock lock(&mu_);
  const intptr_t id = next_task_id_;
  ++next_task_id_;
  const auto delay_it = task_delays_.find(id);
  // Under fuzzer configuration control, maybe make the task run later.
  if (delay_it != task_delays_.end()) {
    when += delay_it->second;
    task_delays_.erase(delay_it);
  }
  auto task = std::make_shared<Task>(id, std::move(closure));
  tasks_by_id_.emplace(id, task);
  tasks_by_time_.emplace(now_ + when, std::move(task));
  return TaskHandle{id, kTaskHandleSalt};
}

bool FuzzingEventEngine::Cancel(TaskHandle handle) {
  grpc_core::MutexLock lock(&mu_);
  GPR_ASSERT(handle.keys[1] == kTaskHandleSalt);
  const intptr_t id = handle.keys[0];
  auto it = tasks_by_id_.find(id);
  if (it == tasks_by_id_.end()) {
    return false;
  }
  if (it->second->closure == nullptr) {
    return false;
  }
  it->second->closure = nullptr;
  return true;
}

gpr_timespec FuzzingEventEngine::GlobalNowImpl(gpr_clock_type clock_type) {
  if (g_fuzzing_event_engine == nullptr) {
    return gpr_inf_future(clock_type);
  }
  GPR_ASSERT(g_fuzzing_event_engine != nullptr);
  grpc_core::MutexLock lock(&g_fuzzing_event_engine->mu_);
  return g_fuzzing_event_engine->NowAsTimespec(clock_type);
}

void FuzzingEventEngine::SetGlobalNowImplEngine(FuzzingEventEngine* engine) {
  GPR_ASSERT(g_fuzzing_event_engine == nullptr);
  g_fuzzing_event_engine = engine;
  g_orig_gpr_now_impl = gpr_now_impl;
  gpr_now_impl = GlobalNowImpl;
}

void FuzzingEventEngine::UnsetGlobalNowImplEngine(FuzzingEventEngine* engine) {
  GPR_ASSERT(g_fuzzing_event_engine == engine);
  g_fuzzing_event_engine = nullptr;
  gpr_now_impl = g_orig_gpr_now_impl;
  g_orig_gpr_now_impl = nullptr;
}

}  // namespace experimental
}  // namespace grpc_event_engine
