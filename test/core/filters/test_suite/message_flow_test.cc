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

// M2 gate test (happy path): drives all six call lifecycle events end-to-end
// through a filter, proving the full metadata + message flow works on the real
// CallSpine. Uses a transparent PassThroughFilter so the assertions are about
// the harness plumbing, not any particular filter's behavior.

#include <grpc/status.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "src/core/call/metadata.h"
#include "src/core/channelz/property_list.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/promise/status_flag.h"
#include "test/core/filters/test_suite/filter_matchers.h"
#include "test/core/filters/test_suite/filter_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace grpc_core {

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

}  // namespace

// A unary echo through a pass-through filter: client sends initial metadata + a
// message + half-close; server replies with initial metadata + an echoed
// message + OK trailers. Exercises OnClientInitialMetadata,
// OnClientToServerMessage, OnClientToServerHalfClose, OnServerInitialMetadata,
// OnServerToClientMessage and OnServerTrailingMetadata across the real spine.
//
// Ordering note: the server pulls the client's message before producing any
// output. Producing server output first would create a cyclic wait (client
// blocked pushing its message, server blocked pushing its metadata) and
// deadlock.
FILTER_TEST_V3(UnaryEchoThroughPassThroughFilter) {
  ASSERT_TRUE(Add<PassThroughFilter>().Build().ok());

  auto initiator = StartCall(NewClientMetadata({{"echo-test", "on"}}));
  SpawnTestSeq(
      initiator, "client",
      [initiator]() mutable {
        return initiator.PushMessage(NewMessage("hello"));
      },
      [initiator](StatusFlag ok) mutable {
        EXPECT_TRUE(ok.ok());
        initiator.FinishSends();
        return initiator.PullServerInitialMetadata();
      },
      [initiator](
          ValueOrFailure<std::optional<ServerMetadataHandle>> md) mutable {
        EXPECT_TRUE(md.ok());
        EXPECT_TRUE(md.value().has_value());
        EXPECT_THAT(***md, HasMetadataKeyValue("server-hdr", "yes"));
        return initiator.PullMessage();
      },
      [initiator](ServerToClientNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_TRUE(msg.has_value());
        EXPECT_THAT(msg.value(), HasMessagePayload("hello"));
        return initiator.PullMessage();
      },
      [initiator](ServerToClientNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_FALSE(msg.has_value());  // end of server->client stream
        return initiator.PullServerTrailingMetadata();
      },
      [](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_THAT(**md, HasMetadataResult(absl::OkStatus()));
      });

  auto handler = TickUntilServerCall();
  auto echoed = std::make_shared<std::string>();
  SpawnTestSeq(
      handler, "server",
      [handler]() mutable { return handler.PullClientInitialMetadata(); },
      [handler](ValueOrFailure<ClientMetadataHandle> md) mutable {
        EXPECT_TRUE(md.ok());
        EXPECT_THAT(**md, HasMetadataKeyValue("echo-test", "on"));
        // Pull the client message *before* producing any server output.
        return handler.PullMessage();
      },
      [handler, echoed](ClientToServerNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_TRUE(msg.has_value());
        EXPECT_THAT(msg.value(), HasMessagePayload("hello"));
        *echoed = msg.value().payload()->JoinIntoString();
        return handler.PushServerInitialMetadata(
            NewServerMetadata({{"server-hdr", "yes"}}));
      },
      [handler, echoed](StatusFlag ok) mutable {
        EXPECT_TRUE(ok.ok());
        return handler.PushMessage(NewMessage(*echoed));
      },
      [handler](StatusFlag ok) mutable {
        EXPECT_TRUE(ok.ok());
        return handler.PullMessage();
      },
      [handler](ClientToServerNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_FALSE(msg.has_value());  // client half-closed
        handler.PushServerTrailingMetadata(
            ServerMetadataFromStatus(GRPC_STATUS_OK));
      });

  WaitForAllPendingWork();
}

}  // namespace grpc_core
