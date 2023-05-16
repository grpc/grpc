//
//
// Copyright 2015 gRPC authors.
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

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/tests/cancel_test_helpers.h"

namespace grpc_core {

// Cancel after accept, no payload
void CancelAfterAccept(CoreEnd2endTest& test,
                       std::unique_ptr<CancellationMode> cancellation_mode,
                       Duration timeout) {
  auto c = test.NewClientCall("/service/method").Timeout(timeout).Create();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  CoreEnd2endTest::IncomingMessage server_message;
  c.NewBatch(1)
      .RecvStatusOnClient(server_status)
      .SendInitialMetadata({})
      .SendMessage(RandomSlice(1024))
      .RecvInitialMetadata(server_initial_metadata)
      .RecvMessage(server_message);
  auto s = test.RequestCall(2);
  test.Expect(2, true);
  test.Step();
  CoreEnd2endTest::IncomingMessage client_message;
  CoreEnd2endTest::IncomingCloseOnServer client_close;
  s.NewBatch(3)
      .RecvMessage(client_message)
      .SendInitialMetadata({})
      .SendMessage(RandomSlice(1024))
      .RecvCloseOnServer(client_close);
  cancellation_mode->Apply(c);
  test.Expect(1, true);
  test.Expect(3, true);
  test.Step();
  EXPECT_THAT(server_status.status(),
              ::testing::AnyOf(cancellation_mode->ExpectedStatus(),
                               GRPC_STATUS_INTERNAL));
  EXPECT_TRUE(client_close.was_cancelled());
}

CORE_END2END_TEST(CoreEnd2endTest, CancelAfterAccept) {
  CancelAfterAccept(*this, std::make_unique<CancelCancellationMode>(),
                    Duration::Seconds(5));
}

CORE_END2END_TEST(CoreDeadlineTest, DeadlineAfterAccept) {
  CancelAfterAccept(*this, std::make_unique<DeadlineCancellationMode>(),
                    Duration::Seconds(5));
}

CORE_END2END_TEST(CoreClientChannelTest, DeadlineAfterAcceptWithServiceConfig) {
  SKIP_IF_USES_EVENT_ENGINE_CLIENT();
  InitServer(ChannelArgs());
  InitClient(ChannelArgs().Set(
      GRPC_ARG_SERVICE_CONFIG,
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"service\", \"method\": \"method\" },\n"
      "      { \"service\": \"unused\" }\n"
      "    ],\n"
      "    \"timeout\": \"5s\"\n"
      "  } ]\n"
      "}"));
  CancelAfterAccept(*this, std::make_unique<DeadlineCancellationMode>(),
                    Duration::Infinity());
}

}  // namespace grpc_core
