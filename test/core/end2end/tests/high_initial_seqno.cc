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

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

void SimpleRequest(CoreEnd2endTest& test) {
  auto c = test.NewClientCall("/foo").Timeout(Duration::Minutes(1)).Create();
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = test.RequestCall(101);
  test.Expect(101, true);
  test.Step();
  CoreEnd2endTest::IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_UNIMPLEMENTED, "xyz", {})
      .RecvCloseOnServer(client_close);
  test.Expect(102, true);
  test.Expect(1, true);
  test.Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNIMPLEMENTED);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/foo");
  EXPECT_FALSE(client_close.was_cancelled());
  // TODO(ctiller): this rate limits the test, and it should be removed when
  //                retry has been implemented; until then cross-thread chatter
  //                may result in some requests needing to be cancelled due to
  //                seqno exhaustion.
  test.Step();
}

void TenRequests(CoreEnd2endTest& test, int initial_sequence_number) {
  test.InitServer(ChannelArgs());
  test.InitClient(ChannelArgs().Set(GRPC_ARG_HTTP2_INITIAL_SEQUENCE_NUMBER,
                                    initial_sequence_number));
  for (int i = 0; i < 10; i++) {
    SimpleRequest(test);
  }
}

CORE_END2END_TEST(Http2Test, HighInitialSeqno) { TenRequests(*this, 16777213); }
CORE_END2END_TEST(RetryHttp2Test, HighInitialSeqno) {
  TenRequests(*this, 2147483645);
}

}  // namespace
}  // namespace grpc_core
