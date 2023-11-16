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
#include <grpc/support/port_platform.h>

#include <algorithm>
#include <memory>
#include <thread>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/grpc.h>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "test/core/util/test_config.h"

namespace {

using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;

class DefaultEngineTest : public testing::Test {
 protected:
  // Does nothing, fills space that a nullptr could not
  class FakeEventEngine : public EventEngine {
   public:
    FakeEventEngine() = default;
    ~FakeEventEngine() override = default;
    absl::StatusOr<std::unique_ptr<Listener>> CreateListener(
        Listener::AcceptCallback /* on_accept */,
        absl::AnyInvocable<void(absl::Status)> /* on_shutdown */,
        const grpc_event_engine::experimental::EndpointConfig& /* config */,
        std::unique_ptr<
            grpc_event_engine::experimental::
                MemoryAllocatorFactory> /* memory_allocator_factory */)
        override {
      return absl::UnimplementedError("test");
    };
    ConnectionHandle Connect(
        OnConnectCallback /* on_connect */, const ResolvedAddress& /* addr */,
        const grpc_event_engine::experimental::EndpointConfig& /* args */,
        grpc_event_engine::experimental::MemoryAllocator /* memory_allocator */,
        Duration /* timeout */) override {
      return {-1, -1};
    };
    bool CancelConnect(ConnectionHandle /* handle */) override {
      return false;
    };
    bool IsWorkerThread() override { return false; };
    absl::StatusOr<std::unique_ptr<DNSResolver>> GetDNSResolver(
        const DNSResolver::ResolverOptions& /* options */) override {
      return nullptr;
    };
    void Run(Closure* /* closure */) override{};
    void Run(absl::AnyInvocable<void()> /* closure */) override{};
    TaskHandle RunAfter(Duration /* when */, Closure* /* closure */) override {
      return {-1, -1};
    }
    TaskHandle RunAfter(Duration /* when */,
                        absl::AnyInvocable<void()> /* closure */) override {
      return {-1, -1};
    }
    bool Cancel(TaskHandle /* handle */) override { return false; };
  };
};

TEST_F(DefaultEngineTest, SharedPtrGlobalEventEngineLifetimesAreValid) {
  int create_count = 0;
  grpc_event_engine::experimental::SetEventEngineFactory([&create_count] {
    ++create_count;
    return std::make_unique<FakeEventEngine>();
  });
  std::shared_ptr<EventEngine> ee2;
  {
    std::shared_ptr<EventEngine> ee1 = GetDefaultEventEngine();
    ASSERT_EQ(1, create_count);
    ee2 = GetDefaultEventEngine();
    ASSERT_EQ(1, create_count);
    ASSERT_EQ(ee2.use_count(), 2);
  }
  // Ensure the first shared_ptr did not delete the global
  ASSERT_TRUE(ee2.unique());
  ASSERT_FALSE(ee2->IsWorkerThread());  // useful for ASAN
  // destroy the global engine via the last shared_ptr, and create a new one.
  ee2.reset();
  ee2 = GetDefaultEventEngine();
  ASSERT_EQ(2, create_count);
  ASSERT_TRUE(ee2.unique());
  grpc_event_engine::experimental::EventEngineFactoryReset();
}

TEST_F(DefaultEngineTest, StressTestSharedPtr) {
  constexpr int thread_count = 13;
  constexpr absl::Duration spin_time = absl::Seconds(3);
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (int i = 0; i < thread_count; i++) {
    threads.emplace_back([&spin_time] {
      auto timeout = absl::Now() + spin_time;
      do {
        GetDefaultEventEngine().reset();
      } while (timeout > absl::Now());
    });
  }
  for (auto& thd : threads) {
    thd.join();
  }
}
}  // namespace

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
