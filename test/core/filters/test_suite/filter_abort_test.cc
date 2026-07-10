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

// M2 gate test (failure path): a filter that aborts a call at its very first
// interception point. Returning a ServerMetadataHandle from
// OnClientInitialMetadata terminates the call before it reaches the server, so
// the client observes the filter's status directly. This is the cleanest form
// of the "filter rejects a call" contract and is robust under any fuzzed
// schedule (there is no server-side work to drain).

#include <grpc/status.h>

#include <memory>

#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/call/metadata.h"
#include "src/core/channelz/property_list.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "test/core/filters/test_suite/filter_matchers.h"
#include "test/core/filters/test_suite/filter_test.h"

namespace grpc_core {

namespace {

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

}  // namespace

FILTER_TEST_V3(FilterRejectsAtInitialMetadata) {
  ASSERT_TRUE(Add<RejectingFilter>().Build().ok());

  auto initiator = StartCall(NewClientMetadata());
  SpawnTestSeq(
      initiator, "client",
      [initiator]() mutable { return initiator.PullServerTrailingMetadata(); },
      [](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_THAT(**md, HasMetadataResult(absl::PermissionDeniedError(
                              "rejected by filter")));
      });

  // Filters only execute as the server pulls: OnClientInitialMetadata runs when
  // the handler pulls the client initial metadata. Drive that pull so the
  // filter fires; because the filter aborts, the pull resolves to failure.
  auto handler = TickUntilServerCall();
  SpawnTestSeq(
      handler, "server",
      [handler]() mutable { return handler.PullClientInitialMetadata(); },
      [](ValueOrFailure<ClientMetadataHandle> md) { EXPECT_FALSE(md.ok()); });

  WaitForAllPendingWork();
}

}  // namespace grpc_core
