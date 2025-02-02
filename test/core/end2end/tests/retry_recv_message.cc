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

#include "gtest/gtest.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

// Tests that receiving a message commits the call.
// - 1 retry allowed for ABORTED status
// - first attempt receives a message and therefore does not retry even
//   though the final status is ABORTED
CORE_END2END_TEST(RetryTests, RetryRecvMessage) {
  if (!IsRetryInCallv3Enabled()) SKIP_IF_V3();
  InitServer(ChannelArgs());
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
      NewClientCall("/service/method").Timeout(Duration::Minutes(1)).Create();
  EXPECT_NE(c.GetPeer(), std::nullopt);
  IncomingMessage server_message;
  IncomingMetadata server_initial_metadata;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage("foo")
      .RecvMessage(server_message)
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = RequestCall(101);
  Expect(101, true);
  Step();
  EXPECT_NE(s.GetPeer(), std::nullopt);
  EXPECT_NE(c.GetPeer(), std::nullopt);
  IncomingCloseOnServer client_close;
  s.NewBatch(103)
      .SendInitialMetadata({})
      .SendMessage("bar")
      .SendStatusFromServer(GRPC_STATUS_ABORTED, "xyz", {})
      .RecvCloseOnServer(client_close);
  Expect(103, true);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_ABORTED);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_FALSE(client_close.was_cancelled());
}

}  // namespace
}  // namespace grpc_core
