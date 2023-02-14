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
#include <grpc/support/port_platform.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <ares.h>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>

#include "src/core/lib/event_engine/handle_containers.h"
#include "src/core/lib/event_engine/posix.h"
#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/timer_manager.h"
#include "src/core/lib/event_engine/thread_pool.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/surface/init_internally.h"

#ifdef GRPC_POSIX_SOCKET_TCP
#include "src/core/lib/event_engine/posix_engine/posix_engine_closure.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#endif  // GRPC_POSIX_SOCKET_TCP

namespace grpc_event_engine {
namespace experimental {

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
      grpc_event_engine::experimental::PosixEventPoller* poller);
  grpc_event_engine::experimental::PosixEventPoller* Poller() {
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
  grpc_event_engine::experimental::PosixEventPoller* poller_ = nullptr;
  std::atomic<PollerState> poller_state_{PollerState::kOk};
  std::shared_ptr<ThreadPool> executor_;
};
#endif  // GRPC_POSIX_SOCKET_TCP

// An iomgr-based Posix EventEngine implementation.
// All methods require an ExecCtx to already exist on the thread's stack.
// TODO(ctiller): KeepsGrpcInitialized is an interim measure to ensure that
// EventEngine is shut down before we shut down iomgr.
class PosixEventEngine final : public PosixEventEngineWithFdSupport,
                               public grpc_core::KeepsGrpcInitialized {
 public:
  class PosixDNSResolver : public EventEngine::DNSResolver {
   public:
    explicit PosixDNSResolver(
        ResolverOptions const& options,
        std::shared_ptr<PosixEnginePollerManager> poller_manager);
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

    class GrpcAresRequest;
    class GrpcAresHostnameRequest;

   private:
    // TODO(yijiem): see if we can use std::list
    // per ares-channel linked-list of FdNodes
    class FdNodeList {
     public:
      class FdNode {
       public:
        FdNode() = default;
        explicit FdNode(ares_socket_t as, EventHandle* ev_handle)
            : as_(as), ev_handle_(ev_handle) {}

        bool readable_registered() const { return readable_registered_; }
        bool writable_registered() const { return writable_registered_; }
        void set_readable_registered(bool rr) { readable_registered_ = rr; }
        void set_writable_registered(bool wr) { writable_registered_ = wr; }

        int WrappedFd() const { return static_cast<int>(as_); }
        EventHandle* event_handle() const { return ev_handle_; }

       private:
        friend class FdNodeList;

        // ares socket
        ares_socket_t as_;
        // Poller event handle
        EventHandle* ev_handle_;
        // next fd node
        FdNode* next_ = nullptr;
        /// if the readable closure has been registered
        bool readable_registered_ = false;
        /// if the writable closure has been registered
        bool writable_registered_ = false;
      };

      FdNodeList() = default;

      bool IsEmpty() const { return head_ == nullptr; }

      void PushFdNode(FdNode* fd_node) {
        fd_node->next_ = head_;
        head_ = fd_node;
      }

      FdNode* PopFdNode() {
        GPR_ASSERT(!IsEmpty());
        FdNode* ret = head_;
        head_ = head_->next_;
        return ret;
      }

      // Search for as in the FdNode list. This is an O(n) search, the max
      // possible value of n is ARES_GETSOCK_MAXNUM (16). n is typically 1 - 2
      // in our tests.
      FdNode* PopFdNode(ares_socket_t as) {
        FdNode phony_head;
        phony_head.next_ = head_;
        FdNode* node = &phony_head;
        while (node->next_ != nullptr) {
          if (node->next_->as_ == as) {
            FdNode* ret = node->next_;
            node->next_ = node->next_->next_;
            head_ = phony_head.next_;
            return ret;
          }
          node = node->next_;
        }
        return nullptr;
      }

     private:
      FdNode* head_ = nullptr;
    };

    void OnEvent(GrpcAresRequest* request);
    void OnReadable(FdNodeList::FdNode* fd_node, GrpcAresRequest* request,
                    absl::Status status);
    void OnWritable(FdNodeList::FdNode* fd_node, GrpcAresRequest* request,
                    absl::Status status);
    void OnDone(FdNodeList::FdNode* fd_node, GrpcAresRequest* request,
                absl::Status status);

    const ResolverOptions options_;
    std::shared_ptr<PosixEnginePollerManager> poller_manager_;
  };

#ifdef GRPC_POSIX_SOCKET_TCP
  // Constructs an EventEngine which does not own the poller. Do not call this
  // constructor directly. Instead use the MakeTestOnlyPosixEventEngine static
  // method. Its expected to be used only in tests.
  explicit PosixEventEngine(
      grpc_event_engine::experimental::PosixEventPoller* poller);
  PosixEventEngine();
#else   // GRPC_POSIX_SOCKET_TCP
  PosixEventEngine();
#endif  // GRPC_POSIX_SOCKET_TCP

  ~PosixEventEngine() override;

  std::unique_ptr<PosixEndpointWithFdSupport> CreatePosixEndpointFromFd(
      int fd, const EndpointConfig& config,
      MemoryAllocator memory_allocator) override;

  absl::StatusOr<std::unique_ptr<Listener>> CreateListener(
      Listener::AcceptCallback on_accept,
      absl::AnyInvocable<void(absl::Status)> on_shutdown,
      const EndpointConfig& config,
      std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory)
      override;

  absl::StatusOr<std::unique_ptr<PosixListenerWithFdSupport>>
  CreatePosixListener(
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
  std::unique_ptr<DNSResolver> GetDNSResolver(
      const DNSResolver::ResolverOptions& options) override;
  void Run(Closure* closure) override;
  void Run(absl::AnyInvocable<void()> closure) override;
  TaskHandle RunAfter(Duration when, Closure* closure) override;
  TaskHandle RunAfter(Duration when,
                      absl::AnyInvocable<void()> closure) override;
  bool Cancel(TaskHandle handle) override;

#ifdef GRPC_POSIX_SOCKET_TCP
  // The posix EventEngine returned by this method would not own the poller
  // and would not be in-charge of driving the poller by calling its Work(..)
  // method. Instead its upto the test to drive the poller. The returned posix
  // EventEngine will also not attempt to shutdown the poller since it does not
  // own it.
  static std::shared_ptr<PosixEventEngine> MakeTestOnlyPosixEventEngine(
      grpc_event_engine::experimental::PosixEventPoller* test_only_poller) {
    return std::make_shared<PosixEventEngine>(test_only_poller);
  }
#endif  // GRPC_POSIX_SOCKET_TCP

 private:
  struct ClosureData;
  EventEngine::TaskHandle RunAfterInternal(Duration when,
                                           absl::AnyInvocable<void()> cb);

#ifdef GRPC_POSIX_SOCKET_TCP
  friend class AsyncConnect;
  struct ConnectionShard {
    grpc_core::Mutex mu;
    absl::flat_hash_map<int64_t, AsyncConnect*> pending_connections
        ABSL_GUARDED_BY(&mu);
  };

  static void PollerWorkInternal(
      std::shared_ptr<PosixEnginePollerManager> poller_manager);

  ConnectionHandle ConnectInternal(
      grpc_event_engine::experimental::PosixSocketWrapper sock,
      OnConnectCallback on_connect, ResolvedAddress addr,
      MemoryAllocator&& allocator,
      const grpc_event_engine::experimental::PosixTcpOptions& options,
      Duration timeout);

  void OnConnectFinishInternal(int connection_handle);

  std::vector<ConnectionShard> connection_shards_;
  std::atomic<int64_t> last_connection_id_{1};

#endif  // GRPC_POSIX_SOCKET_TCP

  grpc_core::Mutex mu_;
  TaskHandleSet known_handles_ ABSL_GUARDED_BY(mu_);
  std::atomic<intptr_t> aba_token_{0};
  std::shared_ptr<ThreadPool> executor_;
  TimerManager timer_manager_;
#ifdef GRPC_POSIX_SOCKET_TCP
  std::shared_ptr<PosixEnginePollerManager> poller_manager_;
#endif  // GRPC_POSIX_SOCKET_TCP
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENGINE_H
