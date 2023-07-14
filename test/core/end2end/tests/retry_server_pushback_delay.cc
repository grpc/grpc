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

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

// Tests that we honor server push-back delay.
// - 2 retries allowed for ABORTED status
// - first attempt gets ABORTED with a long delay
// - second attempt succeeds
CORE_END2END_TEST(RetryTest, RetryServerPushbackDelay) {
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
  absl::optional<IncomingCall> s = RequestCall(101);
  Expect(101, true);
  Step(Duration::Seconds(20));
  EXPECT_NE(s->GetPeer(), absl::nullopt);
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  IncomingCloseOnServer client_close;
  s->NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_ABORTED, "message1",
                            {{"grpc-retry-pushback-ms", "2000"}})
      .RecvCloseOnServer(client_close);
  Expect(102, true);
  Step();
  const auto before_retry = Timestamp::Now();
  s.reset();
  s.emplace(RequestCall(201));
  Expect(201, true);
  Step();
  const auto after_retry = Timestamp::Now();
  const auto retry_delay = after_retry - before_retry;
  // Configured back-off was 1 second, server push-back said 2 seconds.
  // To avoid flakiness, we allow some fudge factor here.
  EXPECT_GE(retry_delay, Duration::Milliseconds(1800));
  EXPECT_NE(s->GetPeer(), absl::nullopt);
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  IncomingCloseOnServer client_close2;
  s->NewBatch(202)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_OK, "message2", {})
      .RecvCloseOnServer(client_close2);
  Expect(202, true);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_OK);
  EXPECT_EQ(server_status.message(), "message2");
  EXPECT_EQ(s->method(), "/service/method");
  EXPECT_FALSE(client_close2.was_cancelled());
}

}  // namespace
}  // namespace grpc_core
