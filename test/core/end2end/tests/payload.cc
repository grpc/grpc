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

#include <grpc/status.h>

#include <memory>

#include "absl/strings/match.h"
#include "gtest/gtest.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/util/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {

namespace {
void RequestResponseWithPayload(CoreEnd2endTest& test) {
  // Create large request and response bodies. These are big enough to require
  // multiple round trips to deliver to the peer, and their exact contents of
  // will be verified on completion.
  auto request_slice = RandomSlice(1024 * 1024);
  auto response_slice = RandomSlice(1024 * 1024);

  auto c = test.NewClientCall("/foo").Timeout(Duration::Seconds(60)).Create();

  IncomingMetadata server_initial_md;
  IncomingMessage server_message;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage(request_slice.Ref())
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_md)
      .RecvMessage(server_message)
      .RecvStatusOnClient(server_status);

  CoreEnd2endTest::IncomingCall s = test.RequestCall(101);
  test.Expect(101, true);
  test.Step();

  IncomingMessage client_message;
  s.NewBatch(102).SendInitialMetadata({}).RecvMessage(client_message);
  test.Expect(102, true);
  test.Step();

  IncomingCloseOnServer client_close;
  s.NewBatch(103)
      .RecvCloseOnServer(client_close)
      .SendMessage(response_slice.Ref())
      .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
  test.Expect(103, true);
  test.Expect(1, true);
  test.Step();

  EXPECT_EQ(server_status.status(), GRPC_STATUS_OK);
  EXPECT_EQ(server_status.message(), IsErrorFlattenEnabled() ? "" : "xyz");
  EXPECT_EQ(s.method(), "/foo");
  EXPECT_FALSE(client_close.was_cancelled());
  EXPECT_EQ(client_message.payload(), request_slice);
  EXPECT_EQ(server_message.payload(), response_slice);
}
}  // namespace

// Client sends a request with payload, server reads then returns a response
// payload and status.
CORE_END2END_TEST(CoreLargeSendTests, RequestResponseWithPayload) {
  RequestResponseWithPayload(*this);
}

CORE_END2END_TEST(CoreLargeSendTests, RequestResponseWithPayload10Times) {
  if (fuzzing() && absl::StrContains(test_config()->name, "Compression")) {
    GTEST_SKIP()
        << "Skipping, since the fuzzer will time out with compression enabled.";
  }
  for (int i = 0; i < 10; i++) {
    RequestResponseWithPayload(*this);
  }
}

TEST(Fuzzers, CoreLargeSendTests_RequestResponseWithPayload10TimesRegression1) {
  // With the fuzzer exception removed from RequestResponseWithPayload10Times,
  // this configuration regularly timed out with gzip taking ~3s, and deflate
  // taking 1-2s.
  CoreLargeSendTests_RequestResponseWithPayload10Times(
      CoreTestConfigurationNamed("Chttp2FullstackCompression"),
      ParseTestProto(
          R"pb(event_engine_actions { assign_ports: 2147483647 })pb"));
}
}  // namespace grpc_core
