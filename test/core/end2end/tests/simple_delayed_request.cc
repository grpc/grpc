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

#include <grpc/grpc.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

CORE_END2END_TEST(Http2SingleHopTest, SimpleDelayedRequestShort) {
  InitClient(ChannelArgs()
                 .Set(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, 1000)
                 .Set(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS, 1000)
                 .Set(GRPC_ARG_MIN_RECONNECT_BACKOFF_MS, 5000));
  gpr_log(GPR_ERROR, "Create client side call");
  auto c = NewClientCall("/foo").Timeout(Duration::Seconds(30)).Create();
  IncomingMetadata server_initial_metadata;
  IncomingStatusOnClient server_status;
  gpr_log(GPR_ERROR, "Start initial batch");
  c.NewBatch(1)
      .SendInitialMetadata({}, GRPC_INITIAL_METADATA_WAIT_FOR_READY)
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  gpr_log(GPR_ERROR, "Start server");
  InitServer(ChannelArgs());
  auto s = RequestCall(101);
  Expect(101, true);
  Step();
  IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_UNIMPLEMENTED, "xyz", {})
      .RecvCloseOnServer(client_close);
  Expect(102, true);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNIMPLEMENTED);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/foo");
  EXPECT_FALSE(client_close.was_cancelled());
}

}  // namespace
}  // namespace grpc_core
