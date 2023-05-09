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

#include <stddef.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <queue>
#include <ratio>
#include <set>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/no_destruct.h"
#include "src/core/lib/gprpp/sync.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/util/port.h"

namespace grpc_event_engine {
namespace experimental {

// EventEngine implementation to be used by fuzzers.
// It's only allowed to have one FuzzingEventEngine instantiated at a time.
class FuzzingEventEngine : public EventEngine {
 public:
  struct Options {
    Duration max_delay_run_after = std::chrono::seconds(30);
  };
  explicit FuzzingEventEngine(Options options,
                              const fuzzing_event_engine::Actions& actions);
  ~FuzzingEventEngine() override { UnsetGlobalHooks(); }

  // Once the fuzzing work is completed, this method should be called to speed
  // quiescence.
  void FuzzingDone() ABSL_LOCKS_EXCLUDED(mu_);
  // Increment time once and perform any scheduled work.
  void Tick(Duration max_time = std::chrono::seconds(600))
      ABSL_LOCKS_EXCLUDED(mu_);
  // Repeatedly call Tick() until there is no more work to do.
  void TickUntilIdle() ABSL_LOCKS_EXCLUDED(mu_);

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

  // Clear any global hooks installed by this event engine. Call prior to
  // destruction to ensure no overlap between tests if constructing/destructing
  // each test.
  void UnsetGlobalHooks() ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // One pending task to be run.
  struct Task {
    Task(intptr_t id, absl::AnyInvocable<void()> closure)
        : id(id), closure(std::move(closure)) {}
    intptr_t id;
    absl::AnyInvocable<void()> closure;
  };

  // Per listener information.
  // We keep a shared_ptr to this, one reference held by the FuzzingListener
  // Listener implementation, and one reference in the event engine state, so it
  // may be iterated through and inspected - principally to discover the ports
  // on which this listener is listening.
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
    // The callback to invoke when a new connection is accepted.
    Listener::AcceptCallback on_accept;
    // The callback to invoke when the listener is shut down.
    absl::AnyInvocable<void(absl::Status)> on_shutdown;
    // The memory allocator factory to use for this listener.
    const std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory;
    // The ports on which this listener is listening.
    std::vector<int> ports ABSL_GUARDED_BY(mu_);
    // Has start been called on the listener?
    // Used to emulate the Bind/Start semantics demanded by the API.
    bool started ABSL_GUARDED_BY(mu_);
    // The status to return via on_shutdown.
    absl::Status shutdown_status ABSL_GUARDED_BY(mu_);
  };

  // Implementation of Listener.
  class FuzzingListener final : public Listener {
   public:
    explicit FuzzingListener(std::shared_ptr<ListenerInfo> info)
        : info_(std::move(info)) {}
    ~FuzzingListener() override;
    absl::StatusOr<int> Bind(const ResolvedAddress& addr) override;
    absl::Status Start() override;

   private:
    std::shared_ptr<ListenerInfo> info_;
  };

  // One read that's outstanding.
  struct PendingRead {
    // Callback to invoke when the read completes.
    absl::AnyInvocable<void(absl::Status)> on_read;
    // The buffer to read into.
    SliceBuffer* buffer;
  };

  // The join between two Endpoint instances.
  struct EndpointMiddle {
    EndpointMiddle(int listener_port, int client_port)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
    // Address of each side of the endpoint.
    const ResolvedAddress addrs[2];
    // Is the endpoint closed?
    bool closed ABSL_GUARDED_BY(mu_) = false;
    // Bytes written into each endpoint and awaiting a read.
    std::vector<uint8_t> pending[2] ABSL_GUARDED_BY(mu_);
    // The sizes of each accepted write, as determined by the fuzzer actions.
    std::queue<size_t> write_sizes[2] ABSL_GUARDED_BY(mu_);
    // The next read that's pending (or nullopt).
    absl::optional<PendingRead> pending_read[2] ABSL_GUARDED_BY(mu_);

    // Helper to take some bytes from data and queue them into pending[index].
    // Returns true if all bytes were consumed, false if more writes are needed.
    bool Write(SliceBuffer* data, int index) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  };

  // Implementation of Endpoint.
  // When a connection is formed, we create two of these - one with index 0, the
  // other index 1, both pointing to the same EndpointMiddle.
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
      return middle_->addrs[peer_index()];
    }
    const ResolvedAddress& GetLocalAddress() const override {
      return middle_->addrs[my_index()];
    }

   private:
    int my_index() const { return index_; }
    int peer_index() const { return 1 - index_; }
    // Schedule additional writes to be performed later.
    // Takes a ref to middle instead of holding this, so that should the
    // endpoint be destroyed we don't have to worry about use-after-free.
    // Instead that scheduled callback will see the middle is closed and finally
    // report completion to the caller.
    // Since there is no timeliness contract for the completion of writes after
    // endpoint shutdown, it's believed this is a legal implementation.
    static void ScheduleDelayedWrite(
        std::shared_ptr<EndpointMiddle> middle, int index,
        absl::AnyInvocable<void(absl::Status)> on_writable, SliceBuffer* data)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
    const std::shared_ptr<EndpointMiddle> middle_;
    const int index_;
  };

  void RunLocked(absl::AnyInvocable<void()> closure)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    RunAfterLocked(Duration::zero(), std::move(closure));
  }

  TaskHandle RunAfterLocked(Duration when, absl::AnyInvocable<void()> closure)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Allocate a port. Considered fuzzer selected port orderings first, and then
  // falls back to an exhaustive incremental search from port #1.
  int AllocatePort() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  // Is the given port in use by any listener?
  bool IsPortUsed(int port) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  // For the next connection being built, query the list of fuzzer selected
  // write size limits.
  std::queue<size_t> WriteSizesForConnection()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  gpr_timespec NowAsTimespec(gpr_clock_type clock_type)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(now_mu_);
  static gpr_timespec GlobalNowImpl(gpr_clock_type clock_type)
      ABSL_LOCKS_EXCLUDED(mu_);

  static grpc_core::NoDestruct<grpc_core::Mutex> mu_;
  static grpc_core::NoDestruct<grpc_core::Mutex> now_mu_
      ABSL_ACQUIRED_AFTER(mu_);

  Duration exponential_gate_time_increment_ ABSL_GUARDED_BY(mu_) =
      std::chrono::milliseconds(1);
  const Duration max_delay_run_after_;
  intptr_t next_task_id_ ABSL_GUARDED_BY(mu_);
  intptr_t current_tick_ ABSL_GUARDED_BY(now_mu_);
  Time now_ ABSL_GUARDED_BY(now_mu_);
  std::queue<Duration> task_delays_ ABSL_GUARDED_BY(mu_);
  std::map<intptr_t, std::shared_ptr<Task>> tasks_by_id_ ABSL_GUARDED_BY(mu_);
  std::multimap<Time, std::shared_ptr<Task>> tasks_by_time_
      ABSL_GUARDED_BY(mu_);
  std::set<std::shared_ptr<ListenerInfo>> listeners_ ABSL_GUARDED_BY(mu_);
  // Fuzzer selected port allocations.
  std::queue<int> free_ports_ ABSL_GUARDED_BY(mu_);
  // Next free port to allocate once fuzzer selections are exhausted.
  int next_free_port_ ABSL_GUARDED_BY(mu_) = 1;
  // Ports that were included in the fuzzer selected port orderings.
  std::set<int> fuzzer_mentioned_ports_ ABSL_GUARDED_BY(mu_);
  // Fuzzer selected write sizes for future connections - one picked off per
  // WriteSizesForConnection() call.
  std::queue<std::queue<size_t>> write_sizes_for_future_connections_
      ABSL_GUARDED_BY(mu_);
  grpc_pick_port_functions previous_pick_port_functions_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_TEST_CORE_EVENT_ENGINE_FUZZING_EVENT_ENGINE_FUZZING_EVENT_ENGINE_H
