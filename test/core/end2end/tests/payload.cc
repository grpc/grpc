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

#include "gtest/gtest.h"

#include <grpc/status.h>

#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/slice/slice.h"
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

  CoreEnd2endTest::IncomingMetadata server_initial_md;
  CoreEnd2endTest::IncomingMessage server_message;
  CoreEnd2endTest::IncomingStatusOnClient server_status;
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

  CoreEnd2endTest::IncomingMessage client_message;
  s.NewBatch(102).SendInitialMetadata({}).RecvMessage(client_message);
  test.Expect(102, true);
  test.Step();

  CoreEnd2endTest::IncomingCloseOnServer client_close;
  s.NewBatch(103)
      .RecvCloseOnServer(client_close)
      .SendMessage(response_slice.Ref())
      .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
  test.Expect(103, true);
  test.Expect(1, true);
  test.Step();

  EXPECT_EQ(server_status.status(), GRPC_STATUS_OK);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/foo");
  EXPECT_FALSE(client_close.was_cancelled());
  EXPECT_EQ(client_message.payload(), request_slice);
  EXPECT_EQ(server_message.payload(), response_slice);
}
}  // namespace

// Client sends a request with payload, server reads then returns a response
// payload and status.
CORE_END2END_TEST(CoreLargeSendTest, RequestResponseWithPayload) {
  RequestResponseWithPayload(*this);
}

CORE_END2END_TEST(CoreLargeSendTest, RequestResponseWithPayload10Times) {
  for (int i = 0; i < 10; i++) {
    RequestResponseWithPayload(*this);
  }
}

}  // namespace grpc_core
