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
#include <string>
#include <utility>
#include <vector>

#include "src/core/call/call_spine.h"
#include "src/core/call/message.h"
#include "src/core/call/metadata.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/wait_for_single_owner.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/promise/poll_matcher.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace grpc_core {
namespace {

class EarlyFailureInterceptor
    : public V3InterceptorToV2Bridge<EarlyFailureInterceptor> {
 public:
  EarlyFailureInterceptor()
      : V3InterceptorToV2Bridge<EarlyFailureInterceptor>(ChannelArgs()) {}

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
      : V3InterceptorToV2Bridge<HangingInterceptor>(ChannelArgs()),
        done_(std::move(done)),
        cancelled_(std::move(cancelled)) {}

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

// A v3 interceptor run through V3InterceptorToV2Bridge. It forwards the call
// to the next v2 filter and records each client-to-server message and the
// client half-close into `events`.
class HalfCloseRecordingInterceptor final
    : public V3InterceptorToV2Bridge<HalfCloseRecordingInterceptor> {
 public:
  explicit HalfCloseRecordingInterceptor(std::vector<std::string>* events)
      : V3InterceptorToV2Bridge<HalfCloseRecordingInterceptor>(ChannelArgs()),
        events_(events) {}

  void InterceptCall(UnstartedCallHandler unstarted_call_handler) override {
    CallHandler handler = Consume(std::move(unstarted_call_handler));
    handler.SpawnGuarded(
        "start_child_call",
        [self = RefAsSubclass<HalfCloseRecordingInterceptor>(),
         handler]() mutable {
          return TrySeq(handler.PullClientInitialMetadata(),
                        [self, handler](ClientMetadataHandle metadata) mutable {
                          CallInitiator initiator = self->MakeChildCall(
                              std::move(metadata), handler.arena()->Ref());
                          handler.AddChildCall(initiator);
                          self->ForwardAndRecord(handler, initiator);
                          return absl::OkStatus();
                        });
        });
  }

  void Orphaned() override {}

 private:
  void ForwardAndRecord(CallHandler handler, CallInitiator initiator) {
    std::vector<std::string>* events = events_;
    handler.SpawnInfallible(
        "record_client_to_server", [handler, initiator, events]() mutable {
          return Seq(ForEach(MessagesFrom(handler),
                             [initiator, events](MessageHandle msg) mutable {
                               events->push_back(absl::StrCat(
                                   "msg:", msg->payload()->JoinIntoString()));
                               initiator.SpawnPushMessage(std::move(msg));
                               return Success{};
                             }),
                     [initiator, events](StatusFlag status) mutable {
                       // A clean end of the message stream is the client
                       // half-close.
                       if (status.ok()) {
                         events->push_back("half_close");
                       }
                       initiator.SpawnFinishSends();
                       return Empty{};
                     });
        });
    initiator.SpawnInfallible(
        "forward_server_trailing_metadata", [handler, initiator]() mutable {
          return Map(initiator.PullServerTrailingMetadata(),
                     [handler](ServerMetadataHandle md) mutable {
                       handler.SpawnPushServerTrailingMetadata(std::move(md));
                       return Empty{};
                     });
        });
  }

  std::vector<std::string>* events_;
};

// An activity that records whether it has been woken, so a test can poll a
// promise until it stops making progress instead of a fixed number of times.
class WakeTrackingActivity final : public Activity, public Wakeable {
 public:
  void Run(absl::FunctionRef<void()> f) {
    ScopedActivity scoped(this);
    f();
  }
  bool TakeWoken() { return std::exchange(woken_, false); }
  void Orphan() override {}
  void ForceImmediateRepoll(WakeupMask) override { woken_ = true; }
  Waker MakeNonOwningWaker() override { return Waker(this, 0); }
  Waker MakeOwningWaker() override { return Waker(this, 0); }
  void Wakeup(WakeupMask) override { woken_ = true; }
  void WakeupAsync(WakeupMask) override { woken_ = true; }
  void Drop(WakeupMask) override {}
  std::string DebugTag() const override { return "WakeTrackingActivity"; }
  std::string ActivityDebugTag(WakeupMask) const override { return DebugTag(); }

 private:
  bool woken_ = false;
};

// Drives V3InterceptorToV2Bridge::MakeCallPromise directly with
// HalfCloseRecordingInterceptor. The test plays the previous v2 filter: it
// owns the client-to-server pipe sender, pushes messages into it and closes it
// to half-close. The next v2 filter is a promise that pulls and acks
// client-to-server messages like a transport would. The tests check that the
// half-close reaches the v3 interceptor, and only after the last message.
//
// The v3 parties run on a FuzzingEventEngine set on the call arena, so all
// work runs on the test thread when the test ticks it.
class ClientHalfClosePropagationTest : public ::testing::Test {
 protected:
  ClientHalfClosePropagationTest() {
    arena_->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine_.get());
    RunInActivity([this]() {
      pipes_.emplace();
      CallArgs args{Arena::MakePooledForOverwrite<ClientMetadata>(),
                    ClientInitialMetadataOutstandingToken::Empty(),
                    nullptr,
                    &pipes_->server_initial_metadata.sender,
                    &pipes_->client_to_server.receiver,
                    &pipes_->server_to_client.sender};
      call_promise_ = interceptor_->MakeCallPromise(
          std::move(args), [this](CallArgs next_args) {
            return NextFilter(next_args.client_to_server_messages);
          });
    });
  }

  ~ClientHalfClosePropagationTest() override {
    // The call never finishes, so destroying the promise cancels the v3 call.
    RunInActivity([this]() {
      push_.reset();
      call_promise_.reset();
      pipes_.reset();
    });
    event_engine_->TickUntilIdle();
    event_engine_->UnsetGlobalHooks();
  }

  // The next v2 filter: pulls client-to-server messages until the pipe closes.
  // Each message is acked once `hold_message_ack_` is false; holding the ack
  // is like a transport that has not yet completed the send. It never returns
  // trailing metadata.
  ArenaPromise<ServerMetadataHandle> NextFilter(
      PipeReceiver<MessageHandle>* messages) {
    return Seq(
        Loop([this, messages]() {
          return Seq(
              messages->Next(), [this](NextResult<MessageHandle> message) {
                return [this, message = std::move(
                                  message)]() mutable -> Poll<LoopCtl<Empty>> {
                  if (!message.has_value()) return Empty{};
                  if (hold_message_ack_) return Pending{};
                  return Continue{};
                };
              });
        }),
        [](Empty) { return Never<ServerMetadataHandle>(); });
  }

  template <typename F>
  void RunInActivity(F f) {
    activity_.Run([&]() {
      promise_detail::Context<Arena> arena_ctx(arena_.get());
      f();
    });
  }

  // Starts pushing a message from the previous v2 filter. The push is polled
  // once so the message is in the pipe before this returns.
  void SendMessage(absl::string_view payload) {
    ASSERT_FALSE(push_.has_value());
    RunInActivity([&]() {
      push_.emplace(
          pipes_->client_to_server.sender.Push(Arena::MakePooled<Message>(
              SliceBuffer(Slice::FromCopiedString(payload)), 0)));
      PollPush();
    });
  }

  // Polls the v2 side and ticks the event engine until neither has work left.
  void Drain() {
    for (int i = 0; i < 1000; ++i) {
      activity_.TakeWoken();
      RunInActivity([this]() {
        PollPush();
        EXPECT_TRUE((*call_promise_)().pending());
      });
      const bool did_work = ExecCtx::Get()->Flush();
      if (!activity_.TakeWoken() && !did_work && event_engine_->IsIdle()) {
        return;
      }
      event_engine_->TickUntilIdle();
    }
    ADD_FAILURE() << "call did not quiesce";
  }

  void PollPush() {
    if (!push_.has_value()) return;
    Poll<bool> pushed = (*push_)();
    if (pushed.ready()) {
      EXPECT_TRUE(pushed.value());
      push_.reset();
    }
  }

  struct Pipes {
    Pipe<ServerMetadataHandle> server_initial_metadata;
    Pipe<MessageHandle> client_to_server;
    Pipe<MessageHandle> server_to_client;
  };

  std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
      event_engine_ =
          std::make_shared<grpc_event_engine::experimental::FuzzingEventEngine>(
              grpc_event_engine::experimental::FuzzingEventEngine::Options(),
              fuzzing_event_engine::Actions());
  ExecCtx exec_ctx_;
  std::vector<std::string> events_;
  RefCountedPtr<Arena> arena_ = SimpleArenaAllocator()->MakeArena();
  RefCountedPtr<HalfCloseRecordingInterceptor> interceptor_ =
      MakeRefCounted<HalfCloseRecordingInterceptor>(&events_);
  WakeTrackingActivity activity_;
  bool hold_message_ack_ = false;
  std::optional<Pipes> pipes_;
  std::optional<PipeSender<MessageHandle>::PushType> push_;
  std::optional<ArenaPromise<ServerMetadataHandle>> call_promise_;
};

TEST_F(ClientHalfClosePropagationTest,
       HalfCloseRightAfterMessagePropagatesAfterMessage) {
  SendMessage("hello");
  RunInActivity([this]() { pipes_->client_to_server.sender.Close(); });
  Drain();
  EXPECT_THAT(events_, ::testing::ElementsAre("msg:hello", "half_close"));
}

TEST_F(ClientHalfClosePropagationTest, HalfCloseAfterMessagePropagates) {
  SendMessage("msg1");
  Drain();
  // No half-close was sent yet, so v3 must not see one.
  EXPECT_THAT(events_, ::testing::ElementsAre("msg:msg1"));
  RunInActivity([this]() { pipes_->client_to_server.sender.Close(); });
  Drain();
  EXPECT_THAT(events_, ::testing::ElementsAre("msg:msg1", "half_close"));
}

TEST_F(ClientHalfClosePropagationTest, HalfCloseWithoutAnyMessagePropagates) {
  Drain();
  EXPECT_THAT(events_, ::testing::IsEmpty());
  RunInActivity([this]() { pipes_->client_to_server.sender.Close(); });
  Drain();
  EXPECT_THAT(events_, ::testing::ElementsAre("half_close"));
}

TEST_F(ClientHalfClosePropagationTest,
       HalfCloseWhilePreviousMessageIsNotAckedPropagatesAfterAck) {
  hold_message_ack_ = true;
  SendMessage("msg1");
  Drain();
  EXPECT_THAT(events_, ::testing::ElementsAre("msg:msg1"));
  // Half-close while the next filter still holds the ack for msg1. The v3
  // interceptor must not observe the half-close yet.
  RunInActivity([this]() { pipes_->client_to_server.sender.Close(); });
  Drain();
  EXPECT_THAT(events_, ::testing::ElementsAre("msg:msg1"));
  // Now ack msg1.
  hold_message_ack_ = false;
  Drain();
  EXPECT_THAT(events_, ::testing::ElementsAre("msg:msg1", "half_close"));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_core::ForceEnableExperiment("v2_non_owning_waker_implementation", true);
  grpc_core::ForceEnableExperiment("promise_filter_client_half_close", true);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
