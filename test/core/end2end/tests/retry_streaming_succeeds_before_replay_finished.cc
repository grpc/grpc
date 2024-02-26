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

// Tests that we correctly clean up if the second attempt finishes
// before we have finished replaying all of the send ops.
CORE_END2END_TEST(RetryTest, RetryStreamSucceedsBeforeReplayFinished) {
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
      NewClientCall("/service/method").Timeout(Duration::Seconds(5)).Create();
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  // Client starts a batch for receiving initial metadata, a message,
  // and trailing metadata.
  IncomingMetadata server_initial_metadata;
  IncomingMessage server_message;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .RecvInitialMetadata(server_initial_metadata)
      .RecvMessage(server_message)
      .RecvStatusOnClient(server_status);
  // Client sends initial metadata and a message.
  c.NewBatch(2).SendInitialMetadata({}).SendMessage("foo");
  Expect(2, true);
  Step();
  // Server gets a call with received initial metadata.
  absl::optional<IncomingCall> s = RequestCall(101);
  Expect(101, true);
  Step();
  EXPECT_NE(s->GetPeer(), absl::nullopt);
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  // Server receives a message.
  IncomingMessage client_message;
  s->NewBatch(102).RecvMessage(client_message);
  Expect(102, true);
  Step();
  // Client sends a second message.
  c.NewBatch(3).SendMessage("bar");
  Expect(3, true);
  Step();
  // Server receives the second message.
  IncomingMessage client_message2;
  s->NewBatch(103).RecvMessage(client_message2);
  Expect(103, true);
  Step();
  // Client sends a third message.
  c.NewBatch(4).SendMessage("baz");
  Expect(4, true);
  Step();
  // Server receives the third message.
  IncomingMessage client_message3;
  s->NewBatch(104).RecvMessage(client_message3);
  Expect(104, true);
  Step();
  // Server sends both initial and trailing metadata.
  IncomingCloseOnServer client_close;
  s->NewBatch(105)
      .RecvCloseOnServer(client_close)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_ABORTED, "xyz", {});
  Expect(105, true);
  Step();
  // Clean up from first attempt.
  s.reset();
  EXPECT_EQ(client_message.payload(), "foo");
  EXPECT_EQ(client_message2.payload(), "bar");
  EXPECT_EQ(client_message3.payload(), "baz");
  // Server gets a second call (the retry).
  s.emplace(RequestCall(201));
  Expect(201, true);
  Step();
  EXPECT_NE(s->GetPeer(), absl::nullopt);
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  // Server receives the first message (and does not receive any others).
  IncomingMessage client_message4;
  s->NewBatch(202).RecvMessage(client_message4);
  Expect(202, true);
  Step();
  // Server sends initial metadata, a message, and trailing metadata.
  s->NewBatch(205)
      .SendInitialMetadata({})
      .SendMessage("qux")
      // Returning a retriable code, but because we are also sending a
      // message, the client will commit instead of retrying again.
      .SendStatusFromServer(GRPC_STATUS_ABORTED, "xyz", {});
  Expect(205, true);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_ABORTED);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s->method(), "/service/method");
  EXPECT_FALSE(client_close.was_cancelled());
  EXPECT_EQ(client_message4.payload(), "foo");
}

}  // namespace
}  // namespace grpc_core
