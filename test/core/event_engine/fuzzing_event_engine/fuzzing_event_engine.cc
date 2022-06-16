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

namespace grpc_event_engine {
namespace experimental {

namespace {
const intptr_t kTaskHandleSalt = 12345;
}

FuzzingEventEngine::FuzzingEventEngine(Options options)
    : final_tick_length_(options.final_tick_length) {
  for (const auto& delay : options.actions.tick_lengths()) {
    tick_increments_[delay.id()] += absl::Microseconds(delay.delay_us());
  }
  for (const auto& delay : options.actions.run_delay()) {
    task_delays_[delay.id()] += absl::Microseconds(delay.delay_us());
  }
}

void FuzzingEventEngine::Tick() {
  std::vector<std::function<void()>> to_run;
  {
    grpc_core::MutexLock lock(&mu_);
    // Increment time
    auto tick_it = tick_increments_.find(current_tick_);
    if (tick_it != tick_increments_.end()) {
      now_ += tick_it->second;
      tick_increments_.erase(tick_it);
    } else if (tick_increments_.empty()) {
      now_ += final_tick_length_;
    }
    ++current_tick_;
    // Find newly expired timers.
    while (!tasks_by_time_.empty() && tasks_by_time_.begin()->first <= now_) {
      tasks_by_id_.erase(tasks_by_time_.begin()->second->id);
      to_run.push_back(std::move(tasks_by_time_.begin()->second->closure));
      tasks_by_time_.erase(tasks_by_time_.begin());
    }
  }
  for (auto& closure : to_run) {
    closure();
  }
}

absl::Time FuzzingEventEngine::Now() {
  grpc_core::MutexLock lock(&mu_);
  return now_;
}

absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
FuzzingEventEngine::CreateListener(Listener::AcceptCallback,
                                   std::function<void(absl::Status)>,
                                   const EndpointConfig&,
                                   std::unique_ptr<MemoryAllocatorFactory>) {
  abort();
}

EventEngine::ConnectionHandle FuzzingEventEngine::Connect(
    OnConnectCallback, const ResolvedAddress&, const EndpointConfig&,
    MemoryAllocator, absl::Time) {
  abort();
}

bool FuzzingEventEngine::CancelConnect(ConnectionHandle) { abort(); }

bool FuzzingEventEngine::IsWorkerThread() { abort(); }

std::unique_ptr<EventEngine::DNSResolver> FuzzingEventEngine::GetDNSResolver(
    const DNSResolver::ResolverOptions&) {
  abort();
}

void FuzzingEventEngine::Run(Closure* closure) { RunAt(Now(), closure); }

void FuzzingEventEngine::Run(std::function<void()> closure) {
  RunAt(Now(), closure);
}

EventEngine::TaskHandle FuzzingEventEngine::RunAt(absl::Time when,
                                                  Closure* closure) {
  return RunAt(when, [closure]() { closure->Run(); });
}

EventEngine::TaskHandle FuzzingEventEngine::RunAt(
    absl::Time when, std::function<void()> closure) {
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
  tasks_by_time_.emplace(when, std::move(task));
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
  if (it->second == nullptr) {
    return false;
  }
  it->second = nullptr;
  return true;
}

}  // namespace experimental
}  // namespace grpc_event_engine
