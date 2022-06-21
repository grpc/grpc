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

#ifndef GRPC_TEST_CORE_EVENT_ENGINE_FUZZING_EVENT_ENGINE_H
#define GRPC_TEST_CORE_EVENT_ENGINE_FUZZING_EVENT_ENGINE_H

#include <chrono>
#include <cstdint>
#include <map>

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/gprpp/sync.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"

namespace grpc_event_engine {
namespace experimental {

// EventEngine implementation to be used by fuzzers.
class FuzzingEventEngine : public EventEngine {
 public:
  struct Options {
    // After all scheduled tick lengths are completed, this is the amount of
    // time Now() will be incremented each tick.
    Duration final_tick_length = std::chrono::seconds(1);
    fuzzing_event_engine::Actions actions;
  };
  explicit FuzzingEventEngine(Options options);
  void Tick();

  absl::StatusOr<std::unique_ptr<Listener>> CreateListener(
      Listener::AcceptCallback on_accept,
      std::function<void(absl::Status)> on_shutdown,
      const EndpointConfig& config,
      std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory)
      override;

  ConnectionHandle Connect(OnConnectCallback on_connect,
                           const ResolvedAddress& addr,
                           const EndpointConfig& args,
                           MemoryAllocator memory_allocator,
                           Duration timeout) override;

  bool CancelConnect(ConnectionHandle handle) override;

  bool IsWorkerThread() override;

  std::unique_ptr<DNSResolver> GetDNSResolver(
      const DNSResolver::ResolverOptions& options) override;

  void Run(Closure* closure) override;
  void Run(std::function<void()> closure) override;
  TaskHandle RunAfter(Duration when, Closure* closure) override;
  TaskHandle RunAfter(Duration when, std::function<void()> closure) override;
  bool Cancel(TaskHandle handle) override;

  using Time = std::chrono::time_point<FuzzingEventEngine, Duration>;

  Time Now() ABSL_LOCKS_EXCLUDED(mu_);

 private:
  struct Task {
    Task(intptr_t id, std::function<void()> closure)
        : id(id), closure(std::move(closure)) {}
    intptr_t id;
    std::function<void()> closure;
  };

  const Duration final_tick_length_;

  grpc_core::Mutex mu_;

  intptr_t next_task_id_ ABSL_GUARDED_BY(mu_) = 1;
  intptr_t current_tick_ ABSL_GUARDED_BY(mu_) = 0;
  Time now_ ABSL_GUARDED_BY(mu_) = Time::min();
  std::map<intptr_t, Duration> tick_increments_ ABSL_GUARDED_BY(mu_);
  std::map<intptr_t, Duration> task_delays_ ABSL_GUARDED_BY(mu_);
  std::map<intptr_t, std::shared_ptr<Task>> tasks_by_id_ ABSL_GUARDED_BY(mu_);
  std::multimap<Time, std::shared_ptr<Task>> tasks_by_time_
      ABSL_GUARDED_BY(mu_);
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif
