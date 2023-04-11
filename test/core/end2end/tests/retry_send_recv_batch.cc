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

#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

// Tests a scenario where there is a batch containing both a send op and
// a recv op, where the send op completes but the recv op does not, and
// then a subsequent recv op is started.  This ensures that we do not
// incorrectly attempt to replay the send op.
TEST_P(RetryTest, RetrySendRecvBatch) {
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
  // Client starts batch with send_initial_metadata and recv_initial_metadata.
  IncomingMetadata server_initial_metadata;
  c.NewBatch(1).SendInitialMetadata({}).RecvInitialMetadata(
      server_initial_metadata);
  // Client starts a batch with send_message and recv_trailing_metadata.
  IncomingStatusOnClient server_status;
  c.NewBatch(2).SendMessage("hello").RecvStatusOnClient(server_status);
  // Server gets a call.
  auto s = RequestCall(101);
  Expect(101, true);
  Step();
  // Client starts a batch containing recv_message.
  IncomingMessage server_message;
  c.NewBatch(3).RecvMessage(server_message);
  // Server fails the call with a non-retriable status.
  IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_PERMISSION_DENIED, "xyz", {})
      .RecvCloseOnServer(client_close);
  Expect(102, true);
  Expect(1, true);
  Expect(2, true);
  Expect(3, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_PERMISSION_DENIED);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_FALSE(client_close.was_cancelled());
}

}  // namespace
}  // namespace grpc_core
