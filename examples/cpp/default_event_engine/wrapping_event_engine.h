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

#include <grpc/event_engine/event_engine.h>

namespace my_application {

// A custom EventEngine that keeps track of how many times the `Run` methods
// were invoked, and delegates all calls to a gRPC internal EventEngine instance
// owned by this object.
class WrappingEventEngine
    : public grpc_event_engine::experimental::EventEngine {
 public:
  WrappingEventEngine()
      : wrapped_engine_(grpc_event_engine::experimental::CreateEventEngine()) {}
  ~WrappingEventEngine() override = default;

  void Run(Closure* closure) override {
    run_count_++;
    wrapped_engine_->Run(closure);
  }

  void Run(absl::AnyInvocable<void()> closure) override {
    run_count_++;
    wrapped_engine_->Run(std::move(closure));
  }

  int get_run_count() { return run_count_.load(); }

  // Passthrough methods
  absl::StatusOr<std::unique_ptr<Listener>> CreateListener(
      Listener::AcceptCallback on_accept,
      absl::AnyInvocable<void(absl::Status)> on_shutdown,
      const grpc_event_engine::experimental::EndpointConfig& config,
      std::unique_ptr<grpc_event_engine::experimental::MemoryAllocatorFactory>
          memory_allocator_factory) override {
    return wrapped_engine_->CreateListener(std::move(on_accept),
                                           std::move(on_shutdown), config,
                                           std::move(memory_allocator_factory));
  }
  ConnectionHandle Connect(
      OnConnectCallback on_connect, const ResolvedAddress& addr,
      const grpc_event_engine::experimental::EndpointConfig& args,
      grpc_event_engine::experimental::MemoryAllocator memory_allocator,
      Duration timeout) override {
    return wrapped_engine_->Connect(std::move(on_connect), addr, args,
                                    std::move(memory_allocator), timeout);
  }
  bool CancelConnect(ConnectionHandle handle) override {
    return wrapped_engine_->CancelConnect(handle);
  }

  bool IsWorkerThread() override { return wrapped_engine_->IsWorkerThread(); }

  absl::StatusOr<std::unique_ptr<DNSResolver>> GetDNSResolver(
      const DNSResolver::ResolverOptions& options) override {
    return wrapped_engine_->GetDNSResolver(options);
  }

  TaskHandle RunAfter(Duration when, Closure* closure) override {
    return wrapped_engine_->RunAfter(when, closure);
  }

  TaskHandle RunAfter(Duration when,
                      absl::AnyInvocable<void()> closure) override {
    return wrapped_engine_->RunAfter(when, std::move(closure));
  }
  bool Cancel(TaskHandle handle) override {
    return wrapped_engine_->Cancel(handle);
  }

 private:
  std::shared_ptr<EventEngine> wrapped_engine_;
  std::atomic<int> run_count_{0};
};

}  // namespace my_application