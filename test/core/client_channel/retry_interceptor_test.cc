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

#include "src/core/client_channel/retry_interceptor.h"

#include <grpc/grpc.h>

#include <atomic>
#include <memory>
#include <queue>

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/call/yodel/yodel_test.h"

namespace grpc_core {

using EventEngine = grpc_event_engine::experimental::EventEngine;

namespace {
const absl::string_view kTestPath = "/test_method";
}  // namespace

class RetryInterceptorTest : public YodelTest {
 protected:
  using YodelTest::YodelTest;

  void InitInterceptor(const ChannelArgs& args) {
    CHECK(destination_under_test_ == nullptr);
    InterceptionChainBuilder builder(args, nullptr, nullptr);
    builder.Add<RetryInterceptor>();
    destination_under_test_ = builder.Build(call_destination_).value();
  }

  ClientMetadataHandle MakeClientInitialMetadata() {
    auto client_initial_metadata =
        Arena::MakePooledForOverwrite<ClientMetadata>();
    client_initial_metadata->Set(HttpPathMetadata(),
                                 Slice::FromCopiedString(kTestPath));
    return client_initial_metadata;
  }

  CallInitiatorAndHandler MakeCall(
      ClientMetadataHandle client_initial_metadata) {
    auto arena = call_arena_allocator_->MakeArena();
    arena->SetContext<EventEngine>(event_engine().get());
    return MakeCallPair(std::move(client_initial_metadata), std::move(arena));
  }

  CallHandler TickUntilCallStarted() {
    auto poll = [this]() -> Poll<CallHandler> {
      auto handler = call_destination_->PopHandler();
      if (handler.has_value()) return std::move(*handler);
      return Pending();
    };
    return TickUntil(absl::FunctionRef<Poll<CallHandler>()>(poll));
  }

  UnstartedCallDestination& destination_under_test() {
    CHECK(destination_under_test_ != nullptr);
    return *destination_under_test_;
  }

 private:
  class TestCallDestination final : public UnstartedCallDestination {
   public:
    void StartCall(UnstartedCallHandler unstarted_call_handler) override {
      handlers_.push(unstarted_call_handler.StartCall());
    }

    std::optional<CallHandler> PopHandler() {
      if (handlers_.empty()) return std::nullopt;
      auto handler = std::move(handlers_.front());
      handlers_.pop();
      return handler;
    }

    void Orphaned() override {}

   private:
    std::queue<CallHandler> handlers_;
  };

  void InitCoreConfiguration() override {}

  void Shutdown() override {
    call_destination_.reset();
    destination_under_test_.reset();
    call_arena_allocator_.reset();
  }

  RefCountedPtr<TestCallDestination> call_destination_ =
      MakeRefCounted<TestCallDestination>();
  RefCountedPtr<UnstartedCallDestination> destination_under_test_;
  RefCountedPtr<CallArenaAllocator> call_arena_allocator_ =
      MakeRefCounted<CallArenaAllocator>(
          ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
              "test"),
          1024);
};

#define RETRY_INTERCEPTOR_TEST(name) YODEL_TEST(RetryInterceptorTest, name)

RETRY_INTERCEPTOR_TEST(NoOp) {
  InitInterceptor(ChannelArgs());
  destination_under_test();
}

RETRY_INTERCEPTOR_TEST(CreateCall) {
  InitInterceptor(ChannelArgs());
  auto call = MakeCall(MakeClientInitialMetadata());
  SpawnTestSeq(
      call.initiator, "initiator",
      [this, handler = std::move(call.handler)]() {
        destination_under_test().StartCall(handler);
      },
      [call_initiator = call.initiator]() mutable { call_initiator.Cancel(); });
  WaitForAllPendingWork();
}

RETRY_INTERCEPTOR_TEST(StartCall) {
  InitInterceptor(ChannelArgs());
  auto call = MakeCall(MakeClientInitialMetadata());
  SpawnTestSeq(call.initiator, "initiator",
               [this, handler = std::move(call.handler)]() {
                 destination_under_test().StartCall(handler);
               });
  auto handler = TickUntilCallStarted();
  SpawnTestSeq(
      call.initiator, "cancel",
      [call_initiator = call.initiator]() mutable { call_initiator.Cancel(); });
  WaitForAllPendingWork();
}

// TODO(roth, ctiller): more tests

}  // namespace grpc_core
