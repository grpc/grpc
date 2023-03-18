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

#include <string.h>

#include <functional>
#include <initializer_list>
#include <memory>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/time_util.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/tests/cancel_test_helpers.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace {

void TestRetryCancelDuringDelay(
    CoreEnd2endTest& test,
    std::unique_ptr<CancellationMode> cancellation_mode) {
  test.InitServer(ChannelArgs());
  test.InitClient(ChannelArgs().Set(
      GRPC_ARG_SERVICE_CONFIG,
      absl::StrFormat(
          "{\n"
          "  \"methodConfig\": [ {\n"
          "    \"name\": [\n"
          "      { \"service\": \"service\", \"method\": \"method\" }\n"
          "    ],\n"
          "    \"retryPolicy\": {\n"
          "      \"maxAttempts\": 3,\n"
          "      \"initialBackoff\": \"%ds\",\n"
          "      \"maxBackoff\": \"120s\",\n"
          "      \"backoffMultiplier\": 1.6,\n"
          "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
          "    }\n"
          "  } ]\n"
          "}",
          10 * grpc_test_slowdown_factor())));
  auto expect_finish_before =
      test.TimestampAfterDuration(Duration::Seconds(10));
  auto c = test.NewClientCall("/service/method")
               .Timeout(Duration::Seconds(5))
               .Create();
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  // Client starts a batch with all 6 ops.
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  CoreEnd2endTest::IncomingMessage server_message;
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage("foo")
      .RecvMessage(server_message)
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  // Server gets a call and fails with retryable status.
  auto s = test.RequestCall(101);
  test.Expect(101, true);
  test.Step();
  EXPECT_NE(s.GetPeer(), absl::nullopt);
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  CoreEnd2endTest::IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_ABORTED, "xyz", {})
      .RecvCloseOnServer(client_close);
  test.Expect(102, true);
  test.Step();
  // Server should never get a second call, because the initial retry
  // delay is longer than the call's deadline.
  auto s2 = test.RequestCall(201);
  // Initiate cancellation.
  cancellation_mode->Apply(c);
  test.Expect(1, true);
  test.Step();
  auto finish_time = Timestamp::Now();
  EXPECT_EQ(server_status.status(), cancellation_mode->ExpectedStatus());
  EXPECT_FALSE(client_close.was_cancelled());
  // Make sure we didn't wait the full deadline before failing.
  EXPECT_LT(finish_time, expect_finish_before);
  test.ShutdownServerAndNotify(1000);
  test.Expect(1000, true);
  test.Expect(201, false);
  test.Step();
}

TEST_P(RetryTest, CancelDuringDelay) {
  TestRetryCancelDuringDelay(*this, std::make_unique<CancelCancellationMode>());
}

TEST_P(RetryTest, DeadlineDuringDelay) {
  TestRetryCancelDuringDelay(*this,
                             std::make_unique<DeadlineCancellationMode>());
}

}  // namespace
}  // namespace grpc_core
