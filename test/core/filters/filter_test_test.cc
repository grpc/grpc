// Copyright 2023 gRPC authors.
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

// Tests for the filter test harnesses themselves -- FilterTest (v3) and
// FilterTestV2 -- using synthetic filters and interceptors defined right here,
// so every assertion is about harness plumbing rather than any particular
// filter's behavior. Both live here for the same reason both harnesses live in
// filter_test.{h,cc}: the v2 half goes away wholesale once there are no v2
// filters left.

#include "test/core/filters/filter_test.h"

#include <grpc/compression.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/status.h>

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "src/core/call/interception_chain.h"
#include "src/core/call/message.h"
#include "src/core/call/metadata.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/channelz/property_list.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/filters/filter_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

using ::testing::_;

namespace grpc_core {

////////////////////////////////////////////////////////////////////////////////
// FilterTest (v3 filter API)

namespace {

// A filter that intercepts nothing: every event passes through untouched. It
// exists solely to place a real filter in the call path so the harness
// exercises the genuine CallFilters executor.
class PassThroughFilter : public ImplementChannelFilter<PassThroughFilter> {
 public:
  static absl::string_view TypeName() { return "pass_through"; }

  static absl::StatusOr<std::unique_ptr<PassThroughFilter>> Create(
      const ChannelArgs&, ChannelFilter::Args) {
    return std::make_unique<PassThroughFilter>();
  }

  class Call {
   public:
    static inline const NoInterceptor OnClientInitialMetadata;
    static inline const NoInterceptor OnServerInitialMetadata;
    static inline const NoInterceptor OnServerTrailingMetadata;
    static inline const NoInterceptor OnClientToServerMessage;
    static inline const NoInterceptor OnClientToServerHalfClose;
    static inline const NoInterceptor OnServerToClientMessage;
    static inline const NoInterceptor OnFinalize;
    channelz::PropertyList ChannelzProperties() { return {}; }
  };
};

// A promise that is Pending{} on its first poll and resolves on the next one,
// arranging its own wakeup through the event engine. Used to make every hook of
// AsyncFilter genuinely asynchronous, so the harness has to suspend and resume
// each of them.
template <typename T>
class ResolveAfterOnePending {
 public:
  explicit ResolveAfterOnePending(T value) : value_(std::move(value)) {}

  Poll<T> operator()() {
    if (polled_) return std::move(value_);
    polled_ = true;
    GetContext<grpc_event_engine::experimental::EventEngine>()->Run(
        [waker = GetContext<Activity>()->MakeOwningWaker()]() mutable {
          waker.Wakeup();
        });
    return Pending{};
  }

 private:
  T value_;
  bool polled_ = false;
};

ResolveAfterOnePending<absl::Status> PendOnce() {
  return ResolveAfterOnePending<absl::Status>(absl::OkStatus());
}

// A filter that does asynchronous work at each of its interception points that
// can do any: every hook below returns a promise that yields Pending{} the
// first time it is polled.
//
// OnClientToServerHalfClose and OnServerTrailingMetadata are deliberately
// synchronous: the v3 filter API has no promise-returning form for those two
// hooks (see AddHalfClose/AddServerTrailingMetadata in call_filters.h), so
// there is nothing to exercise there.
//
// Note this is a plain filter rather than an ImplementChannelFilter: the v2
// bridge that ImplementChannelFilter provides does not support
// promise-returning hooks.
class AsyncFilter {
 public:
  static absl::StatusOr<std::unique_ptr<AsyncFilter>> Create(
      const ChannelArgs&, const FilterArgs&) {
    return std::make_unique<AsyncFilter>();
  }

  using PendingStatus = ResolveAfterOnePending<absl::Status>;

  class Call {
   public:
    PendingStatus OnClientInitialMetadata(ClientMetadata&) {
      return PendOnce();
    }
    PendingStatus OnServerInitialMetadata(ServerMetadata&) {
      return PendOnce();
    }
    PendingStatus OnClientToServerMessage(Message&) { return PendOnce(); }
    PendingStatus OnServerToClientMessage(Message&) { return PendOnce(); }
    void OnClientToServerHalfClose() {}
    void OnServerTrailingMetadata(ServerMetadata&) {}
    static inline const NoInterceptor OnFinalize;
    channelz::PropertyList ChannelzProperties() { return {}; }
  };
};

// A filter that rejects every call at initial metadata.
class RejectingFilter : public ImplementChannelFilter<RejectingFilter> {
 public:
  static absl::string_view TypeName() { return "rejecting"; }

  static absl::StatusOr<std::unique_ptr<RejectingFilter>> Create(
      const ChannelArgs&, ChannelFilter::Args) {
    return std::make_unique<RejectingFilter>();
  }

  class Call {
   public:
    ServerMetadataHandle OnClientInitialMetadata(ClientMetadata&) {
      return ServerMetadataFromStatus(GRPC_STATUS_PERMISSION_DENIED,
                                      "rejected by filter");
    }
    static inline const NoInterceptor OnServerInitialMetadata;
    static inline const NoInterceptor OnServerTrailingMetadata;
    static inline const NoInterceptor OnClientToServerMessage;
    static inline const NoInterceptor OnClientToServerHalfClose;
    static inline const NoInterceptor OnServerToClientMessage;
    static inline const NoInterceptor OnFinalize;
    channelz::PropertyList ChannelzProperties() { return {}; }
  };
};

// An interceptor that passes every call down the chain unchanged: exactly one
// child call per intercepted call.
class PassThroughInterceptor final : public Interceptor {
 public:
  static absl::StatusOr<RefCountedPtr<PassThroughInterceptor>> Create(
      const ChannelArgs&, const FilterArgs&) {
    return MakeRefCounted<PassThroughInterceptor>();
  }

  void Orphaned() override {}

 protected:
  void InterceptCall(UnstartedCallHandler unstarted_call_handler) override {
    PassThrough(std::move(unstarted_call_handler));
  }
};

// An interceptor that answers every call itself and never passes it on: zero
// child calls, so no handler is ever produced for the test to collect.
class ConsumingInterceptor final : public Interceptor {
 public:
  static absl::StatusOr<RefCountedPtr<ConsumingInterceptor>> Create(
      const ChannelArgs&, const FilterArgs&) {
    return MakeRefCounted<ConsumingInterceptor>();
  }

  void Orphaned() override {}

 protected:
  void InterceptCall(UnstartedCallHandler unstarted_call_handler) override {
    Consume(std::move(unstarted_call_handler))
        .PushServerTrailingMetadata(ServerMetadataFromStatus(
            GRPC_STATUS_UNIMPLEMENTED, "consumed by interceptor"));
  }
};

}  // namespace

// Drives all six call operations through a transparent filter. The pushes and
// pulls are interleaved rather than issued as two blocks, because a push does
// not complete until the far end pulls: pushing server trailing metadata while
// a message push is still outstanding would race the message.
FILTER_TEST(FilterTest, UnaryEchoThroughPassThroughFilter) {
  ASSERT_TRUE(InitChannel<PassThroughFilter>().ok());
  auto [initiator, handler] =
      StartCallForFilter(NewClientMetadata({{"echo-test", "on"}}));

  // Client sends the request.
  PushClientMessage(NewMessage("hello"));
  PushClientHalfClose();

  // Server receives it.
  ValueOrFailure<ClientMetadataHandle> client_initial_metadata =
      PullClientInitialMetadata(handler);
  ASSERT_TRUE(client_initial_metadata.ok());
  EXPECT_THAT(**client_initial_metadata,
              HasMetadataKeyValue("echo-test", "on"));
  ClientToServerNextMessage request = PullClientMessage(handler);
  ASSERT_TRUE(request.ok());
  ASSERT_TRUE(request.has_value());
  EXPECT_THAT(request.value(), HasMessagePayload("hello"));
  EXPECT_TRUE(PullClientHalfClose(handler));

  // Server responds.
  PushServerInitialMetadata(handler,
                            NewServerMetadata({{"server-hdr", "yes"}}));
  PushServerMessage(handler, NewMessage("hello"));

  // Client receives the response.
  ValueOrFailure<std::optional<ServerMetadataHandle>> server_initial_metadata =
      PullServerInitialMetadata();
  ASSERT_TRUE(server_initial_metadata.ok());
  ASSERT_TRUE(server_initial_metadata->has_value());
  EXPECT_THAT(***server_initial_metadata,
              HasMetadataKeyValue("server-hdr", "yes"));
  ServerToClientNextMessage response = PullServerMessage();
  ASSERT_TRUE(response.ok());
  ASSERT_TRUE(response.has_value());
  EXPECT_THAT(response.value(), HasMessagePayload("hello"));

  // Server finishes, client observes the status.
  PushServerTrailingMetadata(handler, ServerMetadataFromStatus(GRPC_STATUS_OK));
  ValueOrFailure<ServerMetadataHandle> server_trailing_metadata =
      PullServerTrailingMetadata();
  ASSERT_TRUE(server_trailing_metadata.ok());
  EXPECT_THAT(**server_trailing_metadata, HasMetadataResult(absl::OkStatus()));

  WaitForAllPendingWork();
}

// The same unary RPC, but every hook of the filter under test suspends once
// before completing. Nothing about the test changes: the harness resumes each
// hook and all six operations still arrive.
FILTER_TEST(FilterTest, UnaryEchoThroughAsyncFilter) {
  ASSERT_TRUE(InitChannel<AsyncFilter>().ok());
  auto [initiator, handler] =
      StartCallForFilter(NewClientMetadata({{"echo-test", "on"}}));

  PushClientMessage(NewMessage("hello"));
  PushClientHalfClose();

  ValueOrFailure<ClientMetadataHandle> client_initial_metadata =
      PullClientInitialMetadata(handler);
  ASSERT_TRUE(client_initial_metadata.ok());
  EXPECT_THAT(**client_initial_metadata,
              HasMetadataKeyValue("echo-test", "on"));
  ClientToServerNextMessage request = PullClientMessage(handler);
  ASSERT_TRUE(request.ok());
  ASSERT_TRUE(request.has_value());
  EXPECT_THAT(request.value(), HasMessagePayload("hello"));
  EXPECT_TRUE(PullClientHalfClose(handler));

  PushServerInitialMetadata(handler,
                            NewServerMetadata({{"server-hdr", "yes"}}));
  PushServerMessage(handler, NewMessage("hello"));

  ValueOrFailure<std::optional<ServerMetadataHandle>> server_initial_metadata =
      PullServerInitialMetadata();
  ASSERT_TRUE(server_initial_metadata.ok());
  ASSERT_TRUE(server_initial_metadata->has_value());
  EXPECT_THAT(***server_initial_metadata,
              HasMetadataKeyValue("server-hdr", "yes"));
  ServerToClientNextMessage response = PullServerMessage();
  ASSERT_TRUE(response.ok());
  ASSERT_TRUE(response.has_value());
  EXPECT_THAT(response.value(), HasMessagePayload("hello"));

  PushServerTrailingMetadata(handler, ServerMetadataFromStatus(GRPC_STATUS_OK));
  ValueOrFailure<ServerMetadataHandle> server_trailing_metadata =
      PullServerTrailingMetadata();
  ASSERT_TRUE(server_trailing_metadata.ok());
  EXPECT_THAT(**server_trailing_metadata, HasMetadataResult(absl::OkStatus()));

  WaitForAllPendingWork();
}

// Failure path: a filter that aborts a call at its very first interception
// point. Returning a ServerMetadataHandle from OnClientInitialMetadata
// terminates the call before it reaches the server, so the client observes the
// filter's status directly.
FILTER_TEST(FilterTest, FilterRejectsAtInitialMetadata) {
  ASSERT_TRUE(InitChannel<RejectingFilter>().ok());
  auto [initiator, handler] = StartCallForFilter(NewClientMetadata());

  // Filters only execute as the server pulls: driving the pull fires the
  // filter, and because the filter aborts, the pull resolves to failure.
  EXPECT_FALSE(PullClientInitialMetadata(handler).ok());

  ValueOrFailure<ServerMetadataHandle> server_trailing_metadata =
      PullServerTrailingMetadata();
  ASSERT_TRUE(server_trailing_metadata.ok());
  EXPECT_THAT(
      **server_trailing_metadata,
      HasMetadataResult(absl::PermissionDeniedError("rejected by filter")));

  WaitForAllPendingWork();
}

// An interceptor in the stack works just like a filter does, because it starts
// exactly one child call.
FILTER_TEST(FilterTest, UnaryEchoThroughPassThroughInterceptor) {
  ASSERT_TRUE(InitChannel<PassThroughInterceptor>().ok());
  auto [initiator, handler] =
      StartCallForFilter(NewClientMetadata({{"echo-test", "on"}}));

  PushClientMessage(NewMessage("hello"));
  PushClientHalfClose();

  ValueOrFailure<ClientMetadataHandle> client_initial_metadata =
      PullClientInitialMetadata(handler);
  ASSERT_TRUE(client_initial_metadata.ok());
  EXPECT_THAT(**client_initial_metadata,
              HasMetadataKeyValue("echo-test", "on"));
  ClientToServerNextMessage request = PullClientMessage(handler);
  ASSERT_TRUE(request.ok());
  ASSERT_TRUE(request.has_value());
  EXPECT_THAT(request.value(), HasMessagePayload("hello"));
  EXPECT_TRUE(PullClientHalfClose(handler));

  PushServerTrailingMetadata(handler, ServerMetadataFromStatus(GRPC_STATUS_OK));
  ValueOrFailure<ServerMetadataHandle> server_trailing_metadata =
      PullServerTrailingMetadata();
  ASSERT_TRUE(server_trailing_metadata.ok());
  EXPECT_THAT(**server_trailing_metadata, HasMetadataResult(absl::OkStatus()));

  WaitForAllPendingWork();
}

// An interceptor that consumes the call creates *no* child call, so there is no
// handler to collect: this is why StartCall() hands back only the initiator.
FILTER_TEST(FilterTest, ConsumingInterceptorCreatesNoChildCall) {
  ASSERT_TRUE(InitChannel<ConsumingInterceptor>().ok());
  StartCall(NewClientMetadata());

  ValueOrFailure<ServerMetadataHandle> server_trailing_metadata =
      PullServerTrailingMetadata();
  ASSERT_TRUE(server_trailing_metadata.ok());
  EXPECT_THAT(
      **server_trailing_metadata,
      HasMetadataResult(absl::UnimplementedError("consumed by interceptor")));

  WaitForAllPendingWork();
}

////////////////////////////////////////////////////////////////////////////////
// FilterTestV2 (v2 filter API)

namespace {

class NoOpFilter final : public ChannelFilter {
 public:
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs args, NextPromiseFactory next) override {
    return next(std::move(args));
  }

  static absl::StatusOr<std::unique_ptr<NoOpFilter>> Create(
      const ChannelArgs&, ChannelFilter::Args) {
    return std::make_unique<NoOpFilter>();
  }
};
using NoOpFilterTest = FilterTestV2<NoOpFilter>;

class DelayStartFilter final : public ChannelFilter {
 public:
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs args, NextPromiseFactory next) override {
    return Seq(
        [args = std::move(args), i = 10]() mutable -> Poll<CallArgs> {
          --i;
          if (i == 0) return std::move(args);
          GetContext<Activity>()->ForceImmediateRepoll();
          return Pending{};
        },
        std::move(next));
  }

  static absl::StatusOr<std::unique_ptr<DelayStartFilter>> Create(
      const ChannelArgs&, ChannelFilter::Args) {
    return std::make_unique<DelayStartFilter>();
  }
};
using DelayStartFilterTest = FilterTestV2<DelayStartFilter>;

class AddClientInitialMetadataFilter final : public ChannelFilter {
 public:
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs args, NextPromiseFactory next) override {
    args.client_initial_metadata->Set(HttpPathMetadata(),
                                      Slice::FromCopiedString("foo.bar"));
    return next(std::move(args));
  }

  static absl::StatusOr<std::unique_ptr<AddClientInitialMetadataFilter>> Create(
      const ChannelArgs&, ChannelFilter::Args) {
    return absl::make_unique<AddClientInitialMetadataFilter>();
  }
};
using AddClientInitialMetadataFilterTest =
    FilterTestV2<AddClientInitialMetadataFilter>;

class AddServerTrailingMetadataFilter final : public ChannelFilter {
 public:
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs args, NextPromiseFactory next) override {
    return Map(next(std::move(args)), [](ServerMetadataHandle handle) {
      handle->Set(HttpStatusMetadata(), 420);
      return handle;
    });
  }

  static absl::StatusOr<std::unique_ptr<AddServerTrailingMetadataFilter>>
  Create(const ChannelArgs&, ChannelFilter::Args) {
    return absl::make_unique<AddServerTrailingMetadataFilter>();
  }
};
using AddServerTrailingMetadataFilterTest =
    FilterTestV2<AddServerTrailingMetadataFilter>;

class AddServerInitialMetadataFilter final : public ChannelFilter {
 public:
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs args, NextPromiseFactory next) override {
    args.server_initial_metadata->InterceptAndMap([](ServerMetadataHandle md) {
      md->Set(GrpcEncodingMetadata(), GRPC_COMPRESS_GZIP);
      return md;
    });
    return next(std::move(args));
  }
  static absl::StatusOr<std::unique_ptr<AddServerInitialMetadataFilter>> Create(
      const ChannelArgs&, ChannelFilter::Args) {
    return absl::make_unique<AddServerInitialMetadataFilter>();
  }
};
using AddServerInitialMetadataFilterTest =
    FilterTestV2<AddServerInitialMetadataFilter>;

TEST_F(NoOpFilterTest, NoOp) {}

TEST_F(NoOpFilterTest, MakeCall) {
  Call call(MakeChannel(ChannelArgs()).value());
}

TEST_F(NoOpFilterTest, MakeClientMetadata) {
  Call call(MakeChannel(ChannelArgs()).value());
  auto md = call.NewClientMetadata({{":path", "foo.bar"}});
  EXPECT_EQ(md->get_pointer(HttpPathMetadata())->as_string_view(), "foo.bar");
}

TEST_F(NoOpFilterTest, MakeServerMetadata) {
  Call call(MakeChannel(ChannelArgs()).value());
  auto md = call.NewServerMetadata({{":status", "200"}});
  EXPECT_EQ(md->get(HttpStatusMetadata()), HttpStatusMetadata::ValueType(200));
}

TEST_F(NoOpFilterTest, CanStart) {
  Call call(MakeChannel(ChannelArgs()).value());
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata());
  Step();
}

TEST_F(DelayStartFilterTest, CanStartWithDelay) {
  Call call(MakeChannel(ChannelArgs()).value());
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata());
  Step();
}

TEST_F(NoOpFilterTest, CanCancel) {
  Call call(MakeChannel(ChannelArgs()).value());
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata());
  call.Cancel();
}

TEST_F(DelayStartFilterTest, CanCancelWithDelay) {
  Call call(MakeChannel(ChannelArgs()).value());
  call.Start(call.NewClientMetadata());
  call.Cancel();
}

TEST_F(AddClientInitialMetadataFilterTest, CanSetClientInitialMetadata) {
  Call call(MakeChannel(ChannelArgs()).value());
  EXPECT_EVENT(Started(&call, HasMetadataKeyValue(":path", "foo.bar")));
  call.Start(call.NewClientMetadata());
  Step();
}

TEST_F(NoOpFilterTest, CanFinish) {
  Call call(MakeChannel(ChannelArgs()).value());
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata());
  call.FinishNextFilter(call.NewServerMetadata());
  EXPECT_EVENT(Finished(&call, _));
  Step();
}

TEST_F(AddServerTrailingMetadataFilterTest, CanSetServerTrailingMetadata) {
  Call call(MakeChannel(ChannelArgs()).value());
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata());
  call.FinishNextFilter(call.NewServerMetadata());
  EXPECT_EVENT(Finished(&call, HasMetadataKeyValue(":status", "420")));
  Step();
}

TEST_F(NoOpFilterTest, CanProcessServerInitialMetadata) {
  Call call(MakeChannel(ChannelArgs()).value());
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata());
  call.ForwardServerInitialMetadata(call.NewServerMetadata());
  EXPECT_EVENT(ForwardedServerInitialMetadata(&call, _));
  Step();
}

TEST_F(AddServerInitialMetadataFilterTest, CanSetServerInitialMetadata) {
  Call call(MakeChannel(ChannelArgs()).value());
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata());
  call.ForwardServerInitialMetadata(call.NewServerMetadata());
  EXPECT_EVENT(ForwardedServerInitialMetadata(
      &call, HasMetadataKeyValue("grpc-encoding", "gzip")));
  Step();
}

TEST_F(NoOpFilterTest, CanProcessClientToServerMessage) {
  Call call(MakeChannel(ChannelArgs()).value());
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata());
  call.ForwardMessageClientToServer(call.NewMessage("abc"));
  EXPECT_CALL(events,
              ForwardedMessageClientToServer(&call, HasMessagePayload("abc")));
  Step();
}

TEST_F(NoOpFilterTest, CanProcessServerToClientMessage) {
  Call call(MakeChannel(ChannelArgs()).value());
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata());
  call.ForwardServerInitialMetadata(call.NewServerMetadata());
  call.ForwardMessageServerToClient(call.NewMessage("abc"));
  EXPECT_EVENT(ForwardedServerInitialMetadata(&call, _));
  EXPECT_CALL(events,
              ForwardedMessageServerToClient(&call, HasMessagePayload("abc")));
  Step();
}

}  // namespace

}  // namespace grpc_core
