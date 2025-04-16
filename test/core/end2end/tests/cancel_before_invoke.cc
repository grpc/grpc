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

#include "test/core/end2end/end2end_tests.h"
#include "gtest/gtest.h"

namespace grpc_core {

CORE_END2END_TEST(CoreEnd2endTests, CancelBeforeInvoke6) {
  auto c = NewClientCall("/service/method").Create();
  c.Cancel();
  IncomingStatusOnClient server_status;
  IncomingMetadata server_initial_metadata;
  IncomingMessage server_message;
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

CORE_END2END_TEST(CoreEnd2endTests, CancelBeforeInvoke5) {
  auto c = NewClientCall("/service/method").Create();
  c.Cancel();
  IncomingStatusOnClient server_status;
  IncomingMetadata server_initial_metadata;
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

CORE_END2END_TEST(CoreEnd2endTests, CancelBeforeInvoke4) {
  auto c = NewClientCall("/service/method").Create();
  c.Cancel();
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .RecvStatusOnClient(server_status)
      .SendInitialMetadata({})
      .SendMessage(RandomSlice(1024))
      .SendCloseFromClient();
  Expect(1, AnyStatus());
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_CANCELLED);
}

CORE_END2END_TEST(CoreEnd2endTests, CancelBeforeInvoke3) {
  auto c = NewClientCall("/service/method").Create();
  c.Cancel();
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .RecvStatusOnClient(server_status)
      .SendInitialMetadata({})
      .SendMessage(RandomSlice(1024));
  Expect(1, AnyStatus());
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_CANCELLED);
}

CORE_END2END_TEST(CoreEnd2endTests, CancelBeforeInvoke2) {
  auto c = NewClientCall("/service/method").Create();
  c.Cancel();
  IncomingStatusOnClient server_status;
  c.NewBatch(1).RecvStatusOnClient(server_status).SendInitialMetadata({});
  Expect(1, AnyStatus());
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_CANCELLED);
}

CORE_END2END_TEST(CoreEnd2endTests, CancelBeforeInvoke1) {
  auto c = NewClientCall("/service/method").Create();
  c.Cancel();
  IncomingStatusOnClient server_status;
  c.NewBatch(1).RecvStatusOnClient(server_status);
  Expect(1, AnyStatus());
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_CANCELLED);
}

}  // namespace grpc_core
