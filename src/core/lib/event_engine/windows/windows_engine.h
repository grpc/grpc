// Copyright 2022 The gRPC Authors
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
#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_WINDOWS_WINDOWS_ENGINE_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_WINDOWS_WINDOWS_ENGINE_H
#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice_buffer.h>

#include "src/core/lib/event_engine/handle_containers.h"
#include "src/core/lib/event_engine/posix_engine/timer_manager.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/windows/iocp.h"
#include "src/core/lib/event_engine/windows/windows_endpoint.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/surface/init_internally.h"

namespace grpc_event_engine {
namespace experimental {

// TODO(ctiller): KeepsGrpcInitialized is an interim measure to ensure that
// EventEngine is shut down before we shut down iomgr.
class WindowsEventEngine : public EventEngine,
                           public grpc_core::KeepsGrpcInitialized {
 public:
  class WindowsDNSResolver : public EventEngine::DNSResolver {
   public:
    ~WindowsDNSResolver() override;
    LookupTaskHandle LookupHostname(LookupHostnameCallback on_resolve,
                                    absl::string_view name,
                                    absl::string_view default_port,
                                    Duration timeout) override;
    LookupTaskHandle LookupSRV(LookupSRVCallback on_resolve,
                               absl::string_view name,
                               Duration timeout) override;
    LookupTaskHandle LookupTXT(LookupTXTCallback on_resolve,
                               absl::string_view name,
                               Duration timeout) override;
    bool CancelLookup(LookupTaskHandle handle) override;
  };

  WindowsEventEngine();
  ~WindowsEventEngine() override;

  absl::StatusOr<std::unique_ptr<Listener>> CreateListener(
      Listener::AcceptCallback on_accept,
      absl::AnyInvocable<void(absl::Status)> on_shutdown,
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
  void Run(absl::AnyInvocable<void()> closure) override;
  TaskHandle RunAfter(Duration when, Closure* closure) override;
  TaskHandle RunAfter(Duration when,
                      absl::AnyInvocable<void()> closure) override;
  bool Cancel(TaskHandle handle) override;

  // Retrieve the base ThreadPool.
  // This is public because most classes that know the concrete
  // WindowsEventEngine type are effectively friends.
  // Not intended for external use.
  ThreadPool* thread_pool() { return thread_pool_.get(); }
  IOCP* poller() { return &iocp_; }

 private:
  // State of an active connection.
  // Managed by a shared_ptr, owned exclusively by the timeout callback and the
  // OnConnectCompleted callback herein.
  struct ConnectionState {
    // everything is guarded by mu;
    grpc_core::Mutex mu
        ABSL_ACQUIRED_BEFORE(WindowsEventEngine::connection_mu_);
    EventEngine::ConnectionHandle connection_handle ABSL_GUARDED_BY(mu);
    EventEngine::TaskHandle timer_handle ABSL_GUARDED_BY(mu);
    EventEngine::OnConnectCallback on_connected_user_callback
        ABSL_GUARDED_BY(mu);
    EventEngine::Closure* on_connected ABSL_GUARDED_BY(mu);
    std::unique_ptr<WinSocket> socket ABSL_GUARDED_BY(mu);
    EventEngine::ResolvedAddress address ABSL_GUARDED_BY(mu);
    MemoryAllocator allocator ABSL_GUARDED_BY(mu);
  };

  // A poll worker which schedules itself unless kicked
  class IOCPWorkClosure : public EventEngine::Closure {
   public:
    explicit IOCPWorkClosure(ThreadPool* thread_pool, IOCP* iocp);
    void Run() override;
    void WaitForShutdown();

   private:
    std::atomic<int> workers_{1};
    grpc_core::Notification done_signal_;
    ThreadPool* thread_pool_;
    IOCP* iocp_;
  };

  void OnConnectCompleted(std::shared_ptr<ConnectionState> state);

  // CancelConnect called from within the deadline timer.
  // In this case, the connection_state->mu is already locked, and timer
  // cancellation is not possible.
  bool CancelConnectFromDeadlineTimer(ConnectionState* connection_state)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(connection_state->mu);

  // Completes the connection cancellation logic after checking handle validity
  // and optionally cancelling deadline timers.
  bool CancelConnectInternalStateLocked(ConnectionState* connection_state)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(connection_state->mu);

  struct TimerClosure;
  EventEngine::TaskHandle RunAfterInternal(Duration when,
                                           absl::AnyInvocable<void()> cb);
  grpc_core::Mutex task_mu_;
  TaskHandleSet known_handles_ ABSL_GUARDED_BY(task_mu_);
  grpc_core::Mutex connection_mu_ ABSL_ACQUIRED_AFTER(ConnectionState::mu);
  grpc_core::CondVar connection_cv_;
  ConnectionHandleSet known_connection_handles_ ABSL_GUARDED_BY(connection_mu_);
  std::atomic<intptr_t> aba_token_{0};

  std::shared_ptr<ThreadPool> thread_pool_;
  IOCP iocp_;
  TimerManager timer_manager_;
  IOCPWorkClosure iocp_worker_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_WINDOWS_WINDOWS_ENGINE_H
