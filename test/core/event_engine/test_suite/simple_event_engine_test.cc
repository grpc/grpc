// Copyright 2021 gRPC authors.
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

#include <thread>

#include "absl/status/status.h"
#include "absl/time/clock.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice_allocator.h>
#include <grpc/support/log.h>

#include "test/core/event_engine/test_suite/event_engine_test.h"

using ::grpc_event_engine::experimental::EndpointConfig;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::SliceAllocator;
using ::grpc_event_engine::experimental::SliceAllocatorFactory;

constexpr absl::Duration SLEEP_TIME = absl::Milliseconds(100);

class SimpleEventEngine : public grpc_event_engine::experimental::EventEngine {
 public:
  void Run(EventEngine::Closure* closure) override { closure->Run(); }

  void Run(std::function<void()> closure) override { closure(); }

  absl::StatusOr<std::unique_ptr<Listener>> CreateListener(
      Listener::AcceptCallback on_accept,
      std::function<void(absl::Status)> on_shutdown,
      const EndpointConfig& config,
      std::unique_ptr<SliceAllocatorFactory> slice_allocator_factory) override {
    return nullptr;
  }

  EventEngine::TaskHandle RunAt(absl::Time when,
                                EventEngine::Closure* closure) override {
    std::thread t([=]() {
      while (absl::Now() < when) {
        absl::SleepFor(SLEEP_TIME);
        if (shutting_down_) return;
      }
      closure->Run();
    });
    return {-1, -1};
  }
  EventEngine::TaskHandle RunAt(absl::Time when,
                                std::function<void()> closure) override {
    threads_.emplace_back([=]() {
      while (absl::Now() < when) {
        absl::SleepFor(SLEEP_TIME);
        if (shutting_down_) return;
      }
      closure();
    });
    return {-1, -1};
  }

  bool Cancel(EventEngine::TaskHandle handle) override { return false; }

  absl::Status Connect(EventEngine::OnConnectCallback on_connect,
                       const EventEngine::ResolvedAddress& addr,
                       const EndpointConfig& args,
                       std::unique_ptr<SliceAllocator> slice_allocator,
                       absl::Time deadline) override {
    return absl::OkStatus();
  }

  std::unique_ptr<DNSResolver> GetDNSResolver() override { return nullptr; }

  bool IsWorkerThread() override { return false; }

  ~SimpleEventEngine() {
    shutting_down_ = true;
    for (std::thread& t : threads_) {
      t.join();
    }
  }

 private:
  std::vector<std::thread> threads_;
  bool shutting_down_ = false;
};

std::unique_ptr<EventEngine> EventEngineTest::NewEventEngine() {
  return absl::make_unique<SimpleEventEngine>();
}
