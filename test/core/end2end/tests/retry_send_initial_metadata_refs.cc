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

#include <string>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

// Tests that we hold refs to send_initial_metadata payload while
// cached, even after the caller has released its refs:
// - 2 retries allowed for ABORTED status
// - first attempt returns ABORTED
// - second attempt returns OK
CORE_END2END_TEST(RetryTest, RetrySendInitialMetadataRefs) {
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
  c.NewBatch(1)
      .SendInitialMetadata(
          {// First element is short enough for slices to be inlined.
           {"foo", "bar"},
           // Second element requires slice allocation.
           {std::string(GRPC_SLICE_INLINED_SIZE + 1, 'x'),
            std::string(GRPC_SLICE_INLINED_SIZE + 1, 'y')}})
      .SendMessage("foo")
      .SendCloseFromClient();
  Expect(1, true);
  Step();
  IncomingMessage server_message;
  IncomingMetadata server_initial_metadata;
  IncomingStatusOnClient server_status;
  c.NewBatch(2)
      .RecvMessage(server_message)
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = RequestCall(101);
  Expect(101, true);
  Step();
  // Make sure the "grpc-previous-rpc-attempts" header was not sent in the
  // initial attempt.
  EXPECT_EQ(s.GetInitialMetadata("grpc-previous-rpc-attempts"), absl::nullopt);
  EXPECT_NE(s.GetPeer(), absl::nullopt);
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_ABORTED, "xyz", {})
      .RecvCloseOnServer(client_close);
  Expect(102, true);
  Step();
  auto s2 = RequestCall(201);
  Expect(201, true);
  Step();
  // Make sure the "grpc-previous-rpc-attempts" header was sent in the retry.
  EXPECT_EQ(s2.GetInitialMetadata("grpc-previous-rpc-attempts"), "1");
  // It should also contain the initial metadata, even though the client
  // freed it already.
  EXPECT_EQ(s2.GetInitialMetadata("foo"), "bar");
  EXPECT_EQ(
      s2.GetInitialMetadata(std::string(GRPC_SLICE_INLINED_SIZE + 1, 'x')),
      std::string(GRPC_SLICE_INLINED_SIZE + 1, 'y'));
  EXPECT_NE(s.GetPeer(), absl::nullopt);
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  IncomingMessage client_message;
  s2.NewBatch(202).RecvMessage(client_message);
  IncomingCloseOnServer client_close2;
  s2.NewBatch(203)
      .SendInitialMetadata({})
      .SendMessage("bar")
      .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {})
      .RecvCloseOnServer(client_close2);
  Expect(202, true);
  Expect(203, true);
  Expect(2, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_OK);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_FALSE(client_close.was_cancelled());
}

}  // namespace
}  // namespace grpc_core
