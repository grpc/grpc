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

#include "gtest/gtest.h"
#include "src/core/util/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

CORE_END2END_TEST(CoreEnd2endTests, SimpleMetadata) {
  auto c = NewClientCall("/foo").Timeout(Duration::Minutes(1)).Create();
  IncomingStatusOnClient server_status;
  IncomingMetadata server_initial_metadata;
  IncomingMessage server_message;
  c.NewBatch(1)
      .SendInitialMetadata({{"key1", "val1"}, {"key2", "val2"}})
      .SendMessage("hello world")
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvMessage(server_message)
      .RecvStatusOnClient(server_status);
  auto s = RequestCall(101);
  Expect(101, true);
  Step();
  IncomingMessage client_message;
  s.NewBatch(102)
      .SendInitialMetadata({{"key3", "val3"}, {"key4", "val4"}})
      .RecvMessage(client_message);
  Expect(102, true);
  Step();
  IncomingCloseOnServer client_close;
  s.NewBatch(103)
      .RecvCloseOnServer(client_close)
      .SendMessage("hello you")
      .SendStatusFromServer(GRPC_STATUS_OK, "xyz",
                            {{"key5", "val5"}, {"key6", "val6"}});
  Expect(103, true);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_OK);
  EXPECT_EQ(server_status.message(), IsErrorFlattenEnabled() ? "" : "xyz");
  EXPECT_EQ(s.method(), "/foo");
  EXPECT_FALSE(client_close.was_cancelled());
  EXPECT_EQ(server_message.payload(), "hello you");
  EXPECT_EQ(client_message.payload(), "hello world");
  EXPECT_EQ(s.GetInitialMetadata("key1"), "val1");
  EXPECT_EQ(s.GetInitialMetadata("key2"), "val2");
  EXPECT_EQ(server_initial_metadata.Get("key3"), "val3");
  EXPECT_EQ(server_initial_metadata.Get("key4"), "val4");
  EXPECT_EQ(server_status.GetTrailingMetadata("key5"), "val5");
  EXPECT_EQ(server_status.GetTrailingMetadata("key6"), "val6");
}

TEST(Fuzzers, CoreEnd2endTestsSimpleMetadataRegression1) {
  CoreEnd2endTests_SimpleMetadata(
      CoreTestConfigurationNamed("ChaoticGoodOneByteChunk"),
      ParseTestProto(R"pb(config_vars { trace: "promise_primitives" })pb"));
}

}  // namespace
}  // namespace grpc_core
