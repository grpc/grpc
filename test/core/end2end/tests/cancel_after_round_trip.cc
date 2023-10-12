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

#include <memory>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/tests/cancel_test_helpers.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace {

void CancelAfterRoundTrip(CoreEnd2endTest& test,
                          std::unique_ptr<CancellationMode> mode,
                          Duration timeout) {
  auto c = test.NewClientCall("/service/method").Timeout(timeout).Create();
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  CoreEnd2endTest::IncomingMessage server_message;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage(RandomSlice(100))
      .RecvInitialMetadata(server_initial_metadata)
      .RecvMessage(server_message);
  auto s = test.RequestCall(101);
  test.Expect(101, true);
  test.Step();
  CoreEnd2endTest::IncomingMessage client_message;
  s.NewBatch(102)
      .RecvMessage(client_message)
      .SendInitialMetadata({})
      .SendMessage(RandomSlice(100));
  test.Expect(102, true);
  test.Expect(1, true);
  test.Step();
  CoreEnd2endTest::IncomingMessage server_message_2;
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  c.NewBatch(2).RecvMessage(server_message_2).RecvStatusOnClient(server_status);
  mode->Apply(c);
  CoreEnd2endTest::IncomingCloseOnServer client_close;
  s.NewBatch(103).RecvCloseOnServer(client_close).SendMessage(RandomSlice(100));
  test.Expect(2, true);
  test.Expect(103, true);
  test.Step();
  EXPECT_THAT(server_status.status(),
              ::testing::AnyOf(mode->ExpectedStatus(), GRPC_STATUS_INTERNAL));
  EXPECT_TRUE(client_close.was_cancelled());
}

CORE_END2END_TEST(CoreEnd2endTest, CancelAfterRoundTrip) {
  CancelAfterRoundTrip(*this, std::make_unique<CancelCancellationMode>(),
                       Duration::Seconds(5));
}

CORE_END2END_TEST(CoreDeadlineTest, DeadlineAfterRoundTrip) {
  CancelAfterRoundTrip(*this, std::make_unique<DeadlineCancellationMode>(),
                       Duration::Seconds(5));
}

CORE_END2END_TEST(CoreClientChannelTest,
                  DeadlineAfterRoundTripWithServiceConfig) {
  InitServer(ChannelArgs());
  InitClient(ChannelArgs().Set(
      GRPC_ARG_SERVICE_CONFIG,
      absl::StrCat(
          "{\n"
          "  \"methodConfig\": [ {\n"
          "    \"name\": [\n"
          "      { \"service\": \"service\", \"method\": \"method\" }\n"
          "    ],\n"
          "    \"timeout\": \"",
          5 * grpc_test_slowdown_factor(),
          "s\"\n"
          "  } ]\n"
          "}")));
  CancelAfterRoundTrip(*this, std::make_unique<DeadlineCancellationMode>(),
                       Duration::Infinity());
}

}  // namespace
}  // namespace grpc_core
