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

#include "src/core/ext/filters/message_size/message_size_filter.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include "src/core/call/metadata.h"
#include "src/core/lib/channel/channel_args.h"
#include "test/core/filters/filter_matchers.h"
#include "test/core/filters/filter_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace grpc_core {

class MessageSizeFilterTest : public FilterTest {
 protected:
  using FilterTest::FilterTest;

  absl::Status InitWithMaxRecvSize(int max_recv_size) {
    return InitChannel<ServerMessageSizeFilter>(
        ChannelArgs().Set(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, max_recv_size));
  }
};

// A message within the limit passes through and the call completes normally.
FILTER_TEST(MessageSizeFilterTest, WithinLimitPasses) {
  ASSERT_TRUE(InitWithMaxRecvSize(1024).ok());
  auto [initiator, handler] = StartCallForFilter(NewClientMetadata());

  PushClientMessage(NewMessage("small"));
  PushClientHalfClose();

  ASSERT_TRUE(PullClientInitialMetadata(handler).ok());
  ClientToServerNextMessage message = PullClientMessage(handler);
  ASSERT_TRUE(message.ok());
  ASSERT_TRUE(message.has_value());
  EXPECT_THAT(message.value(), HasMessagePayload("small"));
  EXPECT_TRUE(PullClientHalfClose(handler));

  PushServerTrailingMetadata(handler, ServerMetadataFromStatus(GRPC_STATUS_OK));
  ValueOrFailure<ServerMetadataHandle> server_trailing_metadata =
      PullServerTrailingMetadata();
  ASSERT_TRUE(server_trailing_metadata.ok());
  EXPECT_THAT(**server_trailing_metadata, HasMetadataResult(absl::OkStatus()));

  WaitForAllPendingWork();
}

// The limit is inclusive: a message of exactly the limit is delivered.
FILTER_TEST(MessageSizeFilterTest, AtLimitPasses) {
  ASSERT_TRUE(InitWithMaxRecvSize(5).ok());
  auto [initiator, handler] = StartCallForFilter(NewClientMetadata());

  PushClientMessage(NewMessage("exact"));
  PushClientHalfClose();

  ASSERT_TRUE(PullClientInitialMetadata(handler).ok());
  ClientToServerNextMessage message = PullClientMessage(handler);
  ASSERT_TRUE(message.ok());
  ASSERT_TRUE(message.has_value());
  EXPECT_THAT(message.value(), HasMessagePayload("exact"));
  EXPECT_TRUE(PullClientHalfClose(handler));

  PushServerTrailingMetadata(handler, ServerMetadataFromStatus(GRPC_STATUS_OK));
  ValueOrFailure<ServerMetadataHandle> server_trailing_metadata =
      PullServerTrailingMetadata();
  ASSERT_TRUE(server_trailing_metadata.ok());
  EXPECT_THAT(**server_trailing_metadata, HasMetadataResult(absl::OkStatus()));

  WaitForAllPendingWork();
}

// An oversized message is rejected: the server's pull fails rather than
// delivering it, and the client sees RESOURCE_EXHAUSTED.
FILTER_TEST(MessageSizeFilterTest, ExceedsLimitRejected) {
  ASSERT_TRUE(InitWithMaxRecvSize(4).ok());
  auto [initiator, handler] = StartCallForFilter(NewClientMetadata());

  PushClientMessage(NewMessage("much too big"));
  PushClientHalfClose();

  ASSERT_TRUE(PullClientInitialMetadata(handler).ok());
  EXPECT_FALSE(PullClientMessage(handler).ok());

  ValueOrFailure<ServerMetadataHandle> server_trailing_metadata =
      PullServerTrailingMetadata();
  ASSERT_TRUE(server_trailing_metadata.ok());
  std::optional<grpc_status_code> code =
      (**server_trailing_metadata).get(GrpcStatusMetadata());
  ASSERT_TRUE(code.has_value());
  EXPECT_EQ(*code, GRPC_STATUS_RESOURCE_EXHAUSTED);

  WaitForAllPendingWork();
}

}  // namespace grpc_core
