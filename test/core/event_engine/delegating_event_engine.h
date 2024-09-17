// Copyright 2024 gRPC authors.
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

#ifndef GRPC_TEST_CORE_EVENT_ENGINE_DELEGATING_EVENT_ENGINE_H
#define GRPC_TEST_CORE_EVENT_ENGINE_DELEGATING_EVENT_ENGINE_H

#include <memory>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include <grpc/event_engine/event_engine.h>

namespace grpc_event_engine {
namespace experimental {

class DelegatingEventEngine : public EventEngine {
 public:
  explicit DelegatingEventEngine(std::shared_ptr<EventEngine> wrapped_ee)
      : wrapped_ee_(std::move(wrapped_ee)) {}

  absl::StatusOr<std::unique_ptr<Listener>> CreateListener(
      Listener::AcceptCallback on_accept,
      absl::AnyInvocable<void(absl::Status)> on_shutdown,
      const EndpointConfig& config,
      std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory)
      override {
    return wrapped_ee_->CreateListener(std::move(on_accept),
                                       std::move(on_shutdown), config,
                                       std::move(memory_allocator_factory));
  }

  ConnectionHandle Connect(OnConnectCallback on_connect,
                           const ResolvedAddress& addr,
                           const EndpointConfig& args,
                           MemoryAllocator memory_allocator,
                           Duration timeout) override {
    return wrapped_ee_->Connect(std::move(on_connect), addr, args,
                                std::move(memory_allocator), timeout);
  }

  bool CancelConnect(ConnectionHandle handle) override {
    return wrapped_ee_->CancelConnect(handle);
  }

  bool IsWorkerThread() override { return wrapped_ee_->IsWorkerThread(); }

  absl::StatusOr<std::unique_ptr<DNSResolver>> GetDNSResolver(
      const DNSResolver::ResolverOptions& options) override {
    return wrapped_ee_->GetDNSResolver(options);
  }

  void Run(Closure* closure) override { return wrapped_ee_->Run(closure); }

  void Run(absl::AnyInvocable<void()> closure) override {
    return wrapped_ee_->Run(std::move(closure));
  }

  TaskHandle RunAfter(Duration when, Closure* closure) override {
    return wrapped_ee_->RunAfter(when, closure);
  }

  TaskHandle RunAfter(Duration when,
                      absl::AnyInvocable<void()> closure) override {
    return wrapped_ee_->RunAfter(when, std::move(closure));
  }

  bool Cancel(TaskHandle handle) override {
    return wrapped_ee_->Cancel(handle);
  }

 protected:
  EventEngine* wrapped_ee() const { return wrapped_ee_.get(); }

 private:
  std::shared_ptr<EventEngine> wrapped_ee_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_TEST_CORE_EVENT_ENGINE_DELEGATING_EVENT_ENGINE_H
