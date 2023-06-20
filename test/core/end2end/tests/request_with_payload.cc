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

#include "gtest/gtest.h"

#include <grpc/status.h>

#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

CORE_END2END_TEST(CoreEnd2endTest, RequestWithPayload) {
  auto c = NewClientCall("/foo").Timeout(Duration::Seconds(30)).Create();
  IncomingMetadata server_initial_metadata;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage("hello world")
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = RequestCall(101);
  Expect(101, true);
  Expect(1, CoreEnd2endTest::MaybePerformAction{[&](bool success) {
           Crash(absl::StrCat(
               "Unexpected completion of client side call: success=",
               success ? "true" : "false", " status=", server_status.ToString(),
               " initial_md=", server_initial_metadata.ToString()));
         }});
  Step();
  IncomingMessage client_message;
  s.NewBatch(102).SendInitialMetadata({}).RecvMessage(client_message);
  Expect(102, true);
  Step();
  IncomingCloseOnServer client_close;
  s.NewBatch(103)
      .RecvCloseOnServer(client_close)
      .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
  Expect(103, true);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_OK);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/foo");
  EXPECT_FALSE(client_close.was_cancelled());
  EXPECT_EQ(client_message.payload(), "hello world");
}

}  // namespace
}  // namespace grpc_core
