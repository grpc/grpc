// Copyright 2023 gRPC authors.
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

#include <string.h>

#include <memory>

#include "gtest/gtest.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/slice/slice.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

CORE_END2END_TEST(CoreDeadlineTest, TimeoutBeforeRequestCall) {
  auto c = NewClientCall("/foo").Timeout(Duration::Seconds(1)).Create();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  Expect(1, true);
  Step(Duration::Seconds(2));
  EXPECT_EQ(server_status.status(), GRPC_STATUS_DEADLINE_EXCEEDED);
  auto s = RequestCall(2);
  Expect(2, true);
  Step(Duration::Milliseconds(100));
  IncomingCloseOnServer client_close;
  s.NewBatch(3).RecvCloseOnServer(client_close);
  Expect(3, true);
  Step(Duration::Milliseconds(100));
  EXPECT_EQ(client_close.was_cancelled(), true);
}

}  // namespace
}  // namespace grpc_core
