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

// M2 gate test (failure path): a filter that aborts a call.
// ServerMessageSizeFilter rejects a client->server message larger than the
// configured receive limit by returning error trailing metadata from
// OnClientToServerMessage; the client observes RESOURCE_EXHAUSTED rather than a
// normal completion.

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include <optional>

#include "src/core/call/message.h"
#include "src/core/call/metadata.h"
#include "src/core/ext/filters/message_size/message_size_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "test/core/filters/filter_matchers.h"
#include "test/core/filters/v3_filter_test/v3_filter_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace grpc_core {

// A message within the limit passes through and the call completes normally.
FILTER_TEST_V3(WithinLimitPasses) {
  ASSERT_TRUE(
      Add<ServerMessageSizeFilter>()
          .Build(ChannelArgs().Set(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, 1024))
          .ok());

  auto [initiator, handler] = StartCall(Arena::MakePooledForOverwrite<ClientMetadata>());
  SpawnTestSeq(
      initiator, "client",
      [initiator = initiator]() mutable {
        return initiator.PushMessage(Arena::MakePooled<Message>(SliceBuffer(Slice::FromCopiedString("small")), 0));
      },
      [initiator = initiator](StatusFlag ok) mutable {
        EXPECT_TRUE(ok.ok());
        initiator.FinishSends();
        return initiator.PullServerInitialMetadata();
      },
      [initiator = initiator](
          ValueOrFailure<std::optional<ServerMetadataHandle>> md) mutable {
        EXPECT_TRUE(md.ok());
        EXPECT_TRUE(md.value().has_value());
        return initiator.PullMessage();
      },
      [initiator = initiator](ServerToClientNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_FALSE(msg.has_value());  // server sends no message
        return initiator.PullServerTrailingMetadata();
      },
      [](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_THAT(**md, HasMetadataResult(absl::OkStatus()));
      });

  SpawnTestSeq(
      handler, "server",
      [handler = handler]() mutable { return handler.PullClientInitialMetadata(); },
      [handler = handler](ValueOrFailure<ClientMetadataHandle> md) mutable {
        EXPECT_TRUE(md.ok());
        return handler.PullMessage();
      },
      [handler = handler](ClientToServerNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_TRUE(msg.has_value());
        EXPECT_THAT(msg.value(), HasMessagePayload("small"));
        return handler.PushServerInitialMetadata(Arena::MakePooledForOverwrite<ServerMetadata>());
      },
      [handler = handler](StatusFlag ok) mutable {
        EXPECT_TRUE(ok.ok());
        return handler.PullMessage();
      },
      [handler = handler](ClientToServerNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_FALSE(msg.has_value());  // client half-closed
        handler.PushServerTrailingMetadata(
            ServerMetadataFromStatus(GRPC_STATUS_OK));
      });

  WaitForAllPendingWork();
}

}  // namespace grpc_core
