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

#include <memory>

#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

// Tests that we can continue to send/recv messages on a streaming call
// after retries are committed.
CORE_END2END_TEST(RetryTest, RetryStreamingAfterCommit) {
  InitServer(ChannelArgs());
  InitClient(ChannelArgs().Set(
      GRPC_ARG_SERVICE_CONFIG,
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
      NewClientCall("/service/method").Timeout(Duration::Minutes(1)).Create();
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  // Client starts a batch for receiving initial metadata and a message.
  // This will commit retries.
  IncomingMessage server_message;
  IncomingMetadata server_initial_metadata;
  c.NewBatch(2)
      .RecvInitialMetadata(server_initial_metadata)
      .RecvMessage(server_message);
  // Client sends initial metadata and a message.
  c.NewBatch(3).SendInitialMetadata({}).SendMessage("foo");
  Expect(3, true);
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
  // Server sends initial metadata and a message.
  s.NewBatch(103).SendInitialMetadata({}).SendMessage("bar");
  Expect(103, true);
  // Client receives initial metadata and a message.
  Expect(2, true);
  Step();
  // Client sends a second message and a close.
  c.NewBatch(4).SendMessage("baz").SendCloseFromClient();
  Expect(4, true);
  Step();
  // Server receives a second message.
  IncomingMessage client_message2;
  s.NewBatch(104).RecvMessage(client_message2);
  Expect(104, true);
  Step();
  // Server receives a close, sends a second message, and sends status.
  // Returning a retriable code, but because retries are already
  // committed, the client will not retry.
  IncomingCloseOnServer client_close;
  s.NewBatch(105)
      .RecvCloseOnServer(client_close)
      .SendMessage("quux")
      .SendStatusFromServer(GRPC_STATUS_ABORTED, "xyz", {});
  Expect(105, true);
  Step();
  // Client receives a second message.
  IncomingMessage server_message2;
  c.NewBatch(5).RecvMessage(server_message2);
  Expect(5, true);
  Step();
  // Client receives status.
  IncomingStatusOnClient server_status;
  c.NewBatch(1).RecvStatusOnClient(server_status);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_ABORTED);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_FALSE(client_close.was_cancelled());
  EXPECT_EQ(client_message.payload(), "foo");
  EXPECT_EQ(server_message.payload(), "bar");
  EXPECT_EQ(client_message2.payload(), "baz");
  EXPECT_EQ(server_message2.payload(), "quux");
}

}  // namespace
}  // namespace grpc_core
