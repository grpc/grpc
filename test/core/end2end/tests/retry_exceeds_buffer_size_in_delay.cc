//
// Copyright 2021 gRPC authors.
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

#include <string>

#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

// Tests the case where the retry buffer size is exceeded during backoff.
// - 1 retry allowed for ABORTED status
// - buffer size set to 100 KiB (larger than initial metadata)
// - client initially sends initial metadata (smaller than buffer size)
// - server sends ABORTED, client goes into backoff delay
// - client sends a 100 KiB message, thus exceeding the buffer size limit
// - retry attempt gets ABORTED but is not retried
CORE_END2END_TEST(RetryTest, RetryExceedsBufferSizeInDelay) {
  InitServer(ChannelArgs());
  InitClient(
      ChannelArgs()
          .Set(GRPC_ARG_SERVICE_CONFIG,
               "{\n"
               "  \"methodConfig\": [ {\n"
               "    \"name\": [\n"
               "      { \"service\": \"service\", \"method\": \"method\" }\n"
               "    ],\n"
               "    \"retryPolicy\": {\n"
               "      \"maxAttempts\": 3,\n"
               "      \"initialBackoff\": \"2s\",\n"
               "      \"maxBackoff\": \"120s\",\n"
               "      \"backoffMultiplier\": 1.6,\n"
               "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
               "    }\n"
               "  } ]\n"
               "}")
          .Set(GRPC_ARG_PER_RPC_RETRY_BUFFER_SIZE, 102400));
  auto c =
      NewClientCall("/service/method").Timeout(Duration::Seconds(15)).Create();
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  // Client sends initial metadata and starts the recv ops.
  IncomingMessage server_message;
  IncomingMetadata server_initial_metadata;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .RecvMessage(server_message)
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  // Server gets a call.
  absl::optional<IncomingCall> s = RequestCall(101);
  Expect(101, true);
  Step();
  EXPECT_NE(s->GetPeer(), absl::nullopt);
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  // Server sends ABORTED.  This tells the client to retry.
  IncomingCloseOnServer client_close;
  s->NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_ABORTED, "message1", {})
      .RecvCloseOnServer(client_close);
  Expect(102, true);
  Step();
  s.reset();
  // Do a bit more polling, to make sure the client sees status from the
  // first attempt.  (Note: This polls for 1s, which is less than the
  // retry initial backoff time of 2s from the service config above.)
  Step(Duration::Seconds(1));
  // Client sends a message that puts it over the buffer size limit.
  c.NewBatch(2).SendMessage(std::string(102401, 'a')).SendCloseFromClient();
  Expect(2, true);
  Step();
  // Server gets another call.
  auto s2 = RequestCall(201);
  Expect(201, true);
  Step();
  // Server again sends ABORTED.  But this time, the client won't retry,
  // since the call has been committed by exceeding the buffer size.
  IncomingCloseOnServer client_close2;
  s2.NewBatch(202)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_ABORTED, "message2", {})
      .RecvCloseOnServer(client_close2);
  Expect(202, true);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_ABORTED);
  EXPECT_EQ(server_status.message(), "message2");
  EXPECT_EQ(s2.method(), "/service/method");
  EXPECT_FALSE(client_close2.was_cancelled());
}

}  // namespace
}  // namespace grpc_core
