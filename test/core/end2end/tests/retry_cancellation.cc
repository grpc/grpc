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
#include "test/core/end2end/tests/cancel_test_helpers.h"

namespace grpc_core {
namespace {

// Tests retry cancellation.
void TestRetryCancellation(CoreEnd2endTest& test,
                           std::unique_ptr<CancellationMode> mode) {
  test.InitServer(ChannelArgs());
  test.InitClient(ChannelArgs().Set(
      GRPC_ARG_SERVICE_CONFIG,
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"service\", \"method\": \"method\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 5,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    },\n"
      "    \"timeout\": \"10s\"\n"
      "  } ]\n"
      "}"));
  auto c = test.NewClientCall("/service/method")
               .Timeout(Duration::Seconds(5))
               .Create();
  EXPECT_NE(c.GetPeer(), std::nullopt);
  // Client starts a batch with all 6 ops.
  IncomingMetadata server_initial_metadata;
  IncomingMessage server_message;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage("foo")
      .RecvMessage(server_message)
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  // Server gets a call and fails with retryable status.
  std::optional<CoreEnd2endTest::IncomingCall> s = test.RequestCall(101);
  test.Expect(101, true);
  test.Step();
  EXPECT_NE(s->GetPeer(), std::nullopt);
  EXPECT_NE(c.GetPeer(), std::nullopt);
  IncomingCloseOnServer client_close;
  s->NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_ABORTED, "xyz", {})
      .RecvCloseOnServer(client_close);
  test.Expect(102, true);
  test.Step();
  s.reset();
  // Server gets a second call (the retry).
  s.emplace(test.RequestCall(201));
  test.Expect(201, true);
  test.Step();
  // Initiate cancellation.
  mode->Apply(c);
  test.Expect(1, true);
  test.Step();
  EXPECT_EQ(server_status.status(), mode->ExpectedStatus());
  EXPECT_FALSE(client_close.was_cancelled());
}

CORE_END2END_TEST(RetryTests, RetryCancellation) {
  if (!IsRetryInCallv3Enabled()) SKIP_IF_V3();
  TestRetryCancellation(*this, std::make_unique<CancelCancellationMode>());
}

CORE_END2END_TEST(RetryTests, RetryDeadline) {
  if (!IsRetryInCallv3Enabled()) SKIP_IF_V3();
  TestRetryCancellation(*this, std::make_unique<DeadlineCancellationMode>());
}

}  // namespace
}  // namespace grpc_core
