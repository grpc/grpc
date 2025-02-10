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

// Tests that receiving initial metadata commits the call.
// - 1 retry allowed for ABORTED status
// - first attempt receives initial metadata before trailing metadata,
//   so no retry is done even though status was ABORTED
CORE_END2END_TEST(RetryTests, RetryRecvInitialMetadata) {
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
  // Ideally, the client would include the recv_initial_metadata op in
  // the same batch as the others.  However, there are cases where
  // callbacks get reordered such that the retry filter sees
  // recv_trailing_metadata complete before recv_initial_metadata,
  // which causes it to trigger a retry.  Putting recv_initial_metadata
  // in its own batch allows us to wait for the client to receive that
  // op before the server sends trailing metadata, thus avoiding that
  // problem.  This is in principle a little sub-optimal, since it doesn't
  // cover the code paths where all the ops are in the same batch.
  // However, that will be less of an issue once we finish the promise
  // migration, since the promise-based retry impl won't be sensitive to
  // batching, so this is just a short-term deficiency.
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage("foo")
      .RecvMessage(server_message)
      .SendCloseFromClient()
      .RecvStatusOnClient(server_status);
  c.NewBatch(2).RecvInitialMetadata(server_initial_metadata);
  auto s = RequestCall(101);
  Expect(101, true);
  Step();
  EXPECT_NE(s.GetPeer(), std::nullopt);
  EXPECT_NE(c.GetPeer(), std::nullopt);
  // Server sends initial metadata in its own batch, before sending
  // trailing metadata.
  // Ideally, this would not require actually sending any metadata
  // entries, but we do so to avoid sporadic failures in the proxy
  // tests, where the proxy may wind up combining the batches, depending
  // on timing.  Sending a metadata entry ensures that the transport
  // won't send a Trailers-Only response, even if the batches are combined.
  s.NewBatch(102).SendInitialMetadata({{"key1", "val1"}});
  Expect(102, true);
  Expect(2, true);
  Step();
  IncomingCloseOnServer client_close;
  s.NewBatch(103)
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
