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
        StartCallForFilter(NewServiceConfigClientMetadata());
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

  // Asserts a `limit`+1 byte message in the given direction is rejected with
  // RESOURCE_EXHAUSTED.
  void ExpectLimitEnforced(LimitConfig config, bool client_to_server, int limit,
                           absl::string_view service_config_field) {
    InitLimitConfig(config, client_to_server, limit, service_config_field);

    const std::string oversized_payload(limit + 1, 'x');
    if (client_to_server) {
      PushClientMessage(NewMessage(oversized_payload));
      PushClientHalfClose();
      ASSERT_TRUE(PullClientInitialMetadata().ok());
      // The filter rejected the message, so the pull fails rather than
      // delivering.
      EXPECT_FALSE(PullClientMessage().ok());
    } else {
      PushClientHalfClose();
      ASSERT_TRUE(PullClientInitialMetadata().ok());
      PushServerInitialMetadata(NewServerMetadata());
      PushServerMessage(NewMessage(oversized_payload));
      ASSERT_TRUE(PullServerInitialMetadata().ok());
      EXPECT_FALSE(PullServerMessage().ok());
    }

    EXPECT_EQ(PullServerTrailingStatus().code(),
              absl::StatusCode::kResourceExhausted);

    WaitForAllPendingWork();
  }

  void ExpectClientToServerLimitEnforced(LimitConfig config) {
    ExpectLimitEnforced(config, /*client_to_server=*/true, /*limit=*/4,
                        R"("maxRequestMessageBytes": 4)");
  }

  void ExpectServerToClientLimitEnforced(LimitConfig config) {
    ExpectLimitEnforced(config, /*client_to_server=*/false, /*limit=*/4,
                        R"("maxResponseMessageBytes": 4)");
  }

  // Asserts a `payload_size`-byte message in the given direction under
  // `config` with `limit` arrives intact and the call completes OK.
  void ExpectMessageAccepted(LimitConfig config, bool client_to_server,
                             int limit, absl::string_view service_config_field,
                             int payload_size) {
    InitLimitConfig(config, client_to_server, limit, service_config_field);

    const std::string payload(payload_size, 'x');
    if (client_to_server) {
      PushClientMessage(NewMessage(payload));
      PushClientHalfClose();
      ASSERT_TRUE(PullClientInitialMetadata().ok());
      ClientToServerNextMessage message = PullClientMessage();
      ASSERT_TRUE(message.ok());
      ASSERT_TRUE(message.has_value());
      EXPECT_THAT(message.value(), HasMessagePayload(payload));
      EXPECT_TRUE(PullClientHalfClose());
    } else {
      PushClientHalfClose();
      ASSERT_TRUE(PullClientInitialMetadata().ok());
      PushServerInitialMetadata(NewServerMetadata());
      PushServerMessage(NewMessage(payload));
      ASSERT_TRUE(PullServerInitialMetadata().ok());
      ServerToClientNextMessage message = PullServerMessage();
      ASSERT_TRUE(message.ok());
      ASSERT_TRUE(message.has_value());
      EXPECT_THAT(message.value(), HasMessagePayload(payload));
    }

    PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
    EXPECT_TRUE(PullServerTrailingStatus().ok());

    WaitForAllPendingWork();
  }

  // The limit is inclusive: a `limit`-byte message under `config` passes.
  void ExpectClientToServerAtLimitPasses(LimitConfig config) {
    ExpectMessageAccepted(config, /*client_to_server=*/true, /*limit=*/4,
                          R"("maxRequestMessageBytes": 4)",
                          /*payload_size=*/4);
  }

  void ExpectServerToClientAtLimitPasses(LimitConfig config) {
    ExpectMessageAccepted(config, /*client_to_server=*/false, /*limit=*/4,
                          R"("maxResponseMessageBytes": 4)",
                          /*payload_size=*/4);
  }
};

// A message within the limit passes through and the call completes normally.
FILTER_TEST(MessageSizeFilterTest, WithinLimitPasses) {
  ExpectMessageAccepted(LimitConfig::kServerChannelArgs,
                        /*client_to_server=*/true, /*limit=*/1024, "",
                        /*payload_size=*/5);
}

// The limit is inclusive: a message of exactly the limit is delivered,
// regardless of which end configures it or how.
FILTER_TEST(MessageSizeFilterTest, ClientSendAtLimitPasses) {
  ExpectClientToServerAtLimitPasses(LimitConfig::kClientChannelArgs);
}

FILTER_TEST(MessageSizeFilterTest, ClientSendAtLimitFromServiceConfigPasses) {
  ExpectClientToServerAtLimitPasses(LimitConfig::kClientServiceConfig);
}

FILTER_TEST(MessageSizeFilterTest, ServerRecvAtLimitPasses) {
  ExpectClientToServerAtLimitPasses(LimitConfig::kServerChannelArgs);
}

FILTER_TEST(MessageSizeFilterTest, ClientSendExceedsLimitRejected) {
  ExpectClientToServerLimitEnforced(LimitConfig::kClientChannelArgs);
}

FILTER_TEST(MessageSizeFilterTest, ClientSendLimitFromServiceConfig) {
  ExpectClientToServerLimitEnforced(LimitConfig::kClientServiceConfig);
}

FILTER_TEST(MessageSizeFilterTest, ServerRecvExceedsLimitRejected) {
  ExpectClientToServerLimitEnforced(LimitConfig::kServerChannelArgs);
}

// The limit is inclusive: a message of exactly the limit is delivered,
// regardless of which end configures it or how.
FILTER_TEST(MessageSizeFilterTest, ClientRecvAtLimitPasses) {
  ExpectServerToClientAtLimitPasses(LimitConfig::kClientChannelArgs);
}

FILTER_TEST(MessageSizeFilterTest, ClientRecvAtLimitFromServiceConfigPasses) {
  ExpectServerToClientAtLimitPasses(LimitConfig::kClientServiceConfig);
}

FILTER_TEST(MessageSizeFilterTest, ServerSendAtLimitPasses) {
  ExpectServerToClientAtLimitPasses(LimitConfig::kServerChannelArgs);
}

FILTER_TEST(MessageSizeFilterTest, ClientRecvExceedsLimitRejected) {
  ExpectServerToClientLimitEnforced(LimitConfig::kClientChannelArgs);
}

FILTER_TEST(MessageSizeFilterTest, ClientRecvLimitFromServiceConfig) {
  ExpectServerToClientLimitEnforced(LimitConfig::kClientServiceConfig);
}

FILTER_TEST(MessageSizeFilterTest, ServerSendExceedsLimitRejected) {
  ExpectServerToClientLimitEnforced(LimitConfig::kServerChannelArgs);
}

}  // namespace grpc_core
