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

// M5: multi-filter chain. Two real production filters are composed in one
// interception chain: ClientAuthorityFilter (stamps :authority on client
// initial metadata) followed by ServerMessageSizeFilter (bounds client->server
// message size). A single call exercises both: the server sees the stamped
// authority and the within-limit message flows through untouched.

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include <optional>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/call/metadata.h"
#include "src/core/ext/filters/http/client_authority_filter.h"
#include "src/core/ext/filters/message_size/message_size_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/promise/status_flag.h"
#include "test/core/filters/test_suite/filter_matchers.h"
#include "test/core/filters/test_suite/filter_test.h"

namespace grpc_core {

// ClientAuthorityFilter + ServerMessageSizeFilter composed: the call flows
// through both, the authority is stamped, and a within-limit message is
// delivered to the server.
FILTER_TEST_V3(AuthorityThenMessageSize) {
  ASSERT_TRUE(Add<ClientAuthorityFilter>()
                  .Add<ServerMessageSizeFilter>()
                  .Build(ChannelArgs()
                             .Set(GRPC_ARG_DEFAULT_AUTHORITY, "chain.test")
                             .Set(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, 1024))
                  .ok());

  auto initiator = StartCall(NewClientMetadata());
  SpawnTestSeq(
      initiator, "client",
      [initiator]() mutable {
        return initiator.PushMessage(NewMessage("payload"));
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
        return initiator.PullMessage();
      },
      [initiator](ServerToClientNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_FALSE(msg.has_value());  // server sends no message
        return initiator.PullServerTrailingMetadata();
      },
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
        // Stamped by ClientAuthorityFilter, seen after passing both filters.
        EXPECT_THAT(**md, HasMetadataKeyValue(":authority", "chain.test"));
        return handler.PullMessage();
      },
      [handler](ClientToServerNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_TRUE(msg.has_value());
        // Delivered intact: within the message-size limit.
        EXPECT_THAT(msg.value(), HasMessagePayload("payload"));
        return handler.PushServerInitialMetadata(NewServerMetadata());
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
