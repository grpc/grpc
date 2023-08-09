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
// - 1 retry allowed for ABORTED status
// - both attempts do not receive a response until after perAttemptRecvTimeout
CORE_END2END_TEST(RetryTest, RetryPerAttemptRecvTimeoutOnLastAttempt) {
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
                  "      \"maxAttempts\": 2,\n"
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
  // Server gets a second call, which it also does not respond to.
  absl::optional<IncomingCall> s1 = RequestCall(201);
  Expect(201, true);
  Step();
  // Now we can unref the first call.
  s0.reset();
  // Make sure the "grpc-previous-rpc-attempts" header was sent in the retry.
  EXPECT_EQ(s1->GetInitialMetadata("grpc-previous-rpc-attempts"), "1");
  // Client sees call completion.
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_CANCELLED);
  EXPECT_EQ(server_status.message(), "retry perAttemptRecvTimeout exceeded");
  EXPECT_EQ(s1->method(), "/service/method");
}

}  // namespace
}  // namespace grpc_core
