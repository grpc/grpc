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

#include <initializer_list>

#include "absl/strings/str_format.h"
#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace {

// Tests perAttemptRecvTimeout:
// - 2 retries allowed for ABORTED status
// - first attempt does not receive a response until after perAttemptRecvTimeout
// - second attempt returns ABORTED
// - third attempt returns OK
CORE_END2END_TEST(RetryTest, RetryPerAttemptRecvTimeout) {
  InitServer(ChannelArgs());
  InitClient(
      ChannelArgs()
          .Set(GRPC_ARG_EXPERIMENTAL_ENABLE_HEDGING, true)
          .Set(
              GRPC_ARG_SERVICE_CONFIG,
              absl::StrFormat(
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
                  "      \"perAttemptRecvTimeout\": \"%ds\",\n"
                  "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
                  "    }\n"
                  "  } ]\n"
                  "}",
                  2 * grpc_test_slowdown_factor())));
  auto c =
      NewClientCall("/service/method").Timeout(Duration::Seconds(10)).Create();
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
  // Server gets a call but does not respond to the call.
  absl::optional<IncomingCall> s0 = RequestCall(101);
  Expect(101, true);
  Step();
  // Make sure the "grpc-previous-rpc-attempts" header was not sent in the
  // initial attempt.
  EXPECT_EQ(s0->GetInitialMetadata("grpc-previous-rpc-attempts"),
            absl::nullopt);
  // Server gets a second call.
  absl::optional<IncomingCall> s1 = RequestCall(201);
  Expect(201, true);
  Step();
  // Now we can unref the first call.
  s0.reset();
  // Make sure the "grpc-previous-rpc-attempts" header was sent in the retry.
  EXPECT_EQ(s1->GetInitialMetadata("grpc-previous-rpc-attempts"), "1");
  EXPECT_NE(s1->GetPeer(), absl::nullopt);
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  // Server sends status ABORTED.
  IncomingCloseOnServer client_close1;
  s1->NewBatch(202)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_ABORTED, "xyz", {})
      .RecvCloseOnServer(client_close1);
  Expect(202, true);
  Step();
  s1.reset();
  // Server gets a third call.
  auto s2 = RequestCall(301);
  Expect(301, true);
  Step();
  // Make sure the "grpc-previous-rpc-attempts" header was sent in the retry.
  EXPECT_EQ(s2.GetInitialMetadata("grpc-previous-rpc-attempts"), "2");
  IncomingMessage client_message2;
  s2.NewBatch(302).RecvMessage(client_message2);
  // Server sends OK status.
  IncomingCloseOnServer client_close2;
  s2.NewBatch(303)
      .SendInitialMetadata({})
      .SendMessage("bar")
      .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {})
      .RecvCloseOnServer(client_close2);
  Expect(302, true);
  Expect(303, true);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_OK);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s2.method(), "/service/method");
  EXPECT_FALSE(client_close2.was_cancelled());
}
}  // namespace
}  // namespace grpc_core
