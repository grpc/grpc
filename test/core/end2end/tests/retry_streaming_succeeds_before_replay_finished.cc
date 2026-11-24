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

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include <memory>
#include <optional>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/time.h"
#include "test/core/end2end/end2end_tests.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace {

// Tests that we correctly clean up if the second attempt finishes
// before we have finished replaying all of the send ops.
CORE_END2END_TEST(RetryTests, RetryStreamSucceedsBeforeReplayFinished) {
  SKIP_IF_V3();  // Not working yet
  InitServer(DefaultServerArgs());
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
  EXPECT_NE(c.GetPeer(), std::nullopt);
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
  std::optional<IncomingCall> s = RequestCall(101);
  Expect(101, true);
  Step();
  EXPECT_NE(s->GetPeer(), std::nullopt);
  EXPECT_NE(c.GetPeer(), std::nullopt);
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
  EXPECT_NE(s->GetPeer(), std::nullopt);
  EXPECT_NE(c.GetPeer(), std::nullopt);
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

// Tests that we do not fail a pending batch's recv_initial_metadata_ready
// again if the call is cancelled after that callback was returned to the
// surface but before the batch's recv_message completed, while send ops
// are still being replayed.
CORE_END2END_TEST(RetryTests,
                  RetryStreamCancelAfterRecvInitialMetadataDuringReplay) {
  SKIP_IF_V3();  // Not working yet
  // Disabling BDP probing keeps the server's stream window at the default
  // size, so the client cannot replay the first message on the second
  // attempt until the server reads it.
  InitServer(DefaultServerArgs().Set(GRPC_ARG_HTTP2_BDP_PROBE, false));
  InitClient(ChannelArgs().Set(
      GRPC_ARG_SERVICE_CONFIG,
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"service\", \"method\": \"method\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}"));
  auto c =
      NewClientCall("/service/method").Timeout(Duration::Seconds(30)).Create();
  EXPECT_NE(c.GetPeer(), std::nullopt);
  // Client starts a batch for receiving initial metadata and a message,
  // and a separate batch for receiving trailing metadata.
  IncomingMetadata server_initial_metadata;
  IncomingMessage server_message;
  c.NewBatch(1)
      .RecvInitialMetadata(server_initial_metadata)
      .RecvMessage(server_message);
  IncomingStatusOnClient server_status;
  c.NewBatch(2).RecvStatusOnClient(server_status);
  // Client sends initial metadata and a message larger than the default
  // HTTP/2 stream window.
  c.NewBatch(3).SendInitialMetadata({}).SendMessage(RandomSlice(128 * 1024));
  // Server gets a call and receives the message.  With a proxy in between,
  // the client's send may complete before the server reads the message.
  bool send_message_done = false;
  std::optional<IncomingCall> s = RequestCall(101);
  Expect(101, true);
  Expect(3, Maybe{&send_message_done});
  Step();
  IncomingMessage client_message;
  s->NewBatch(102).RecvMessage(client_message);
  Expect(102, true);
  if (!send_message_done) Expect(3, true);
  Step();
  // Client sends a second message, so that a send op remains to be replayed
  // after the first one.
  c.NewBatch(4).SendMessage("bar");
  IncomingMessage client_message2;
  s->NewBatch(103).RecvMessage(client_message2);
  Expect(4, true);
  Expect(103, true);
  Step();
  // Server fails the first attempt with a retriable status.
  IncomingCloseOnServer client_close;
  s->NewBatch(104)
      .RecvCloseOnServer(client_close)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_ABORTED, "xyz", {});
  Expect(104, true);
  Step();
  s.reset();
  EXPECT_EQ(client_message2.payload(), "bar");
  // Server gets a second call (the retry) and sends only initial metadata,
  // without reading the replayed messages.
  s.emplace(RequestCall(201));
  Expect(201, true);
  Step();
  s->NewBatch(202).SendInitialMetadata({});
  Expect(202, true);
  Step();
  // Give the client time to receive the initial metadata.  Batch 1 must not
  // complete, because recv_message is still pending.
  Step();
  // Client cancels the call.
  c.Cancel();
  IncomingCloseOnServer client_close2;
  s->NewBatch(203).RecvCloseOnServer(client_close2);
  Expect(1, AnyStatus{});
  Expect(2, true);
  Expect(203, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_CANCELLED);
  EXPECT_TRUE(client_close2.was_cancelled());
}

}  // namespace
}  // namespace grpc_core
