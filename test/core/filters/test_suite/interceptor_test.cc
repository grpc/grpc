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

// M3 gate test: interceptors. Unlike a filter (which observes/mutates a call in
// place), an Interceptor can take over a call at StartCall time. It may:
//   - PassThrough : forward the call unchanged to the next stage (the server).
//   - Consume     : terminate the call itself; it never reaches the server.
//   - Hijack      : take over the call and issue one or more new child calls,
//                   forwarding one of them to the server.
// FilterTestV3::Add<T> already routes Interceptor subclasses to the interceptor
// overload of InterceptionChainBuilder, so these need no new harness API.

#include <grpc/status.h>

#include <memory>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/call/call_spine.h"
#include "src/core/call/interception_chain.h"
#include "src/core/call/metadata.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/promise/map.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/filters/test_suite/filter_matchers.h"
#include "test/core/filters/test_suite/filter_test.h"

namespace grpc_core {

namespace {

// Forwards the call unchanged to the next stage.
class PassThroughInterceptor final : public Interceptor {
 public:
  void InterceptCall(UnstartedCallHandler unstarted_call_handler) override {
    PassThrough(std::move(unstarted_call_handler));
  }
  void Orphaned() override {}
  static absl::StatusOr<RefCountedPtr<PassThroughInterceptor>> Create(
      const ChannelArgs&, ChannelFilter::Args) {
    return MakeRefCounted<PassThroughInterceptor>();
  }
};

// Terminates the call with a fixed status; it never reaches the server.
class ConsumingInterceptor final : public Interceptor {
 public:
  void InterceptCall(UnstartedCallHandler unstarted_call_handler) override {
    Consume(std::move(unstarted_call_handler))
        .PushServerTrailingMetadata(
            ServerMetadataFromStatus(GRPC_STATUS_UNAVAILABLE, "consumed"));
  }
  void Orphaned() override {}
  static absl::StatusOr<RefCountedPtr<ConsumingInterceptor>> Create(
      const ChannelArgs&, ChannelFilter::Args) {
    return MakeRefCounted<ConsumingInterceptor>();
  }
};

// Hijacks the call and forwards a new child call to the next stage.
class HijackingInterceptor final : public Interceptor {
 public:
  void InterceptCall(UnstartedCallHandler unstarted_call_handler) override {
    unstarted_call_handler.SpawnInfallible(
        "hijack", [this, unstarted_call_handler]() mutable {
          return Map(Hijack(std::move(unstarted_call_handler)),
                     [](ValueOrFailure<HijackedCall> hijacked_call) {
                       ForwardCall(hijacked_call.value().original_call_handler(),
                                   hijacked_call.value().MakeCall());
                     });
        });
  }
  void Orphaned() override {}
  static absl::StatusOr<RefCountedPtr<HijackingInterceptor>> Create(
      const ChannelArgs&, ChannelFilter::Args) {
    return MakeRefCounted<HijackingInterceptor>();
  }
};

}  // namespace

// A pass-through interceptor lets the call reach the server, which completes it
// normally.
FILTER_TEST_V3(PassThroughInterceptorReachesServer) {
  ASSERT_TRUE(Add<PassThroughInterceptor>().Build().ok());

  auto initiator = StartCall(NewClientMetadata({{"probe", "1"}}));
  SpawnTestSeq(
      initiator, "client",
      [initiator]() mutable { return initiator.PullServerTrailingMetadata(); },
      [](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_THAT(**md, HasMetadataResult(absl::OkStatus()));
      });

  auto handler = TickUntilServerCall();
  SpawnTestSeq(
      handler, "server",
      [handler]() mutable { return handler.PullClientInitialMetadata(); },
      [handler](ValueOrFailure<ClientMetadataHandle> md) mutable {
        EXPECT_TRUE(md.ok());
        EXPECT_THAT(**md, HasMetadataKeyValue("probe", "1"));
        handler.PushServerTrailingMetadata(
            ServerMetadataFromStatus(GRPC_STATUS_OK));
      });

  WaitForAllPendingWork();
}

// A consuming interceptor terminates the call itself; the server is never
// reached, and the client observes the interceptor's status.
FILTER_TEST_V3(ConsumingInterceptorTerminatesCall) {
  ASSERT_TRUE(Add<ConsumingInterceptor>().Build().ok());

  auto initiator = StartCall(NewClientMetadata());
  SpawnTestSeq(
      initiator, "client",
      [initiator]() mutable { return initiator.PullServerTrailingMetadata(); },
      [](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_THAT(**md,
                    HasMetadataResult(absl::UnavailableError("consumed")));
      });

  WaitForAllPendingWork();
}

// A hijacking interceptor takes over the call and forwards a new child call to
// the server; from the client's perspective the call still completes normally,
// and the server sees the (forwarded) client initial metadata.
FILTER_TEST_V3(HijackingInterceptorForwardsToServer) {
  ASSERT_TRUE(Add<HijackingInterceptor>().Build().ok());

  auto initiator = StartCall(NewClientMetadata({{"probe", "hijack"}}));
  SpawnTestSeq(
      initiator, "client",
      [initiator]() mutable { return initiator.PullServerTrailingMetadata(); },
      [](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_THAT(**md, HasMetadataResult(absl::OkStatus()));
      });

  auto handler = TickUntilServerCall();
  SpawnTestSeq(
      handler, "server",
      [handler]() mutable { return handler.PullClientInitialMetadata(); },
      [handler](ValueOrFailure<ClientMetadataHandle> md) mutable {
        EXPECT_TRUE(md.ok());
        EXPECT_THAT(**md, HasMetadataKeyValue("probe", "hijack"));
        handler.PushServerTrailingMetadata(
            ServerMetadataFromStatus(GRPC_STATUS_OK));
      });

  WaitForAllPendingWork();
}

}  // namespace grpc_core
