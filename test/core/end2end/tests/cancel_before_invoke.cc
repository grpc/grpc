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

#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {

CORE_END2END_TEST(CoreEnd2endTest, CancelBeforeInvoke6) {
  auto c = NewClientCall("/service/method").Create();
  c.Cancel();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  CoreEnd2endTest::IncomingMessage server_message;
  c.NewBatch(1)
      .RecvStatusOnClient(server_status)
      .SendInitialMetadata({})
      .SendMessage(RandomSlice(1024))
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvMessage(server_message);
  Expect(1, AnyStatus());
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_CANCELLED);
}

CORE_END2END_TEST(CoreEnd2endTest, CancelBeforeInvoke5) {
  auto c = NewClientCall("/service/method").Create();
  c.Cancel();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .RecvStatusOnClient(server_status)
      .SendInitialMetadata({})
      .SendMessage(RandomSlice(1024))
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata);
  Expect(1, AnyStatus());
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_CANCELLED);
}

CORE_END2END_TEST(CoreEnd2endTest, CancelBeforeInvoke4) {
  auto c = NewClientCall("/service/method").Create();
  c.Cancel();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .RecvStatusOnClient(server_status)
      .SendInitialMetadata({})
      .SendMessage(RandomSlice(1024))
      .SendCloseFromClient();
  Expect(1, AnyStatus());
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_CANCELLED);
}

CORE_END2END_TEST(CoreEnd2endTest, CancelBeforeInvoke3) {
  auto c = NewClientCall("/service/method").Create();
  c.Cancel();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .RecvStatusOnClient(server_status)
      .SendInitialMetadata({})
      .SendMessage(RandomSlice(1024));
  Expect(1, AnyStatus());
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_CANCELLED);
}

CORE_END2END_TEST(CoreEnd2endTest, CancelBeforeInvoke2) {
  auto c = NewClientCall("/service/method").Create();
  c.Cancel();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  c.NewBatch(1).RecvStatusOnClient(server_status).SendInitialMetadata({});
  Expect(1, AnyStatus());
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_CANCELLED);
}

CORE_END2END_TEST(CoreEnd2endTest, CancelBeforeInvoke1) {
  auto c = NewClientCall("/service/method").Create();
  c.Cancel();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  c.NewBatch(1).RecvStatusOnClient(server_status);
  Expect(1, AnyStatus());
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_CANCELLED);
}

}  // namespace grpc_core
