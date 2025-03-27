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

#include <grpc/grpc.h>
#include <grpc/status.h>

#include <memory>

#include "gtest/gtest.h"
#include "src/core/util/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
// Client sends a request with payload, server reads then returns status.
CORE_END2END_TEST(WriteBufferingTests, WriteBufferingWorks) {
  auto c = NewClientCall("/foo").Timeout(Duration::Minutes(1)).Create();
  c.NewBatch(1).SendInitialMetadata({});
  IncomingMetadata server_initial_metadata;
  c.NewBatch(2).RecvInitialMetadata(server_initial_metadata);
  auto s = RequestCall(101);
  Expect(1, true);  // send message is buffered
  Expect(101, true);
  Step();
  c.NewBatch(3).SendMessage("hello world", GRPC_WRITE_BUFFER_HINT);
  s.NewBatch(102).SendInitialMetadata({});

  // recv message should not succeed yet - it's buffered at the client still
  IncomingMessage request_payload_recv1;
  s.NewBatch(103).RecvMessage(request_payload_recv1);
  Expect(2, true);
  Expect(3, true);
  Expect(102, true);
  Step();

  // send another message, this time not buffered
  c.NewBatch(4).SendMessage("abc123");
  // now the first send should match up with the first recv
  Expect(103, true);
  Expect(4, true);
  Step();

  // and the next recv should be ready immediately also
  IncomingMessage request_payload_recv2;
  s.NewBatch(104).RecvMessage(request_payload_recv2);
  Expect(104, true);
  Step();

  IncomingStatusOnClient server_status;
  c.NewBatch(5).SendCloseFromClient().RecvStatusOnClient(server_status);

  IncomingCloseOnServer client_close;
  s.NewBatch(105)
      .RecvCloseOnServer(client_close)
      .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});

  Expect(105, true);
  Expect(5, true);
  Step();

  EXPECT_EQ(server_status.status(), GRPC_STATUS_OK);
  EXPECT_EQ(server_status.message(), IsErrorFlattenEnabled() ? "" : "xyz");
  EXPECT_EQ(s.method(), "/foo");
  EXPECT_FALSE(client_close.was_cancelled());
  EXPECT_EQ(request_payload_recv1.payload(), "hello world");
  EXPECT_EQ(request_payload_recv2.payload(), "abc123");
}
}  // namespace grpc_core
