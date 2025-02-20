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

// Tests that we don't retry for non-retryable status codes.
// - 1 retry allowed for ABORTED status
// - first attempt gets INVALID_ARGUMENT, so no retry is done
CORE_END2END_TEST(RetryTests, RetryNonRetriableStatus) {
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
      NewClientCall("/service/method").Timeout(Duration::Seconds(30)).Create();
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
  s.NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_INVALID_ARGUMENT, "xyz", {})
      .RecvCloseOnServer(client_close);
  Expect(102, true);
  Expect(1, true);
  // TODO(roth): After promise conversion, reevalute this.
  LOG(INFO)
      << "NOTE(roth): We've seen infrequent flakiness in this test due to "
         "a callback reordering issue.  I considered making a change similar "
         "to https://github.com/grpc/grpc/pull/37944 here to avoid the "
         "flakiness, but that would have made this test essentially the "
         "same as the existing retry_non_retriable_status_before_trailers "
         "test, and the reason these are two separate tests is that they "
         "cover different edge cases in the current implementation.  The "
         "flake rate is currently low enough (about 3 flakes in 6 months) "
         "that I think we get more value from having this separate test "
         "than we're losing due to the flakiness, so I'm leaving the test "
         "as-is for now.  Once the promise migration is done, this "
         "difference won't be important anymore, and we'll be able to "
         "remove a bunch of retry test cases at that point anyway.";
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_FALSE(client_close.was_cancelled());
}
}  // namespace
}  // namespace grpc_core
