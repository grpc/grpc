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

#include <string>

#include "src/core/lib/channel/channel_args.h"
#include "test/core/filters/filter_matchers.h"
#include "test/core/filters/filter_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace grpc_core {

enum class LimitConfig {
  kClientChannelArgs,
  kClientServiceConfig,
  kServerChannelArgs,
};

class MessageSizeFilterTest : public FilterTest {
 protected:
  using FilterTest::FilterTest;

  // Builds a stack per `config` with `limit` on the given direction.
  void InitLimitConfig(LimitConfig config, bool client_to_server, int limit,
                       absl::string_view service_config_field) {
    switch (config) {
      case LimitConfig::kClientChannelArgs:
        ASSERT_TRUE(
            CreateFilterChain<ClientMessageSizeFilter>(
                ChannelArgs().Set(client_to_server
                                      ? GRPC_ARG_MAX_SEND_MESSAGE_LENGTH
                                      : GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH,
                                  limit))
                .ok());
        StartCallForFilter(NewClientMetadata());
        break;
      case LimitConfig::kClientServiceConfig:
        ASSERT_TRUE(CreateFilterChain<ClientMessageSizeFilter>().ok());
        SetServiceConfig(service_config_field);
        StartCallForFilter(NewClientMetadata());
        break;
      case LimitConfig::kServerChannelArgs:
        ASSERT_TRUE(
            CreateFilterChain<ServerMessageSizeFilter>(
                ChannelArgs().Set(client_to_server
                                      ? GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH
                                      : GRPC_ARG_MAX_SEND_MESSAGE_LENGTH,
                                  limit))
                .ok());
        StartCallForFilter(NewClientMetadata());
        break;
    }
  }
};

// The limit is inclusive: a message of exactly the limit is delivered,
// regardless of which end configures it or how.
FILTER_TEST(MessageSizeFilterTest,
            ClientMessageLimitFromClientChannelArgsBelowLimitIsAllowed) {
  InitLimitConfig(LimitConfig::kClientChannelArgs, /*client_to_server=*/true,
                  /*limit=*/4, R"("maxRequestMessageBytes": 4)");

  const std::string payload(4, 'x');
  PushClientMessage(NewMessage(payload));
  PushClientHalfClose();
  ASSERT_TRUE(PullClientInitialMetadata().ok());
  ClientToServerNextMessage message = PullClientMessage();
  ASSERT_TRUE(message.ok());
  ASSERT_TRUE(message.has_value());
  EXPECT_THAT(message.value(), HasMessagePayload(payload));

  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_TRUE(PullServerTrailingStatus().ok());

  WaitForAllPendingWork();
}

FILTER_TEST(MessageSizeFilterTest,
            ClientMessageLimitFromServiceConfigBelowLimitIsAllowed) {
  InitLimitConfig(LimitConfig::kClientServiceConfig, /*client_to_server=*/true,
                  /*limit=*/4, R"("maxRequestMessageBytes": 4)");

  const std::string payload(4, 'x');
  PushClientMessage(NewMessage(payload));
  PushClientHalfClose();
  ASSERT_TRUE(PullClientInitialMetadata().ok());
  ClientToServerNextMessage message = PullClientMessage();
  ASSERT_TRUE(message.ok());
  ASSERT_TRUE(message.has_value());
  EXPECT_THAT(message.value(), HasMessagePayload(payload));

  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_TRUE(PullServerTrailingStatus().ok());

  WaitForAllPendingWork();
}

FILTER_TEST(MessageSizeFilterTest,
            ClientMessageLimitFromServerChannelArgsBelowLimitIsAllowed) {
  InitLimitConfig(LimitConfig::kServerChannelArgs, /*client_to_server=*/true,
                  /*limit=*/4, R"("maxRequestMessageBytes": 4)");

  const std::string payload(4, 'x');
  PushClientMessage(NewMessage(payload));
  PushClientHalfClose();
  ASSERT_TRUE(PullClientInitialMetadata().ok());
  ClientToServerNextMessage message = PullClientMessage();
  ASSERT_TRUE(message.ok());
  ASSERT_TRUE(message.has_value());
  EXPECT_THAT(message.value(), HasMessagePayload(payload));

  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_TRUE(PullServerTrailingStatus().ok());

  WaitForAllPendingWork();
}

FILTER_TEST(MessageSizeFilterTest,
            ClientMessageLimitFromClientChannelArgsTooLargeFails) {
  InitLimitConfig(LimitConfig::kClientChannelArgs, /*client_to_server=*/true,
                  /*limit=*/4, R"("maxRequestMessageBytes": 4)");

  const std::string oversized_payload(5, 'x');
  PushClientMessage(NewMessage(oversized_payload));
  PushClientHalfClose();
  ASSERT_TRUE(PullClientInitialMetadata().ok());
  // The filter rejected the message, so the pull fails rather than
  // delivering.
  EXPECT_FALSE(PullClientMessage().ok());
  EXPECT_EQ(PullServerTrailingStatus(),
            absl::ResourceExhaustedError(
                "CLIENT: Sent message larger than max (5 vs. 4)"));

  WaitForAllPendingWork();
}

FILTER_TEST(MessageSizeFilterTest,
            ClientMessageLimitFromServiceConfigTooLargeFails) {
  InitLimitConfig(LimitConfig::kClientServiceConfig, /*client_to_server=*/true,
                  /*limit=*/4, R"("maxRequestMessageBytes": 4)");

  const std::string oversized_payload(5, 'x');
  PushClientMessage(NewMessage(oversized_payload));
  PushClientHalfClose();
  ASSERT_TRUE(PullClientInitialMetadata().ok());
  // The filter rejected the message, so the pull fails rather than
  // delivering.
  EXPECT_FALSE(PullClientMessage().ok());
  EXPECT_EQ(PullServerTrailingStatus(),
            absl::ResourceExhaustedError(
                "CLIENT: Sent message larger than max (5 vs. 4)"));

  WaitForAllPendingWork();
}

FILTER_TEST(MessageSizeFilterTest,
            ClientMessageLimitFromServerChannelArgsTooLargeFails) {
  InitLimitConfig(LimitConfig::kServerChannelArgs, /*client_to_server=*/true,
                  /*limit=*/4, R"("maxRequestMessageBytes": 4)");

  const std::string oversized_payload(5, 'x');
  PushClientMessage(NewMessage(oversized_payload));
  PushClientHalfClose();
  ASSERT_TRUE(PullClientInitialMetadata().ok());
  // The filter rejected the message, so the pull fails rather than
  // delivering.
  EXPECT_FALSE(PullClientMessage().ok());
  EXPECT_EQ(PullServerTrailingStatus(),
            absl::ResourceExhaustedError(
                "SERVER: Received message larger than max (5 vs. 4)"));

  WaitForAllPendingWork();
}

// The limit is inclusive: a message of exactly the limit is delivered,
// regardless of which end configures it or how.
FILTER_TEST(MessageSizeFilterTest,
            ServerMessageLimitFromClientChannelArgsBelowLimitIsAllowed) {
  InitLimitConfig(LimitConfig::kClientChannelArgs, /*client_to_server=*/false,
                  /*limit=*/4, R"("maxResponseMessageBytes": 4)");

  const std::string payload(4, 'x');
  PushClientHalfClose();
  ASSERT_TRUE(PullClientInitialMetadata().ok());
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(payload));
  ASSERT_TRUE(PullServerInitialMetadata().ok());
  ServerToClientNextMessage message = PullServerMessage();
  ASSERT_TRUE(message.ok());
  ASSERT_TRUE(message.has_value());
  EXPECT_THAT(message.value(), HasMessagePayload(payload));

  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_TRUE(PullServerTrailingStatus().ok());

  WaitForAllPendingWork();
}

FILTER_TEST(MessageSizeFilterTest,
            ServerMessageLimitFromServiceConfigBelowLimitIsAllowed) {
  InitLimitConfig(LimitConfig::kClientServiceConfig, /*client_to_server=*/false,
                  /*limit=*/4, R"("maxResponseMessageBytes": 4)");

  const std::string payload(4, 'x');
  PushClientHalfClose();
  ASSERT_TRUE(PullClientInitialMetadata().ok());
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(payload));
  ASSERT_TRUE(PullServerInitialMetadata().ok());
  ServerToClientNextMessage message = PullServerMessage();
  ASSERT_TRUE(message.ok());
  ASSERT_TRUE(message.has_value());
  EXPECT_THAT(message.value(), HasMessagePayload(payload));

  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_TRUE(PullServerTrailingStatus().ok());

  WaitForAllPendingWork();
}

FILTER_TEST(MessageSizeFilterTest,
            ServerMessageLimitFromServerChannelArgsBelowLimitIsAllowed) {
  InitLimitConfig(LimitConfig::kServerChannelArgs, /*client_to_server=*/false,
                  /*limit=*/4, R"("maxResponseMessageBytes": 4)");

  const std::string payload(4, 'x');
  PushClientHalfClose();
  ASSERT_TRUE(PullClientInitialMetadata().ok());
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(payload));
  ASSERT_TRUE(PullServerInitialMetadata().ok());
  ServerToClientNextMessage message = PullServerMessage();
  ASSERT_TRUE(message.ok());
  ASSERT_TRUE(message.has_value());
  EXPECT_THAT(message.value(), HasMessagePayload(payload));

  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_TRUE(PullServerTrailingStatus().ok());

  WaitForAllPendingWork();
}

FILTER_TEST(MessageSizeFilterTest,
            ServerMessageLimitFromClientChannelArgsTooLargeFails) {
  InitLimitConfig(LimitConfig::kClientChannelArgs, /*client_to_server=*/false,
                  /*limit=*/4, R"("maxResponseMessageBytes": 4)");

  const std::string oversized_payload(5, 'x');
  PushClientHalfClose();
  ASSERT_TRUE(PullClientInitialMetadata().ok());
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(oversized_payload));
  ASSERT_TRUE(PullServerInitialMetadata().ok());
  EXPECT_FALSE(PullServerMessage().ok());
  EXPECT_EQ(PullServerTrailingStatus(),
            absl::ResourceExhaustedError(
                "CLIENT: Received message larger than max (5 vs. 4)"));

  WaitForAllPendingWork();
}

FILTER_TEST(MessageSizeFilterTest,
            ServerMessageLimitFromServiceConfigTooLargeFails) {
  InitLimitConfig(LimitConfig::kClientServiceConfig, /*client_to_server=*/false,
                  /*limit=*/4, R"("maxResponseMessageBytes": 4)");

  const std::string oversized_payload(5, 'x');
  PushClientHalfClose();
  ASSERT_TRUE(PullClientInitialMetadata().ok());
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(oversized_payload));
  ASSERT_TRUE(PullServerInitialMetadata().ok());
  EXPECT_FALSE(PullServerMessage().ok());
  EXPECT_EQ(PullServerTrailingStatus(),
            absl::ResourceExhaustedError(
                "CLIENT: Received message larger than max (5 vs. 4)"));

  WaitForAllPendingWork();
}

FILTER_TEST(MessageSizeFilterTest,
            ServerMessageLimitFromServerChannelArgsTooLargeFails) {
  InitLimitConfig(LimitConfig::kServerChannelArgs, /*client_to_server=*/false,
                  /*limit=*/4, R"("maxResponseMessageBytes": 4)");

  const std::string oversized_payload(5, 'x');
  PushClientHalfClose();
  ASSERT_TRUE(PullClientInitialMetadata().ok());
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(oversized_payload));
  ASSERT_TRUE(PullServerInitialMetadata().ok());
  EXPECT_FALSE(PullServerMessage().ok());
  EXPECT_EQ(PullServerTrailingStatus(),
            absl::ResourceExhaustedError(
                "SERVER: Sent message larger than max (5 vs. 4)"));

  WaitForAllPendingWork();
}

}  // namespace grpc_core
