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
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/core/call/call_spine.h"
#include "src/core/call/message.h"
#include "src/core/call/metadata.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/time.h"
#include "src/core/util/time_precise.h"
#include "src/core/util/wait_for_single_owner.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/promise/poll_matcher.h"
#include "test/core/test_util/mock_transport_filter.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
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

// What the v3 interceptor sees on the client-to-server path. Passed to the
// interceptor through channel args.
struct HalfCloseRecorder {
  struct RawPointerChannelArgTag {};
  static absl::string_view ChannelArgName() {
    return "grpc.test.half_close_recorder";
  }
  std::vector<std::string> events;
};

// A v3 interceptor run through V3InterceptorToV2Bridge. It forwards the call
// to the next v2 filter and records each client-to-server message and the
// client half-close into the HalfCloseRecorder from the channel args.
class HalfCloseRecordingInterceptor final
    : public V3InterceptorToV2Bridge<HalfCloseRecordingInterceptor> {
 public:
  static const grpc_channel_filter kFilter;

  explicit HalfCloseRecordingInterceptor(const ChannelArgs& args)
      : V3InterceptorToV2Bridge<HalfCloseRecordingInterceptor>(args),
        events_(&args.GetObject<HalfCloseRecorder>()->events) {}

  static absl::string_view TypeName() {
    return "half_close_recording_interceptor";
  }

  static absl::StatusOr<RefCountedPtr<HalfCloseRecordingInterceptor>> Create(
      const ChannelArgs& args, ChannelFilter::Args) {
    return MakeRefCounted<HalfCloseRecordingInterceptor>(args);
  }

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
    handler.SpawnInfallible(
        "record_client_to_server",
        [handler, initiator, events = events_]() mutable {
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

const grpc_channel_filter HalfCloseRecordingInterceptor::kFilter =
    MakePromiseBasedFilter<
        HalfCloseRecordingInterceptor, FilterEndpoint::kClient,
        kFilterExaminesServerInitialMetadata | kFilterExaminesOutboundMessages |
            kFilterExaminesInboundMessages>();

// A v2 channel stack with HalfCloseRecordingInterceptor on top of the mock
// transport, and one call on it. The tests start transport batches on the
// call, so they go through ClientCallData, and check that a client half-close
// (send_trailing_metadata) reaches the v3 interceptor through
// V3InterceptorToV2Bridge, and only after the last message.
//
// The v3 parties run on a FuzzingEventEngine set on the call arena, so all
// work runs on the test thread when the test drains it.
class ClientHalfClosePropagationTest : public ::testing::Test {
 protected:
  // A transport batch and everything it points to.
  struct Batch {
    grpc_call_element* elem;
    grpc_transport_stream_op_batch op{};
    grpc_transport_stream_op_batch_payload payload{};
    SliceBuffer message;
    grpc_closure on_complete;
    grpc_closure start;
  };

  ClientHalfClosePropagationTest() {
    transport_state_.call_combiner = &call_combiner_;
    std::vector<FilterAndConfig> filters = {
        {&HalfCloseRecordingInterceptor::kFilter, nullptr},
        {&MockTransportFilter::kFilter, nullptr},
    };
    auto channel_args = CoreConfiguration::Get()
                            .channel_args_preconditioning()
                            .PreconditionChannelArgs(nullptr)
                            .SetObject(&transport_state_)
                            .SetObject(&recorder_);
    channel_stack_ = static_cast<grpc_channel_stack*>(
        gpr_malloc(grpc_channel_stack_size(filters)));
    GRPC_CHECK_OK(grpc_channel_stack_init(
        1,
        [](void* p, grpc_error_handle) {
          grpc_channel_stack_destroy(static_cast<grpc_channel_stack*>(p));
          gpr_free(p);
        },
        channel_stack_, filters, channel_args, "test", channel_stack_));
    arena_->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine_.get());
    call_stack_ = static_cast<grpc_call_stack*>(
        gpr_malloc(channel_stack_->call_stack_size));
    const grpc_call_element_args call_args = {
        call_stack_,
        nullptr,
        gpr_get_cycle_counter(),
        Timestamp::InfFuture(),
        arena_.get(),
        &call_combiner_,
    };
    GRPC_CHECK_OK(grpc_call_stack_init(
        channel_stack_, 1,
        [](void* p, grpc_error_handle) {
          grpc_call_stack_destroy(static_cast<grpc_call_stack*>(p), nullptr,
                                  nullptr);
          gpr_free(p);
        },
        call_stack_, &call_args));
    GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_, RecvTrailingMetadataReady,
                      this, nullptr);
  }

  ~ClientHalfClosePropagationTest() override {
    GRPC_CALL_STACK_UNREF(call_stack_, "done");
    Drain();
    GRPC_CHANNEL_STACK_UNREF(channel_stack_, "done");
    Drain();
    event_engine_->UnsetGlobalHooks();
  }

  // Returns a new batch on the top filter of the call. The fixture keeps it
  // alive until the end of the test.
  Batch& NewBatch() {
    Batch& batch = *batches_.emplace_back(std::make_unique<Batch>());
    batch.elem = grpc_call_stack_element(call_stack_, 0);
    batch.op.payload = &batch.payload;
    GRPC_CLOSURE_INIT(&batch.on_complete, OnComplete, &call_combiner_, nullptr);
    batch.op.on_complete = &batch.on_complete;
    return batch;
  }

  // Adds the ops that the first batch of a call carries: send_initial_metadata
  // and recv_trailing_metadata.
  void AddStartCall(Batch& batch) {
    batch.op.send_initial_metadata = true;
    batch.payload.send_initial_metadata.send_initial_metadata =
        &client_initial_metadata_;
    batch.op.recv_trailing_metadata = true;
    batch.payload.recv_trailing_metadata.recv_trailing_metadata =
        &server_trailing_metadata_;
    batch.payload.recv_trailing_metadata.recv_trailing_metadata_ready =
        &recv_trailing_metadata_ready_;
  }

  void AddSendMessage(Batch& batch, absl::string_view payload) {
    batch.message.Append(Slice::FromCopiedString(payload));
    batch.op.send_message = true;
    batch.payload.send_message.send_message = &batch.message;
  }

  // Adds send_trailing_metadata, which is the client half-close.
  void AddSendTrailingMetadata(Batch& batch) {
    batch.op.send_trailing_metadata = true;
    batch.payload.send_trailing_metadata.send_trailing_metadata =
        &client_trailing_metadata_;
  }

  // Starts the batch under the call combiner, as the surface does, then drains.
  void StartBatch(Batch& batch) {
    GRPC_CLOSURE_INIT(&batch.start, DoStartBatch, &batch, nullptr);
    GRPC_CALL_COMBINER_START(&call_combiner_, &batch.start, absl::OkStatus(),
                             "start_batch");
    Drain();
  }

  // Runs the send_message on_complete held by the mock transport, then drains.
  void CompleteHeldSendMessage() {
    grpc_closure* on_complete = std::exchange(
        transport_state_.pending_send_message_on_complete, nullptr);
    ASSERT_NE(on_complete, nullptr);
    GRPC_CALL_COMBINER_START(&call_combiner_, on_complete, absl::OkStatus(),
                             "complete_held_send_message");
    Drain();
  }

  // Sends OK trailing metadata up from the mock transport and checks that the
  // call finishes.
  void FinishCall() {
    server_trailing_metadata_.Set(GrpcStatusMetadata(), GRPC_STATUS_OK);
    GRPC_CALL_COMBINER_START(&call_combiner_,
                             transport_state_.recv_trailing_metadata_ready,
                             absl::OkStatus(), "finish_call");
    Drain();
    EXPECT_TRUE(call_finished_);
  }

  // Runs closures and event engine work until neither has any left.
  void Drain() {
    for (int i = 0; i < 1000; ++i) {
      const bool did_work = ExecCtx::Get()->Flush();
      if (!did_work && event_engine_->IsIdle()) return;
      event_engine_->TickUntilIdle();
    }
    ADD_FAILURE() << "call did not quiesce";
  }

  static void DoStartBatch(void* arg, grpc_error_handle) {
    auto* batch = static_cast<Batch*>(arg);
    batch->elem->filter->start_transport_stream_op_batch(batch->elem,
                                                         &batch->op);
  }

  static void OnComplete(void* arg, grpc_error_handle) {
    GRPC_CALL_COMBINER_STOP(static_cast<CallCombiner*>(arg), "on_complete");
  }

  static void RecvTrailingMetadataReady(void* arg, grpc_error_handle) {
    auto* self = static_cast<ClientHalfClosePropagationTest*>(arg);
    GRPC_CALL_COMBINER_STOP(&self->call_combiner_, "recv_trailing_metadata");
    self->call_finished_ = true;
  }

  std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
      event_engine_ =
          std::make_shared<grpc_event_engine::experimental::FuzzingEventEngine>(
              grpc_event_engine::experimental::FuzzingEventEngine::Options(),
              fuzzing_event_engine::Actions());
  ExecCtx exec_ctx_;
  HalfCloseRecorder recorder_;
  CallCombiner call_combiner_;
  MockTransportFilter::State transport_state_;
  RefCountedPtr<Arena> arena_ = SimpleArenaAllocator()->MakeArena();
  grpc_channel_stack* channel_stack_ = nullptr;
  grpc_call_stack* call_stack_ = nullptr;
  grpc_metadata_batch client_initial_metadata_;
  grpc_metadata_batch client_trailing_metadata_;
  grpc_metadata_batch server_trailing_metadata_;
  grpc_closure recv_trailing_metadata_ready_;
  bool call_finished_ = false;
  std::vector<std::unique_ptr<Batch>> batches_;
};

// send_message and send_trailing_metadata in the same batch, as in unary calls.
TEST_F(ClientHalfClosePropagationTest,
       HalfCloseInSameBatchAsMessagePropagatesAfterMessage) {
  Batch& batch = NewBatch();
  AddStartCall(batch);
  AddSendMessage(batch, "hello");
  AddSendTrailingMetadata(batch);
  StartBatch(batch);
  EXPECT_THAT(recorder_.events,
              ::testing::ElementsAre("msg:hello", "half_close"));
  FinishCall();
}

// send_trailing_metadata in a later batch, after send_message has completed.
TEST_F(ClientHalfClosePropagationTest,
       HalfCloseInLaterBatchPropagatesAfterMessage) {
  Batch& batch1 = NewBatch();
  AddStartCall(batch1);
  AddSendMessage(batch1, "msg1");
  StartBatch(batch1);
  // No half-close was sent yet, so v3 must not see one.
  EXPECT_THAT(recorder_.events, ::testing::ElementsAre("msg:msg1"));
  Batch& batch2 = NewBatch();
  AddSendTrailingMetadata(batch2);
  StartBatch(batch2);
  EXPECT_THAT(recorder_.events,
              ::testing::ElementsAre("msg:msg1", "half_close"));
  FinishCall();
}

// send_trailing_metadata in the first batch, without any send_message.
TEST_F(ClientHalfClosePropagationTest,
       HalfCloseInFirstBatchWithoutAnyMessagePropagates) {
  Batch& batch = NewBatch();
  AddStartCall(batch);
  AddSendTrailingMetadata(batch);
  StartBatch(batch);
  EXPECT_THAT(recorder_.events, ::testing::ElementsAre("half_close"));
  FinishCall();
}

// send_trailing_metadata in a later batch, without any send_message.
TEST_F(ClientHalfClosePropagationTest,
       HalfCloseInLaterBatchWithoutAnyMessagePropagates) {
  Batch& batch1 = NewBatch();
  AddStartCall(batch1);
  StartBatch(batch1);
  EXPECT_THAT(recorder_.events, ::testing::IsEmpty());
  Batch& batch2 = NewBatch();
  AddSendTrailingMetadata(batch2);
  StartBatch(batch2);
  EXPECT_THAT(recorder_.events, ::testing::ElementsAre("half_close"));
  FinishCall();
}

// send_trailing_metadata while the transport has not yet completed the
// previous send_message.
TEST_F(ClientHalfClosePropagationTest,
       HalfCloseWhileMessageIsPendingPropagatesAfterMessageCompletes) {
  transport_state_.should_hold_send_message_on_complete = true;
  Batch& batch1 = NewBatch();
  AddStartCall(batch1);
  AddSendMessage(batch1, "msg1");
  StartBatch(batch1);
  EXPECT_THAT(recorder_.events, ::testing::ElementsAre("msg:msg1"));
  Batch& batch2 = NewBatch();
  AddSendTrailingMetadata(batch2);
  StartBatch(batch2);
  // send_message has not completed, so v3 must not see the half-close yet.
  EXPECT_THAT(recorder_.events, ::testing::ElementsAre("msg:msg1"));
  CompleteHeldSendMessage();
  EXPECT_THAT(recorder_.events,
              ::testing::ElementsAre("msg:msg1", "half_close"));
  FinishCall();
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_core::ForceEnableExperiment("promise_filter_client_half_close", true);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
