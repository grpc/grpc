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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENGINE_H
#define GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENGINE_H
#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <atomic>
#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>

#include "src/core/lib/event_engine/handle_containers.h"
#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/timer_manager.h"
#include "src/core/lib/event_engine/thread_pool.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/surface/init_internally.h"

namespace grpc_event_engine {
namespace experimental {

#ifdef GRPC_POSIX_SOCKET_TCP
// A helper class to manager lifetime of the poller associated with the
// posix event engine.
class PosixEnginePollerManager
    : public grpc_event_engine::posix_engine::Scheduler {
 public:
  explicit PosixEnginePollerManager(std::shared_ptr<ThreadPool> executor);
  explicit PosixEnginePollerManager(
      grpc_event_engine::posix_engine::PosixEventPoller* poller);
  grpc_event_engine::posix_engine::PosixEventPoller* Poller() {
    return poller_;
  }

  ThreadPool* Executor() { return executor_.get(); }

  void Run(experimental::EventEngine::Closure* closure) override;
  void Run(absl::AnyInvocable<void()>) override;

  bool IsShuttingDown() {
    return poller_state_.load(std::memory_order_acquire) ==
           PollerState::kShuttingDown;
  }
  void TriggerShutdown();

  ~PosixEnginePollerManager() override;

 private:
  enum class PollerState { kExternal, kOk, kShuttingDown };
  grpc_event_engine::posix_engine::PosixEventPoller* poller_ = nullptr;
  std::atomic<PollerState> poller_state_{PollerState::kOk};
  std::shared_ptr<ThreadPool> executor_;
};
#endif  // GRPC_POSIX_SOCKET_TCP

// An iomgr-based Posix EventEngine implementation.
// All methods require an ExecCtx to already exist on the thread's stack.
// TODO(ctiller): KeepsGrpcInitialized is an interim measure to ensure that
// event engine is shut down before we shut down iomgr.
class PosixEventEngine final : public EventEngine,
                               public grpc_core::KeepsGrpcInitialized {
 public:
  class PosixDNSResolver : public EventEngine::DNSResolver {
   public:
    ~PosixDNSResolver() override;
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

#ifdef GRPC_POSIX_SOCKET_TCP
  // Constructs an event engine which does not own the poller. Do not call this
  // constructor directly. Instead use the MakeTestOnlyPosixEventEngine static
  // method. Its expected to be used only in tests.
  explicit PosixEventEngine(
      grpc_event_engine::posix_engine::PosixEventPoller* poller);
  PosixEventEngine();
#else   // GRPC_POSIX_SOCKET_TCP
  PosixEventEngine() = default;
#endif  // GRPC_POSIX_SOCKET_TCP

  ~PosixEventEngine() override;

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

#ifdef GRPC_POSIX_SOCKET_TCP
  // The posix event engine returned by this method would not own the poller
  // and would not be in-charge of driving the poller by calling its Work(..)
  // method. Instead its upto the test to drive the poller. The returned posix
  // event engine will also not attempt to shutdown the poller since it does not
  // own it.
  static std::shared_ptr<PosixEventEngine> MakeTestOnlyPosixEventEngine(
      grpc_event_engine::posix_engine::PosixEventPoller* test_only_poller) {
    return std::make_shared<PosixEventEngine>(test_only_poller);
  }
#endif  // GRPC_POSIX_SOCKET_TCP

 private:
  struct ClosureData;
  EventEngine::TaskHandle RunAfterInternal(Duration when,
                                           absl::AnyInvocable<void()> cb);

#ifdef GRPC_POSIX_SOCKET_TCP
  static void PollerWorkInternal(
      std::shared_ptr<PosixEnginePollerManager> poller_manager);
#endif  // GRPC_POSIX_SOCKET_TCP

  grpc_core::Mutex mu_;
  TaskHandleSet known_handles_ ABSL_GUARDED_BY(mu_);
  std::atomic<intptr_t> aba_token_{0};
  posix_engine::TimerManager timer_manager_;
  std::shared_ptr<ThreadPool> executor_;
#ifdef GRPC_POSIX_SOCKET_TCP
  std::shared_ptr<PosixEnginePollerManager> poller_manager_;
#endif  // GRPC_POSIX_SOCKET_TCP
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENGINE_H
