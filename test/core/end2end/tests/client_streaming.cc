//
//
// Copyright 2020 gRPC authors.
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

#include "gtest/gtest.h"

#include <grpc/status.h>

#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

// Client streaming test where the client sends a bunch of messages and the
// server reads them. After reading some messages, the server sends the status.
// Client writes fail after that due to the end of stream and the client
// subsequently requests and receives the status.
void ClientStreaming(CoreEnd2endTest& test, int messages) {
  auto c = test.NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();

  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  c.NewBatch(1).SendInitialMetadata({}).RecvInitialMetadata(
      server_initial_metadata);
  auto s = test.RequestCall(100);
  test.Expect(100, true);
  test.Step();
  s.NewBatch(101).SendInitialMetadata({});
  test.Expect(101, true);
  test.Expect(1, true);
  test.Step();
  // Client writes bunch of messages and server reads them
  for (int i = 0; i < messages; i++) {
    c.NewBatch(2).SendMessage("hello world");
    CoreEnd2endTest::IncomingMessage client_message;
    s.NewBatch(102).RecvMessage(client_message);
    test.Expect(2, true);
    test.Expect(102, true);
    test.Step();
    EXPECT_EQ(client_message.payload(), "hello world");
  }

  // Server sends status denoting end of stream
  s.NewBatch(103).SendStatusFromServer(GRPC_STATUS_UNIMPLEMENTED, "xyz", {});
  test.Expect(103, true);
  test.Step();
  // Do an empty verify to make sure that the client receives the status
  test.Step();

  // Client tries sending another message which should fail
  c.NewBatch(3).SendMessage("hello world");
  test.Expect(3, false);
  test.Step();

  // Client sends close and requests status
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  c.NewBatch(4).SendCloseFromClient().RecvStatusOnClient(server_status);
  test.Expect(4, true);
  test.Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNIMPLEMENTED);
  EXPECT_EQ(server_status.message(), "xyz");
}

CORE_END2END_TEST(CoreEnd2endTest, ClientStreaming0) {
  ClientStreaming(*this, 0);
}
CORE_END2END_TEST(CoreEnd2endTest, ClientStreaming1) {
  ClientStreaming(*this, 1);
}
CORE_END2END_TEST(CoreEnd2endTest, ClientStreaming3) {
  ClientStreaming(*this, 3);
}
CORE_END2END_TEST(CoreEnd2endTest, ClientStreaming10) {
  ClientStreaming(*this, 10);
}
CORE_END2END_TEST(CoreEnd2endTest, ClientStreaming30) {
  ClientStreaming(*this, 30);
}

}  // namespace
}  // namespace grpc_core
