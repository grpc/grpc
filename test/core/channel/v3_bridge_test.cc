// Copyright 2026 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include <memory>
#include <optional>

#include "src/core/call/call_spine.h"
#include "src/core/call/metadata.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/util/wait_for_single_owner.h"
#include "test/core/promise/poll_matcher.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace grpc_core {
namespace {

class EarlyFailureInterceptor
    : public V3InterceptorToV2Bridge<EarlyFailureInterceptor> {
 public:
  void InterceptCall(UnstartedCallHandler unstarted_call_handler) override {
    // Start the call and fail it immediately.
    auto handler = unstarted_call_handler.StartCall();
    handler.PushServerTrailingMetadata(
        ServerMetadataFromStatus(absl::InternalError("Early failure")));
  }
  void Orphaned() override {}
};

class TestActivity final : public Activity, public Wakeable {
 public:
  void Run(absl::FunctionRef<void()> f) {
    ScopedActivity scoped(this);
    f();
  }
  void Orphan() override {}
  void ForceImmediateRepoll(WakeupMask) override {}
  Waker MakeNonOwningWaker() override { return Waker(this, 0); }
  Waker MakeOwningWaker() override { return Waker(this, 0); }
  void Wakeup(WakeupMask) override {}
  void WakeupAsync(WakeupMask) override {}
  void Drop(WakeupMask) override {}
  std::string DebugTag() const override { return "TestActivity"; }
  std::string ActivityDebugTag(WakeupMask) const override { return DebugTag(); }
};

class V3BridgeTest : public ::testing::Test {
 protected:
  V3BridgeTest()
      : arena_factory_(SimpleArenaAllocator()),
        arena_(arena_factory_->MakeArena()) {
    arena_->SetContext<grpc_event_engine::experimental::EventEngine>(
        grpc_event_engine::experimental::GetDefaultEventEngine().get());
  }

  template <typename F>
  void RunInActivity(F f) {
    TestActivity activity;
    activity.Run([&]() {
      promise_detail::Context<Arena> arena_ctx(arena_.get());
      f();
    });
  }

  RefCountedPtr<ArenaFactory> arena_factory_;
  RefCountedPtr<Arena> arena_;
};

TEST_F(V3BridgeTest, EarlyFailureDoesNotHang) {
  RunInActivity([&]() {
    EarlyFailureInterceptor bridge;
    auto next_promise_factory = [](CallArgs) {
      return ArenaPromise<ServerMetadataHandle>(
          []() -> Poll<ServerMetadataHandle> { return Pending{}; });
    };
    CallArgs args{Arena::MakePooledForOverwrite<ClientMetadata>(),
                  ClientInitialMetadataOutstandingToken::Empty(),
                  nullptr,
                  nullptr,
                  nullptr,
                  nullptr};
    auto promise =
        bridge.MakeCallPromise(std::move(args), next_promise_factory);
    // The promise should eventually resolve to an error because the V3
    // interceptor failed.
    Poll<ServerMetadataHandle> result = Pending{};
    for (int i = 0; i < 100 && result.pending(); ++i) {
      result = promise();
    }
    EXPECT_TRUE(result.ready());
    if (result.ready()) {
      auto status = result.value()
                        ->get(GrpcStatusMetadata())
                        .value_or(GRPC_STATUS_UNKNOWN);
      EXPECT_EQ(status, GRPC_STATUS_INTERNAL);
    }
  });
}

TEST_F(V3BridgeTest, EarlyFailureCleansUpArena) {
  auto factory = SimpleArenaAllocator();
  auto arena = factory->MakeArena();
  arena->SetContext<grpc_event_engine::experimental::EventEngine>(
      grpc_event_engine::experimental::GetDefaultEventEngine().get());
  auto* arena_ptr = arena.get();
  RunInActivity([&]() {
    promise_detail::Context<Arena> arena_ctx(arena_ptr);
    EarlyFailureInterceptor bridge;
    auto next_promise_factory = [](CallArgs) {
      return ArenaPromise<ServerMetadataHandle>(
          []() -> Poll<ServerMetadataHandle> { return Pending{}; });
    };
    {
      CallArgs args{Arena::MakePooledForOverwrite<ClientMetadata>(),
                    ClientInitialMetadataOutstandingToken::Empty(),
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr};
      auto promise =
          bridge.MakeCallPromise(std::move(args), next_promise_factory);
      Poll<ServerMetadataHandle> result = Pending{};
      for (int i = 0; i < 100 && result.pending(); ++i) {
        result = promise();
      }
      EXPECT_TRUE(result.ready());
    }
  });
  // Verify that the arena can be destroyed.
  arena.reset();
}

// An interceptor that wires up a real v3 call (so the bridge spawns all of its
// helper promises) but never produces server trailing metadata -- i.e. the
// call stays in flight forever. It registers an OnDone callback on the v3
// handler so the test can observe whether the v3 call pair is ever torn down.
class HangingInterceptor : public V3InterceptorToV2Bridge<HangingInterceptor> {
 public:
  HangingInterceptor(std::shared_ptr<absl::Notification> done,
                     std::shared_ptr<bool> cancelled)
      : done_(std::move(done)), cancelled_(std::move(cancelled)) {}

  void InterceptCall(UnstartedCallHandler unstarted_call_handler) override {
    // Consume the call: start it and hold on to the handler so the v3 spine
    // stays alive. We deliberately never push trailing metadata, leaving the
    // call in flight.
    CallHandler handler = unstarted_call_handler.StartCall();
    const bool registered = handler.OnDone(
        [done = done_, cancelled = cancelled_](bool was_cancelled) {
          *cancelled = was_cancelled;
          done->Notify();
        });
    CHECK(registered);
    handler_.emplace(std::move(handler));
  }
  void Orphaned() override {}

 private:
  std::shared_ptr<absl::Notification> done_;
  std::shared_ptr<bool> cancelled_;
  std::optional<CallHandler> handler_;
};

// When the v2 promise is force-destroyed while the v3 call is still
// in flight (as ServerCallData::Completed does with `promise =
// ArenaPromise<>`), the bridge must propagate a cancellation to v3 call pair so
// that the v3 handler's OnDone fires (with cancelled = true) and the v3 spine
// is torn down rather than leaked.
TEST_F(V3BridgeTest, ForceDestroyPromiseCancelsV3Call) {
  auto done = std::make_shared<absl::Notification>(false);
  auto cancelled = std::make_shared<bool>(false);
  RunInActivity([&]() {
    HangingInterceptor bridge(done, cancelled);
    auto next_promise_factory = [](CallArgs) {
      return ArenaPromise<ServerMetadataHandle>(
          []() -> Poll<ServerMetadataHandle> { return Pending{}; });
    };
    CallArgs args{Arena::MakePooledForOverwrite<ClientMetadata>(),
                  ClientInitialMetadataOutstandingToken::Empty(),
                  nullptr,
                  nullptr,
                  nullptr,
                  nullptr};
    auto promise =
        bridge.MakeCallPromise(std::move(args), next_promise_factory);
    // Poll promise once to let the bridge wire everything up. The call is in
    // flight, so the promise stays pending.
    Poll<ServerMetadataHandle> result = promise();
    EXPECT_TRUE(result.pending());
    EXPECT_FALSE(done->HasBeenNotified());
    // Promise is forced destroyed similar to how it's done in
    // ServerCallData::Completed
    promise = ArenaPromise<ServerMetadataHandle>();
    // The v3 call pair must be cancelled as a result. The cancellation is
    // pushed onto the v3 CallSpine's party and drained on an event engine
    // thread, so wait for OnDone to fire.
    EXPECT_TRUE(done->WaitForNotificationWithTimeout(absl::Seconds(2)))
        << "v3 handler OnDone never fired -- the v3 CallSpine leaked because "
           "the forced promise destruction was not propagated as a "
           "cancellation.";
    EXPECT_TRUE(*cancelled)
        << "v3 call was torn down but not observed as a cancellation.";
    arena_.reset();
  });
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_core::ForceEnableExperiment("v2_non_owning_waker_implementation", true);
  return RUN_ALL_TESTS();
}
