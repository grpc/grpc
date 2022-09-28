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
#include <grpc/event_engine/slice_buffer.h>

#include "src/core/lib/event_engine/executor/threaded_executor.h"
#include "src/core/lib/event_engine/handle_containers.h"
#include "src/core/lib/event_engine/posix_engine/timer_manager.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_event_engine {
namespace experimental {

// An iomgr-based Posix EventEngine implementation.
// All methods require an ExecCtx to already exist on the thread's stack.
class PosixEventEngine final : public EventEngine {
 public:
  class PosixEndpoint : public EventEngine::Endpoint {
   public:
    ~PosixEndpoint() override;
    void Read(absl::AnyInvocable<void(absl::Status)> on_read,
              SliceBuffer* buffer, const ReadArgs* args) override;
    void Write(absl::AnyInvocable<void(absl::Status)> on_writable,
               SliceBuffer* data, const WriteArgs* args) override;
    const ResolvedAddress& GetPeerAddress() const override;
    const ResolvedAddress& GetLocalAddress() const override;
  };
  class PosixListener : public EventEngine::Listener {
   public:
    ~PosixListener() override;
    absl::StatusOr<int> Bind(const ResolvedAddress& addr) override;
    absl::Status Start() override;
  };
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

  PosixEventEngine() = default;
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

 private:
  struct ClosureData;
  EventEngine::TaskHandle RunAfterInternal(Duration when,
                                           absl::AnyInvocable<void()> cb);

  posix_engine::TimerManager timer_manager_;
  ThreadedExecutor executor_{2};

  grpc_core::Mutex mu_;
  TaskHandleSet known_handles_ ABSL_GUARDED_BY(mu_);
  std::atomic<intptr_t> aba_token_{0};
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENGINE_H
