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
#include "src/core/util/sync.h"
#include "src/core/util/wait_for_single_owner.h"
#include "test/core/promise/poll_matcher.h"
#include "test/core/test_util/test_config.h"
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

class MockTransportFilter {
 public:
  struct State {
    struct RawPointerChannelArgTag {};
    static absl::string_view ChannelArgName() {
      return "grpc.test.v3_bridge_mock_transport";
    }
    CallCombiner* call_combiner = nullptr;
    grpc_metadata_batch* recv_trailing_metadata = nullptr;
    grpc_closure* recv_trailing_metadata_ready = nullptr;
  };

  static const grpc_channel_filter kFilter;

 private:
  static void StartBatch(grpc_call_element* elem,
                         grpc_transport_stream_op_batch* op) {
    auto* state = *static_cast<State**>(elem->channel_data);
    if (op->recv_trailing_metadata) {
      state->recv_trailing_metadata =
          op->payload->recv_trailing_metadata.recv_trailing_metadata;
      state->recv_trailing_metadata_ready =
          op->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    }
    if (op->on_complete != nullptr) {
      GRPC_CALL_COMBINER_START(state->call_combiner, op->on_complete,
                               absl::OkStatus(), "mock_on_complete");
    }
    GRPC_CALL_COMBINER_STOP(state->call_combiner,
                            "mock passed batch to transport");
  }
  static void StartTransportOp(grpc_channel_element*, grpc_transport_op* op) {
    if (op->on_consumed != nullptr) {
      ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, absl::OkStatus());
    }
  }
  static grpc_error_handle InitCallElem(grpc_call_element*,
                                        const grpc_call_element_args*) {
    return absl::OkStatus();
  }
  static void DestroyCallElem(grpc_call_element*, const grpc_call_final_info*,
                              grpc_closure*) {}
  static grpc_error_handle InitChannelElem(grpc_channel_element* elem,
                                           grpc_channel_element_args* args) {
    *static_cast<State**>(elem->channel_data) =
        args->channel_args.GetObject<State>();
    return absl::OkStatus();
  }
  static void DestroyChannelElem(grpc_channel_element*) {}
};

const grpc_channel_filter MockTransportFilter::kFilter = {
    MockTransportFilter::StartBatch,
    MockTransportFilter::StartTransportOp,
    0,
    MockTransportFilter::InitCallElem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    MockTransportFilter::DestroyCallElem,
    sizeof(MockTransportFilter::State*),
    MockTransportFilter::InitChannelElem,
    grpc_channel_stack_no_post_init,
    MockTransportFilter::DestroyChannelElem,
    grpc_channel_next_get_info,
    GRPC_UNIQUE_TYPE_NAME_HERE("v3_bridge_mock_transport"),
};

struct StartBatchCtx {
  grpc_call_element* elem = nullptr;
  grpc_transport_stream_op_batch* batch = nullptr;
};

void DoStartBatch(void* arg, grpc_error_handle) {
  auto* ctx = static_cast<StartBatchCtx*>(arg);
  ctx->elem->filter->start_transport_stream_op_batch(ctx->elem, ctx->batch);
}

void OnCompleteStopCombiner(void* arg, grpc_error_handle) {
  GRPC_CALL_COMBINER_STOP(static_cast<CallCombiner*>(arg), "app:on_complete");
}

// Records what the v3 interceptor sees on the client-to-server path. The v3
// call parties may run on event engine threads, so access is guarded by a
// mutex.
struct HalfCloseRecorder {
  struct RawPointerChannelArgTag {};
  static absl::string_view ChannelArgName() {
    return "grpc.test.half_close_recorder";
  }
  std::vector<std::string> Events() {
    MutexLock lock(&mu);
    return events;
  }
  void Add(std::string event) {
    MutexLock lock(&mu);
    events.push_back(std::move(event));
  }
  Mutex mu;
  std::vector<std::string> events ABSL_GUARDED_BY(mu);
  absl::Notification got_message;
  absl::Notification half_closed;
};

// A v3 interceptor run through V3InterceptorToV2Bridge. It forwards the call
// to the next v2 filter and records each client-to-server message and the
// client half-close.
class HalfCloseRecordingInterceptor final
    : public V3InterceptorToV2Bridge<HalfCloseRecordingInterceptor> {
 public:
  static const grpc_channel_filter kFilter;

  explicit HalfCloseRecordingInterceptor(const ChannelArgs& args)
      : V3InterceptorToV2Bridge<HalfCloseRecordingInterceptor>(args),
        recorder_(args.GetObject<HalfCloseRecorder>()) {}

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
    HalfCloseRecorder* recorder = recorder_;
    handler.SpawnInfallible(
        "record_client_to_server", [handler, initiator, recorder]() mutable {
          return Seq(ForEach(MessagesFrom(handler),
                             [initiator, recorder](MessageHandle msg) mutable {
                               recorder->Add(absl::StrCat(
                                   "msg:", msg->payload()->JoinIntoString()));
                               recorder->got_message.Notify();
                               initiator.SpawnPushMessage(std::move(msg));
                               return Success{};
                             }),
                     [initiator, recorder](StatusFlag status) mutable {
                       // A clean end of the message stream is the client
                       // half-close.
                       if (status.ok()) {
                         recorder->Add("half_close");
                         recorder->half_closed.Notify();
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

  HalfCloseRecorder* recorder_;
};

const grpc_channel_filter HalfCloseRecordingInterceptor::kFilter =
    MakePromiseBasedFilter<
        HalfCloseRecordingInterceptor, FilterEndpoint::kClient,
        kFilterExaminesServerInitialMetadata | kFilterExaminesOutboundMessages |
            kFilterExaminesInboundMessages>();

// Flushes the ExecCtx until `n` is notified or the timeout expires. Work on
// the v3 side may hop to event engine threads and back, so a single flush is
// not enough.
bool FlushUntil(absl::Notification& n,
                absl::Duration timeout = absl::Seconds(5)) {
  const absl::Time deadline = absl::Now() + timeout;
  while (absl::Now() < deadline) {
    ExecCtx::Get()->Flush();
    if (n.WaitForNotificationWithTimeout(absl::Milliseconds(1))) return true;
  }
  return n.HasBeenNotified();
}

struct RecvTrailingReadyArg {
  CallCombiner* call_combiner;
  absl::Notification done;
};

void RecvTrailingReady(void* arg, grpc_error_handle) {
  auto* a = static_cast<RecvTrailingReadyArg*>(arg);
  GRPC_CALL_COMBINER_STOP(a->call_combiner, "app:recv_trailing_ready");
  a->done.Notify();
}

// A v2 channel stack with HalfCloseRecordingInterceptor on top of the mock
// transport, and one call on it. The tests check that a client half-close sent
// on the v2 call reaches the v3 interceptor through V3InterceptorToV2Bridge,
// and only after the last message.
class ClientHalfClosePropagationTest : public ::testing::Test {
 protected:
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
        grpc_event_engine::experimental::GetDefaultEventEngine().get());
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
    recv_trailing_arg_.call_combiner = &call_combiner_;
    GRPC_CLOSURE_INIT(&recv_trailing_ready_, RecvTrailingReady,
                      &recv_trailing_arg_, nullptr);
  }

  ~ClientHalfClosePropagationTest() override {
    GRPC_CALL_STACK_UNREF(call_stack_, "done");
    ExecCtx::Get()->Flush();
    GRPC_CHANNEL_STACK_UNREF(channel_stack_, "done");
  }

  grpc_call_element* top() { return grpc_call_stack_element(call_stack_, 0); }

  void StartBatch(StartBatchCtx* ctx, grpc_closure* start_closure) {
    GRPC_CLOSURE_INIT(start_closure, DoStartBatch, ctx, nullptr);
    GRPC_CALL_COMBINER_START(&call_combiner_, start_closure, absl::OkStatus(),
                             "start_batch");
  }

  // Sends OK trailing metadata up from the mock transport and waits for the
  // call to finish.
  void FinishCall() {
    recv_trailing_md_.Set(GrpcStatusMetadata(), GRPC_STATUS_OK);
    GRPC_CALL_COMBINER_START(&call_combiner_,
                             transport_state_.recv_trailing_metadata_ready,
                             absl::OkStatus(), "finish_call");
    EXPECT_TRUE(FlushUntil(recv_trailing_arg_.done));
  }

  ExecCtx exec_ctx_;
  HalfCloseRecorder recorder_;
  CallCombiner call_combiner_;
  MockTransportFilter::State transport_state_;
  RefCountedPtr<Arena> arena_ = SimpleArenaAllocator()->MakeArena();
  grpc_channel_stack* channel_stack_ = nullptr;
  grpc_call_stack* call_stack_ = nullptr;
  grpc_metadata_batch client_initial_md_;
  grpc_metadata_batch client_trailing_md_;
  grpc_metadata_batch recv_trailing_md_;
  RecvTrailingReadyArg recv_trailing_arg_;
  grpc_closure recv_trailing_ready_;
};

TEST_F(ClientHalfClosePropagationTest,
       HalfCloseInSameBatchAsSendMessagePropagatesAfterSendMessageCompletes) {
  SliceBuffer send_msg_buf;
  send_msg_buf.Append(Slice::FromCopiedString("hello"));
  grpc_closure on_complete;
  GRPC_CLOSURE_INIT(&on_complete, OnCompleteStopCombiner, &call_combiner_,
                    nullptr);
  grpc_transport_stream_op_batch_payload payload{};
  grpc_transport_stream_op_batch batch{};
  batch.payload = &payload;
  batch.send_initial_metadata = true;
  payload.send_initial_metadata.send_initial_metadata = &client_initial_md_;
  batch.send_message = true;
  payload.send_message.send_message = &send_msg_buf;
  batch.send_trailing_metadata = true;
  payload.send_trailing_metadata.send_trailing_metadata = &client_trailing_md_;
  batch.recv_trailing_metadata = true;
  payload.recv_trailing_metadata.recv_trailing_metadata = &recv_trailing_md_;
  payload.recv_trailing_metadata.recv_trailing_metadata_ready =
      &recv_trailing_ready_;
  batch.on_complete = &on_complete;
  StartBatchCtx start_ctx{top(), &batch};
  grpc_closure start_closure;
  StartBatch(&start_ctx, &start_closure);
  ASSERT_TRUE(FlushUntil(recorder_.half_closed));
  EXPECT_THAT(recorder_.Events(),
              ::testing::ElementsAre("msg:hello", "half_close"));
  FinishCall();
}

TEST_F(ClientHalfClosePropagationTest,
       HalfCloseInSeparateBatchPropagatesImmediately) {
  SliceBuffer send_msg_buf;
  send_msg_buf.Append(Slice::FromCopiedString("msg1"));
  grpc_closure on_complete1;
  GRPC_CLOSURE_INIT(&on_complete1, OnCompleteStopCombiner, &call_combiner_,
                    nullptr);
  grpc_transport_stream_op_batch_payload payload1{};
  grpc_transport_stream_op_batch batch1{};
  batch1.payload = &payload1;
  batch1.send_initial_metadata = true;
  payload1.send_initial_metadata.send_initial_metadata = &client_initial_md_;
  batch1.send_message = true;
  payload1.send_message.send_message = &send_msg_buf;
  batch1.recv_trailing_metadata = true;
  payload1.recv_trailing_metadata.recv_trailing_metadata = &recv_trailing_md_;
  payload1.recv_trailing_metadata.recv_trailing_metadata_ready =
      &recv_trailing_ready_;
  batch1.on_complete = &on_complete1;
  StartBatchCtx start_ctx1{top(), &batch1};
  grpc_closure start_closure1;
  StartBatch(&start_ctx1, &start_closure1);
  ASSERT_TRUE(FlushUntil(recorder_.got_message));
  // No half-close was sent yet, so v3 must not see one.
  absl::Notification never;
  FlushUntil(never, absl::Milliseconds(100));
  EXPECT_FALSE(recorder_.half_closed.HasBeenNotified());
  EXPECT_THAT(recorder_.Events(), ::testing::ElementsAre("msg:msg1"));
  grpc_closure on_complete2;
  GRPC_CLOSURE_INIT(&on_complete2, OnCompleteStopCombiner, &call_combiner_,
                    nullptr);
  grpc_transport_stream_op_batch_payload payload2{};
  grpc_transport_stream_op_batch batch2{};
  batch2.payload = &payload2;
  batch2.send_trailing_metadata = true;
  payload2.send_trailing_metadata.send_trailing_metadata = &client_trailing_md_;
  batch2.on_complete = &on_complete2;
  StartBatchCtx start_ctx2{top(), &batch2};
  grpc_closure start_closure2;
  StartBatch(&start_ctx2, &start_closure2);
  ASSERT_TRUE(FlushUntil(recorder_.half_closed));
  EXPECT_THAT(recorder_.Events(),
              ::testing::ElementsAre("msg:msg1", "half_close"));
  FinishCall();
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_core::ForceEnableExperiment("v2_non_owning_waker_implementation", true);
  grpc_core::ForceEnableExperiment("promise_filter_client_half_close", true);
  return RUN_ALL_TESTS();
}
