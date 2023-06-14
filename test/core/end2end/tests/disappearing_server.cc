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

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

#ifndef GPR_WINDOWS  // b/148110727 for more details
namespace grpc_core {

static void OneRequestAndShutdownServer(CoreEnd2endTest& test) {
  gpr_log(GPR_ERROR, "Create client side call");
  auto c = test.NewClientCall("/service/method")
               .Timeout(Duration::Seconds(30))
               .Create();
  CoreEnd2endTest::IncomingMetadata server_initial_md;
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  gpr_log(GPR_ERROR, "Start initial batch");
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_md)
      .RecvStatusOnClient(server_status);
  auto s = test.RequestCall(101);
  test.Expect(101, true);
  test.Expect(
      1, CoreEnd2endTest::MaybePerformAction{[&](bool success) {
        Crash(absl::StrCat(
            "Unexpected completion of client side call: success=",
            success ? "true" : "false", " status=", server_status.ToString(),
            " initial_md=", server_initial_md.ToString()));
      }});
  test.Step();
  test.ShutdownServerAndNotify(1000);
  CoreEnd2endTest::IncomingCloseOnServer client_closed;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_UNIMPLEMENTED, "xyz", {})
      .RecvCloseOnServer(client_closed);
  test.Expect(102, true);
  test.Expect(1, true);
  test.Expect(1000, true);
  test.Step();
  // Please refer https://github.com/grpc/grpc/issues/21221 for additional
  // details.
  // TODO(yashykt@) - The following line should be removeable after C-Core
  // correctly handles GOAWAY frames. Internal Reference b/135458602. If this
  // test remains flaky even after this, an alternative fix would be to send a
  // request when the server is in the shut down state.
  //
  test.Step();

  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNIMPLEMENTED);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_FALSE(client_closed.was_cancelled());
}

CORE_END2END_TEST(CoreClientChannelTest, DisappearingServer) {
  OneRequestAndShutdownServer(*this);
  InitServer(ChannelArgs());
  OneRequestAndShutdownServer(*this);
}

}  // namespace grpc_core
#endif  // GPR_WINDOWS
