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
#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENGINE_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENGINE_H
#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/support/port_platform.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/core/lib/event_engine/ares_resolver.h"
#include "src/core/lib/event_engine/handle_containers.h"
#include "src/core/lib/event_engine/posix.h"
#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/timer_manager.h"
#include "src/core/lib/event_engine/ref_counted_dns_resolver_interface.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/sync.h"

#ifdef GRPC_POSIX_SOCKET_TCP
#include "src/core/lib/event_engine/posix_engine/posix_engine_closure.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#endif  // GRPC_POSIX_SOCKET_TCP

namespace grpc_event_engine::experimental {

#ifdef GRPC_POSIX_SOCKET_TCP
// A helper class to handle asynchronous connect operations.
class AsyncConnect {
 public:
  AsyncConnect(EventEngine::OnConnectCallback on_connect,
               std::shared_ptr<EventEngine> engine, ThreadPool* executor,
               grpc_event_engine::experimental::EventHandle* fd,
               MemoryAllocator&& allocator,
               const grpc_event_engine::experimental::PosixTcpOptions& options,
               std::string resolved_addr_str, int64_t connection_handle)
      : on_connect_(std::move(on_connect)),
        engine_(engine),
        executor_(executor),
        fd_(fd),
        allocator_(std::move(allocator)),
        options_(options),
        resolved_addr_str_(resolved_addr_str),
        connection_handle_(connection_handle),
        connect_cancelled_(false) {}

  void Start(EventEngine::Duration timeout);
  ~AsyncConnect();

 private:
  friend class PosixEventEngine;
  void OnTimeoutExpired(absl::Status status);

  void OnWritable(absl::Status status) ABSL_NO_THREAD_SAFETY_ANALYSIS;

  grpc_core::Mutex mu_;
  grpc_event_engine::experimental::PosixEngineClosure* on_writable_ = nullptr;
  EventEngine::OnConnectCallback on_connect_;
  std::shared_ptr<EventEngine> engine_;
  ThreadPool* executor_;
  EventEngine::TaskHandle alarm_handle_;
  int refs_{2};
  grpc_event_engine::experimental::EventHandle* fd_;
  MemoryAllocator allocator_;
  grpc_event_engine::experimental::PosixTcpOptions options_;
  std::string resolved_addr_str_;
  int64_t connection_handle_;
  bool connect_cancelled_;
};

// A helper class to manager lifetime of the poller associated with the
// posix EventEngine.
class PosixEnginePollerManager
    : public grpc_event_engine::experimental::Scheduler {
 public:
  explicit PosixEnginePollerManager(std::shared_ptr<ThreadPool> executor);
  explicit PosixEnginePollerManager(
      std::shared_ptr<grpc_event_engine::experimental::PosixEventPoller>
          poller);
  grpc_event_engine::experimental::PosixEventPoller* Poller() const {
    return poller_.get();
  }

  ThreadPool* Executor() { return executor_.get(); }

  void Run(experimental::EventEngine::Closure* closure) override;
  void Run(absl::AnyInvocable<void()>) override;

  bool IsShuttingDown() {
    return poller_state_.load(std::memory_order_acquire) ==
           PollerState::kShuttingDown;
  }
  void TriggerShutdown();

 private:
  enum class PollerState { kExternal, kOk, kShuttingDown };
  std::shared_ptr<grpc_event_engine::experimental::PosixEventPoller> poller_;
  std::atomic<PollerState> poller_state_{PollerState::kOk};
  std::shared_ptr<ThreadPool> executor_;
  bool trigger_shutdown_called_;
};

#endif  // GRPC_POSIX_SOCKET_TCP

// An iomgr-based Posix EventEngine implementation.
// All methods require an ExecCtx to already exist on the thread's stack.
class PosixEventEngine final : public PosixEventEngineWithFdSupport {
 public:
  class PosixDNSResolver : public EventEngine::DNSResolver {
   public:
    explicit PosixDNSResolver(
        grpc_core::OrphanablePtr<RefCountedDNSResolverInterface> dns_resolver);
    void LookupHostname(LookupHostnameCallback on_resolve,
                        absl::string_view name,
                        absl::string_view default_port) override;
    void LookupSRV(LookupSRVCallback on_resolve,
                   absl::string_view name) override;
    void LookupTXT(LookupTXTCallback on_resolve,
                   absl::string_view name) override;

   private:
    grpc_core::OrphanablePtr<RefCountedDNSResolverInterface> dns_resolver_;
  };

  static std::shared_ptr<PosixEventEngine> MakePosixEventEngine();

  ~PosixEventEngine() override;

  std::unique_ptr<EventEngine::Endpoint> CreatePosixEndpointFromFd(
      int fd, const EndpointConfig& config,
      MemoryAllocator memory_allocator) override;
  std::unique_ptr<EventEngine::Endpoint> CreateEndpointFromFd(
      int fd, const EndpointConfig& config) override;

  ConnectionHandle CreateEndpointFromUnconnectedFd(
      int fd, EventEngine::OnConnectCallback on_connect,
      const EventEngine::ResolvedAddress& addr, const EndpointConfig& config,
      MemoryAllocator memory_allocator, EventEngine::Duration timeout) override;

  absl::StatusOr<std::unique_ptr<Listener>> CreateListener(
      Listener::AcceptCallback on_accept,
      absl::AnyInvocable<void(absl::Status)> on_shutdown,
      const EndpointConfig& config,
      std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory)
      override;

  absl::StatusOr<std::unique_ptr<EventEngine::Listener>> CreatePosixListener(
      PosixEventEngineWithFdSupport::PosixAcceptCallback on_accept,
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
  absl::StatusOr<std::unique_ptr<DNSResolver>> GetDNSResolver(
      GRPC_UNUSED const DNSResolver::ResolverOptions& options) override;
  void Run(Closure* closure) override;
  void Run(absl::AnyInvocable<void()> closure) override;
  // Caution!! The timer implementation cannot create any fds. See #20418.
  TaskHandle RunAfter(Duration when, Closure* closure) override;
  TaskHandle RunAfter(Duration when,
                      absl::AnyInvocable<void()> closure) override;
  bool Cancel(TaskHandle handle) override;

#ifdef GRPC_POSIX_SOCKET_TCP

#ifdef GRPC_ENABLE_FORK_SUPPORT
  enum class OnForkRole { kChild, kParent };
  void AfterFork(OnForkRole on_fork_role);
  void BeforeFork();
#endif  // GRPC_ENABLE_FORK_SUPPORT

  // The posix EventEngine returned by this method would have a shared ownership
  // of the poller and would not be in-charge of driving the poller by calling
  // its Work(..) method. Instead its upto the test to drive the poller. The
  // returned posix EventEngine will also not attempt to shutdown the poller
  // since it does not own it.
  static std::shared_ptr<PosixEventEngine> MakeTestOnlyPosixEventEngine(
      std::shared_ptr<grpc_event_engine::experimental::PosixEventPoller>
          test_only_poller);
#endif  // GRPC_POSIX_SOCKET_TCP

 private:
  friend class AresResolverTest;
  struct ClosureData;

  PosixEventEngine();

#ifdef GRPC_POSIX_SOCKET_TCP
  // Constructs an EventEngine which has a shared ownership of the poller. Use
  // the MakeTestOnlyPosixEventEngine static method to call this. Its expected
  // to be used only in tests.
  explicit PosixEventEngine(
      std::shared_ptr<grpc_event_engine::experimental::PosixEventPoller>
          poller);
#endif  // GRPC_POSIX_SOCKET_TCP

  EventEngine::TaskHandle RunAfterInternal(Duration when,
                                           absl::AnyInvocable<void()> cb);

#ifdef GRPC_POSIX_SOCKET_TCP
  friend class AsyncConnect;
  struct ConnectionShard {
    grpc_core::Mutex mu;
    absl::flat_hash_map<int64_t, AsyncConnect*> pending_connections
        ABSL_GUARDED_BY(&mu);
  };

  ConnectionHandle CreateEndpointFromUnconnectedFdInternal(
      const FileDescriptor& fd, EventEngine::OnConnectCallback on_connect,
      const EventEngine::ResolvedAddress& addr, const PosixTcpOptions& options,
      MemoryAllocator memory_allocator, EventEngine::Duration timeout);

  void OnConnectFinishInternal(int connection_handle);

  std::vector<ConnectionShard> connection_shards_;
  std::atomic<int64_t> last_connection_id_{1};

#endif  // GRPC_POSIX_SOCKET_TCP

#if GRPC_ENABLE_FORK_SUPPORT
  void AfterForkInChild();
#endif

  grpc_core::Mutex mu_;
  TaskHandleSet known_handles_ ABSL_GUARDED_BY(mu_);
  std::atomic<intptr_t> aba_token_{0};
#if GRPC_ENABLE_FORK_SUPPORT && GRPC_ARES == 1 && \
    defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)
  // A separate mutex to avoid deadlocks.
  grpc_core::Mutex resolver_handles_mu_;
  absl::InlinedVector<std::weak_ptr<AresResolver::ReinitHandle>, 16>
      resolver_handles_ ABSL_GUARDED_BY(resolver_handles_mu_);
#endif  // defined(GRPC_ENABLE_FORK_SUPPORT) && GRPC_ARES == 1 &&
        // defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)
  std::shared_ptr<ThreadPool> executor_;

#if defined(GRPC_POSIX_SOCKET_TCP) && \
    !defined(GRPC_DO_NOT_INSTANTIATE_POSIX_POLLER)

  // RAII wrapper for a polling cycle. Starts a new one in ctor and stops
  // in dtor.
  class PollingCycle {
   public:
    explicit PollingCycle(PosixEnginePollerManager* poller_manager);
    ~PollingCycle();

   private:
    void PollerWorkInternal();

    PosixEnginePollerManager* poller_manager_;
    grpc_core::Mutex mu_;
    std::atomic_bool done_{false};
    int is_scheduled_ ABSL_GUARDED_BY(&mu_) = 0;
    grpc_core::CondVar cond_;
  };

  void SchedulePoller();
  void ResetPollCycle();

  PosixEnginePollerManager poller_manager_;

  // Ensures there's ever only one of these.
  std::optional<PollingCycle> polling_cycle_ ABSL_GUARDED_BY(&mu_);

#endif  // defined(GRPC_POSIX_SOCKET_TCP) &&
        // !defined(GRPC_DO_NOT_INSTANTIATE_POSIX_POLLER)

  std::shared_ptr<TimerManager> timer_manager_;
};

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENGINE_H
