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

#include "src/core/client_channel/load_balanced_call_destination.h"

#include <grpc/grpc.h>

#include <atomic>
#include <memory>
#include <queue>

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/core/call/yodel/yodel_test.h"

using testing::StrictMock;

namespace grpc_core {

using EventEngine = grpc_event_engine::experimental::EventEngine;

namespace {
const absl::string_view kTestPath = "/test_method";
}  // namespace

class LoadBalancedCallDestinationTest : public YodelTest {
 protected:
  using YodelTest::YodelTest;

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

  LoadBalancedCallDestination& destination_under_test() {
    return *destination_under_test_;
  }

  ClientChannel::PickerObservable& picker() { return picker_; }

  RefCountedPtr<SubchannelInterface> subchannel() { return subchannel_; }

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

  class TestSubchannel : public SubchannelInterfaceWithCallDestination {
   public:
    explicit TestSubchannel(
        RefCountedPtr<UnstartedCallDestination> call_destination)
        : call_destination_(std::move(call_destination)) {}

    void WatchConnectivityState(
        std::unique_ptr<ConnectivityStateWatcherInterface>) override {
      Crash("not implemented");
    }
    void CancelConnectivityStateWatch(
        ConnectivityStateWatcherInterface*) override {
      Crash("not implemented");
    }
    void RequestConnection() override { Crash("not implemented"); }
    void ResetBackoff() override { Crash("not implemented"); }
    void AddDataWatcher(std::unique_ptr<DataWatcherInterface>) override {
      Crash("not implemented");
    }
    void CancelDataWatcher(DataWatcherInterface*) override {
      Crash("not implemented");
    }
    RefCountedPtr<UnstartedCallDestination> call_destination() override {
      return call_destination_;
    }

    std::string address() const override { return "test"; }

   private:
    const RefCountedPtr<UnstartedCallDestination> call_destination_;
  };

  void InitCoreConfiguration() override {}

  void Shutdown() override {
    channel_.reset();
    picker_ = ClientChannel::PickerObservable(nullptr);
    call_destination_.reset();
    destination_under_test_.reset();
    call_arena_allocator_.reset();
    subchannel_.reset();
  }

  RefCountedPtr<ClientChannel> channel_;
  ClientChannel::PickerObservable picker_{nullptr};
  RefCountedPtr<TestCallDestination> call_destination_ =
      MakeRefCounted<TestCallDestination>();
  RefCountedPtr<LoadBalancedCallDestination> destination_under_test_ =
      MakeRefCounted<LoadBalancedCallDestination>(picker_);
  RefCountedPtr<CallArenaAllocator> call_arena_allocator_ =
      MakeRefCounted<CallArenaAllocator>(
          ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
              "test"),
          1024);
  RefCountedPtr<TestSubchannel> subchannel_ =
      MakeRefCounted<TestSubchannel>(call_destination_);
};

#define LOAD_BALANCED_CALL_DESTINATION_TEST(name) \
  YODEL_TEST(LoadBalancedCallDestinationTest, name)

class MockPicker : public LoadBalancingPolicy::SubchannelPicker {
 public:
  MOCK_METHOD(LoadBalancingPolicy::PickResult, Pick,
              (LoadBalancingPolicy::PickArgs));
};

LOAD_BALANCED_CALL_DESTINATION_TEST(NoOp) {}

LOAD_BALANCED_CALL_DESTINATION_TEST(CreateCall) {
  auto call = MakeCall(MakeClientInitialMetadata());
  SpawnTestSeq(
      call.initiator, "initiator",
      [this, handler = std::move(call.handler)]() {
        destination_under_test().StartCall(handler);
      },
      [call_initiator = call.initiator]() mutable { call_initiator.Cancel(); });
  WaitForAllPendingWork();
}

LOAD_BALANCED_CALL_DESTINATION_TEST(StartCall) {
  auto call = MakeCall(MakeClientInitialMetadata());
  SpawnTestSeq(call.initiator, "initiator",
               [this, handler = std::move(call.handler)]() {
                 destination_under_test().StartCall(handler);
               });
  auto mock_picker = MakeRefCounted<StrictMock<MockPicker>>();
  EXPECT_CALL(*mock_picker, Pick)
      .WillOnce([this](LoadBalancingPolicy::PickArgs) {
        return LoadBalancingPolicy::PickResult::Complete{subchannel()};
      });
  picker().Set(mock_picker);
  auto handler = TickUntilCallStarted();
  SpawnTestSeq(
      call.initiator, "cancel",
      [call_initiator = call.initiator]() mutable { call_initiator.Cancel(); });
  WaitForAllPendingWork();
}

LOAD_BALANCED_CALL_DESTINATION_TEST(StartCallOnDestroyedChannel) {
  // Create a call.
  auto call = MakeCall(MakeClientInitialMetadata());
  // Client side part of the call: wait for status and expect that it's
  // UNAVAILABLE
  SpawnTestSeq(
      call.initiator, "initiator",
      [this, handler = std::move(call.handler),
       initiator = call.initiator]() mutable {
        destination_under_test().StartCall(handler);
        return initiator.PullServerTrailingMetadata();
      },
      [](ServerMetadataHandle md) {
        EXPECT_EQ(md->get(GrpcStatusMetadata()).value_or(GRPC_STATUS_UNKNOWN),
                  GRPC_STATUS_UNAVAILABLE);
      });
  // Set a picker and wait for at least one pick attempt to prove the call has
  // made it to the picker.
  auto mock_picker = MakeRefCounted<StrictMock<MockPicker>>();
  std::atomic<bool> queued_once{false};
  EXPECT_CALL(*mock_picker, Pick)
      .WillOnce([&queued_once](LoadBalancingPolicy::PickArgs) {
        queued_once.store(true, std::memory_order_relaxed);
        return LoadBalancingPolicy::PickResult::Queue{};
      });
  picker().Set(mock_picker);
  TickUntil<Empty>([&queued_once]() -> Poll<Empty> {
    if (queued_once.load(std::memory_order_relaxed)) return Empty{};
    return Pending();
  });
  // Now set the drop picker (as the client channel does at shutdown) which
  // should trigger Unavailable to be seen by the client side part of the call.
  picker().Set(MakeRefCounted<LoadBalancingPolicy::DropPicker>(
      absl::UnavailableError("Channel destroyed")));
  WaitForAllPendingWork();
}

// TODO(roth, ctiller): more tests
// - tests for the picker returning queue, fail, and drop results.

}  // namespace grpc_core
