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

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {
// Tests that we can unref a call whose status is cached but not yet
// requested by the application.  This should not cause a memory leak.
CORE_END2END_TEST(RetryTest, RetryUnrefBeforeFinish) {
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
  // Client starts send ops.
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage("foo")
      .SendCloseFromClient();
  // Client starts recv_initial_metadata and recv_message, but not
  // recv_trailing_metadata.
  IncomingMetadata server_initial_metadata;
  IncomingMessage server_message;
  c.NewBatch(2)
      .RecvInitialMetadata(server_initial_metadata)
      .RecvMessage(server_message);
  // Server gets a call and client send ops complete.
  auto s = RequestCall(101);
  Expect(1, true);
  Expect(101, true);
  Step();
  // Server immediately sends FAILED_PRECONDITION status (not retriable).
  // This forces the retry filter to start a recv_trailing_metadata op
  // internally, since the application hasn't started it yet.
  IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_FAILED_PRECONDITION, "xyz", {})
      .RecvCloseOnServer(client_close);
  // Server ops complete and client recv ops complete.
  Expect(2, true);
  Expect(102, true);
  Step();
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_FALSE(client_close.was_cancelled());
}
}  // namespace
}  // namespace grpc_core
