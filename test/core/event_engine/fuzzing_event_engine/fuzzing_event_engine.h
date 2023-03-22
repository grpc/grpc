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

#ifndef GRPC_TEST_CORE_EVENT_ENGINE_FUZZING_EVENT_ENGINE_FUZZING_EVENT_ENGINE_H
#define GRPC_TEST_CORE_EVENT_ENGINE_FUZZING_EVENT_ENGINE_FUZZING_EVENT_ENGINE_H

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <queue>
#include <ratio>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/no_destruct.h"
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
  };
  explicit FuzzingEventEngine(Options options,
                              const fuzzing_event_engine::Actions& actions);
  ~FuzzingEventEngine() override { UnsetGlobalHooks(); }

  void FuzzingDone() ABSL_LOCKS_EXCLUDED(mu_);
  void Tick() ABSL_LOCKS_EXCLUDED(mu_);

  absl::StatusOr<std::unique_ptr<Listener>> CreateListener(
      Listener::AcceptCallback on_accept,
      absl::AnyInvocable<void(absl::Status)> on_shutdown,
      const EndpointConfig& config,
      std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory)
      ABSL_LOCKS_EXCLUDED(mu_) override;

  ConnectionHandle Connect(OnConnectCallback on_connect,
                           const ResolvedAddress& addr,
                           const EndpointConfig& args,
                           MemoryAllocator memory_allocator, Duration timeout)
      ABSL_LOCKS_EXCLUDED(mu_) override;

  bool CancelConnect(ConnectionHandle handle) ABSL_LOCKS_EXCLUDED(mu_) override;

  bool IsWorkerThread() override;

  std::unique_ptr<DNSResolver> GetDNSResolver(
      const DNSResolver::ResolverOptions& options) override;

  void Run(Closure* closure) ABSL_LOCKS_EXCLUDED(mu_) override;
  void Run(absl::AnyInvocable<void()> closure)
      ABSL_LOCKS_EXCLUDED(mu_) override;
  TaskHandle RunAfter(Duration when, Closure* closure)
      ABSL_LOCKS_EXCLUDED(mu_) override;
  TaskHandle RunAfter(Duration when, absl::AnyInvocable<void()> closure)
      ABSL_LOCKS_EXCLUDED(mu_) override;
  bool Cancel(TaskHandle handle) ABSL_LOCKS_EXCLUDED(mu_) override;

  using Time = std::chrono::time_point<FuzzingEventEngine, Duration>;

  Time Now() ABSL_LOCKS_EXCLUDED(mu_);

  void UnsetGlobalHooks() ABSL_LOCKS_EXCLUDED(mu_);

 private:
  struct Task {
    Task(intptr_t id, absl::AnyInvocable<void()> closure)
        : id(id), closure(std::move(closure)) {}
    intptr_t id;
    absl::AnyInvocable<void()> closure;
  };

  struct ListenerInfo {
    ListenerInfo(
        Listener::AcceptCallback on_accept,
        absl::AnyInvocable<void(absl::Status)> on_shutdown,
        std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory)
        : on_accept(std::move(on_accept)),
          on_shutdown(std::move(on_shutdown)),
          memory_allocator_factory(std::move(memory_allocator_factory)),
          started(false) {}
    ~ListenerInfo() ABSL_LOCKS_EXCLUDED(mu_);
    Listener::AcceptCallback on_accept;
    absl::AnyInvocable<void(absl::Status)> on_shutdown;
    const std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory;
    std::vector<int> ports ABSL_GUARDED_BY(mu_);
    bool started ABSL_GUARDED_BY(mu_);
    absl::Status shutdown_status ABSL_GUARDED_BY(mu_);
  };

  class FuzzingListener final : public Listener {
   public:
    explicit FuzzingListener(std::shared_ptr<ListenerInfo> info)
        : info_(std::move(info)) {}
    ~FuzzingListener() override;
    virtual absl::StatusOr<int> Bind(const ResolvedAddress& addr) override;
    virtual absl::Status Start() override;

   private:
    std::shared_ptr<ListenerInfo> info_;
  };

  struct PendingRead {
    absl::AnyInvocable<void(absl::Status)> on_read;
    SliceBuffer* buffer;
  };

  struct EndpointMiddle {
    EndpointMiddle(int listener_port, int client_port);
    const ResolvedAddress addrs[2];
    bool closed ABSL_GUARDED_BY(mu_) = false;
    std::vector<uint8_t> pending[2] ABSL_GUARDED_BY(mu_){
        std::vector<uint8_t>(), std::vector<uint8_t>()};
    std::queue<size_t> write_sizes[2] ABSL_GUARDED_BY(mu_);
    absl::optional<PendingRead> pending_read[2] ABSL_GUARDED_BY(mu_);

    bool Write(SliceBuffer* data, int index) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  };

  class FuzzingEndpoint final : public Endpoint {
   public:
    FuzzingEndpoint(std::shared_ptr<EndpointMiddle> middle, int index)
        : middle_(std::move(middle)), index_(index) {}
    ~FuzzingEndpoint() override;

    bool Read(absl::AnyInvocable<void(absl::Status)> on_read,
              SliceBuffer* buffer, const ReadArgs* args) override;
    bool Write(absl::AnyInvocable<void(absl::Status)> on_writable,
               SliceBuffer* data, const WriteArgs* args) override;
    const ResolvedAddress& GetPeerAddress() const override {
      return middle_->addrs[1 - index_];
    }
    const ResolvedAddress& GetLocalAddress() const override {
      return middle_->addrs[index_];
    }

   private:
    static void ScheduleDelayedWrite(
        std::shared_ptr<EndpointMiddle> middle, int index,
        absl::AnyInvocable<void(absl::Status)> on_writable, SliceBuffer* data)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
    std::shared_ptr<EndpointMiddle> middle_;
    int index_;
  };

  void RunLocked(absl::AnyInvocable<void()> closure)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    RunAfterLocked(Duration::zero(), std::move(closure));
  }

  TaskHandle RunAfterLocked(Duration when, absl::AnyInvocable<void()> closure)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  int AllocatePort() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  bool IsPortUsed(int port) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  gpr_timespec NowAsTimespec(gpr_clock_type clock_type)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  static gpr_timespec GlobalNowImpl(gpr_clock_type clock_type)
      ABSL_LOCKS_EXCLUDED(mu_);
  const Duration final_tick_length_;

  static grpc_core::NoDestruct<grpc_core::Mutex> mu_;

  intptr_t next_task_id_ ABSL_GUARDED_BY(mu_);
  intptr_t current_tick_ ABSL_GUARDED_BY(mu_);
  Time now_ ABSL_GUARDED_BY(mu_);
  std::map<intptr_t, Duration> tick_increments_ ABSL_GUARDED_BY(mu_);
  std::map<intptr_t, Duration> task_delays_ ABSL_GUARDED_BY(mu_);
  std::map<intptr_t, std::shared_ptr<Task>> tasks_by_id_ ABSL_GUARDED_BY(mu_);
  std::multimap<Time, std::shared_ptr<Task>> tasks_by_time_
      ABSL_GUARDED_BY(mu_);
  std::set<std::shared_ptr<ListenerInfo>> listeners_ ABSL_GUARDED_BY(mu_);
  std::queue<int> free_ports_ ABSL_GUARDED_BY(mu_);
  int next_free_port_ ABSL_GUARDED_BY(mu_) = 1;
  std::set<int> fuzzer_mentioned_ports_ ABSL_GUARDED_BY(mu_);
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_TEST_CORE_EVENT_ENGINE_FUZZING_EVENT_ENGINE_FUZZING_EVENT_ENGINE_H
