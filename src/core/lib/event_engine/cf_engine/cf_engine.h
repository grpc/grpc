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
#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_CF_ENGINE_CF_ENGINE_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_CF_ENGINE_CF_ENGINE_H
#include <grpc/support/port_platform.h>

#ifdef GPR_APPLE

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/handle_containers.h"
#include "src/core/lib/event_engine/posix_engine/timer_manager.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/surface/init_internally.h"

namespace grpc_event_engine {
namespace experimental {

class CFEventEngine : public EventEngine,
                      public grpc_core::KeepsGrpcInitialized {
 public:
  CFEventEngine();
  ~CFEventEngine() override;

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
  struct Closure;
  EventEngine::TaskHandle RunAfterInternal(Duration when,
                                           absl::AnyInvocable<void()> cb);
  grpc_core::Mutex mu_;
  TaskHandleSet known_handles_ ABSL_GUARDED_BY(mu_);
  std::atomic<intptr_t> aba_token_{0};
  std::shared_ptr<ThreadPool> executor_;
  TimerManager timer_manager_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GPR_APPLE

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_CF_ENGINE_CF_ENGINE_H