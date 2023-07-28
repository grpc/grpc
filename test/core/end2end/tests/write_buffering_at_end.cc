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

#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/status.h>

#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

CORE_END2END_TEST(WriteBufferingTest, WriteBufferingAtEnd) {
  auto c = NewClientCall("/foo").Timeout(Duration::Minutes(1)).Create();
  c.NewBatch(1).SendInitialMetadata({});
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  c.NewBatch(2).RecvInitialMetadata(server_initial_metadata);

  auto s = RequestCall(101);
  Expect(1, true);  // send message is buffered
  Expect(101, true);
  Step();

  c.NewBatch(3).SendMessage("hello world", GRPC_WRITE_BUFFER_HINT);
  s.NewBatch(102).SendInitialMetadata({});
  // recv message should not succeed yet - it's buffered at the client still
  CoreEnd2endTest::IncomingMessage request_payload_recv1;
  s.NewBatch(103).RecvMessage(request_payload_recv1);
  Expect(2, true);
  Expect(3, true);
  Expect(102, true);
  Step();

  // send end of stream: should release the buffering
  c.NewBatch(4).SendCloseFromClient();
  // now the first send should match up with the first recv
  Expect(103, true);
  Expect(4, true);
  Step();

  // and the next recv should be ready immediately also (and empty)
  CoreEnd2endTest::IncomingMessage request_payload_recv2;
  s.NewBatch(104).RecvMessage(request_payload_recv2);
  Expect(104, true);
  Step();

  CoreEnd2endTest::IncomingStatusOnClient server_status;
  c.NewBatch(4).RecvStatusOnClient(server_status);
  CoreEnd2endTest::IncomingCloseOnServer client_close;
  s.NewBatch(105)
      .RecvCloseOnServer(client_close)
      .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
  Expect(105, true);
  Expect(4, true);
  Step();

  EXPECT_EQ(server_status.status(), GRPC_STATUS_OK);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/foo");
  EXPECT_FALSE(client_close.was_cancelled());
  EXPECT_EQ(request_payload_recv1.payload(), "hello world");
  EXPECT_TRUE(request_payload_recv2.is_end_of_stream());
}
}  // namespace
}  // namespace grpc_core
