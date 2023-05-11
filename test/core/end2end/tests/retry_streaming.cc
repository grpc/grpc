//
//
// Copyright 2017 gRPC authors.
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
//
//

#include <string>

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/surface/channel.h"
#include "test/core/end2end/end2end_tests.h"

using testing::HasSubstr;

namespace grpc_core {
namespace {

// Tests retrying a streaming RPC.  This is the same as
// the basic retry test, except that the client sends two messages on the
// call before the initial attempt fails.
// FIXME: We should also test the case where the retry is committed after
// replaying 1 of 2 previously-completed send_message ops.  However,
// there's no way to trigger that from an end2end test, because the
// replayed ops happen under the hood -- they are not surfaced to the
// C-core API, and therefore we have no way to inject the commit at the
// right point.
CORE_END2END_TEST(RetryTest, RetryStreaming) {
  InitServer(ChannelArgs());
  InitClient(
      ChannelArgs()
          .Set(GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE, 1024 * 8)
          .Set(GRPC_ARG_ENABLE_CHANNELZ, true)
          .Set(GRPC_ARG_SERVICE_CONFIG,
               "{\n"
               "  \"methodConfig\": [ {\n"
               "    \"name\": [\n"
               "      { \"service\": \"service\", \"method\": \"method\" }\n"
               "    ],\n"
               "    \"retryPolicy\": {\n"
               "      \"maxAttempts\": 3,\n"
               "      \"initialBackoff\": \"1s\",\n"
               "      \"maxBackoff\": \"120s\",\n"
               "      \"backoffMultiplier\": 1.6,\n"
               "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
               "    }\n"
               "  } ]\n"
               "}"));
  auto c =
      NewClientCall("/service/method").Timeout(Duration::Seconds(5)).Create();
  channelz::ChannelNode* channelz_channel =
      grpc_channel_get_channelz_node(client());
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  // Client starts a batch for receiving initial metadata, a message,
  // and trailing metadata.
  IncomingStatusOnClient server_status;
  IncomingMetadata server_initial_metadata;
  IncomingMessage server_message;
  c.NewBatch(1)
      .RecvInitialMetadata(server_initial_metadata)
      .RecvMessage(server_message)
      .RecvStatusOnClient(server_status);
  // Client sends initial metadata and a message.
  c.NewBatch(2).SendInitialMetadata({}).SendMessage("foo");
  Expect(2, true);
  Step();
  // Server gets a call with received initial metadata.
  auto s = RequestCall(101);
  Expect(101, true);
  Step();
  EXPECT_NE(s.GetPeer(), absl::nullopt);
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  // Server receives a message.
  IncomingMessage client_message;
  s.NewBatch(102).RecvMessage(client_message);
  Expect(102, true);
  Step();
  // Client sends a second message.
  c.NewBatch(3).SendMessage("bar");
  Expect(3, true);
  Step();
  // Server receives the second message.
  IncomingMessage client_message2;
  s.NewBatch(103).RecvMessage(client_message2);
  Expect(103, true);
  Step();
  // Server sends both initial and trailing metadata.
  IncomingCloseOnServer client_close;
  s.NewBatch(104)
      .RecvCloseOnServer(client_close)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_ABORTED, "xyz", {});
  Expect(104, true);
  Step();
  // Clean up from first attempt.
  EXPECT_EQ(client_message.payload(), "foo");
  EXPECT_EQ(client_message2.payload(), "bar");
  // Server gets a second call (the retry).
  auto s2 = RequestCall(201);
  Expect(201, true);
  Step();
  EXPECT_NE(s.GetPeer(), absl::nullopt);
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  // Server receives a message.
  IncomingMessage client_message3;
  s2.NewBatch(202).RecvMessage(client_message3);
  Expect(202, true);
  Step();
  // Server receives a second message.
  IncomingMessage client_message4;
  s2.NewBatch(203).RecvMessage(client_message4);
  Expect(203, true);
  Step();
  // Client sends a third message and a close.
  c.NewBatch(4).SendMessage("baz").SendCloseFromClient();
  Expect(4, true);
  Step();
  // Server receives a third message.
  IncomingMessage client_message5;
  s2.NewBatch(204).RecvMessage(client_message5);
  Expect(204, true);
  Step();
  // Server receives a close and sends initial metadata, a message, and
  // trailing metadata.
  IncomingCloseOnServer client_close2;
  s2.NewBatch(205)
      .RecvCloseOnServer(client_close2)
      .SendInitialMetadata({})
      .SendMessage("quux")
      // Returning a retriable code, but because we are also sending a
      // message, the client will commit instead of retrying again.
      .SendStatusFromServer(GRPC_STATUS_ABORTED, "xyz", {});
  Expect(205, true);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_ABORTED);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_FALSE(client_close.was_cancelled());
  EXPECT_NE(channelz_channel, nullptr);
  // TODO(roth): consider using a regex check here.
  std::string json = channelz_channel->RenderJsonString();
  EXPECT_THAT(json, HasSubstr("\"trace\""));
  EXPECT_THAT(json, HasSubstr("\"description\":\"Channel created\""));
  EXPECT_THAT(json, HasSubstr("\"severity\":\"CT_INFO\""));
  EXPECT_THAT(json, HasSubstr("Resolution event"));
  EXPECT_THAT(json, HasSubstr("Created new LB policy"));
  EXPECT_THAT(json, HasSubstr("Service config changed"));
  EXPECT_THAT(json, HasSubstr("Address list became non-empty"));
  EXPECT_THAT(json, HasSubstr("Channel state change to CONNECTING"));
  EXPECT_EQ(client_message3.payload(), "foo");
  EXPECT_EQ(client_message4.payload(), "bar");
  EXPECT_EQ(client_message5.payload(), "baz");
}

}  // namespace
}  // namespace grpc_core
