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

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

void SimpleRequestBody(CoreEnd2endTest& test) {
  auto c = test.NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
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
}

CORE_END2END_TEST(Http2SingleHopTest, MaxConcurrentStreams) {
  SKIP_IF_MINSTACK();
  InitServer(ChannelArgs().Set(GRPC_ARG_MAX_CONCURRENT_STREAMS, 1));
  InitClient(ChannelArgs());
  // perform a ping-pong to ensure that settings have had a chance to round
  // trip
  SimpleRequestBody(*this);
  // perform another one to make sure that the one stream case still works
  SimpleRequestBody(*this);
  // start two requests - ensuring that the second is not accepted until
  // the first completes
  auto c1 = NewClientCall("/alpha").Timeout(Duration::Seconds(1000)).Create();
  auto c2 = NewClientCall("/beta").Timeout(Duration::Seconds(1000)).Create();
  auto s1 = RequestCall(101);
  c1.NewBatch(301).SendInitialMetadata({}).SendCloseFromClient();
  IncomingMetadata server_initial_metadata1;
  IncomingStatusOnClient server_status1;
  c1.NewBatch(302)
      .RecvStatusOnClient(server_status1)
      .RecvInitialMetadata(server_initial_metadata1);
  c2.NewBatch(401).SendInitialMetadata({}).SendCloseFromClient();
  IncomingMetadata server_initial_metadata2;
  IncomingStatusOnClient server_status2;
  c2.NewBatch(402)
      .RecvStatusOnClient(server_status2)
      .RecvInitialMetadata(server_initial_metadata2);
  bool got_client_start = false;
  bool got_server_start = false;
  int live_call;
  Expect(101, MaybePerformAction{[&got_server_start](bool ok) {
           EXPECT_TRUE(ok);
           got_server_start = ok;
         }});
  Expect(301, MaybePerformAction{[&got_client_start, &live_call](bool ok) {
           EXPECT_FALSE(got_client_start);
           EXPECT_TRUE(ok);
           got_client_start = ok;
           live_call = 300;
         }});
  Expect(401, MaybePerformAction{[&got_client_start, &live_call](bool ok) {
           EXPECT_FALSE(got_client_start);
           EXPECT_TRUE(ok);
           got_client_start = ok;
           live_call = 400;
         }});
  Step();
  if (got_client_start && !got_server_start) {
    Expect(101, true);
    Step();
    got_server_start = true;
  } else if (got_server_start && !got_client_start) {
    Expect(301, MaybePerformAction{[&got_client_start, &live_call](bool ok) {
             EXPECT_FALSE(got_client_start);
             EXPECT_TRUE(ok);
             got_client_start = ok;
             live_call = 300;
           }});
    Expect(401, MaybePerformAction{[&got_client_start, &live_call](bool ok) {
             EXPECT_FALSE(got_client_start);
             EXPECT_TRUE(ok);
             got_client_start = ok;
             live_call = 400;
           }});
    Step();
    EXPECT_TRUE(got_client_start);
  }
  IncomingCloseOnServer client_close;
  s1.NewBatch(102)
      .SendInitialMetadata({})
      .RecvCloseOnServer(client_close)
      .SendStatusFromServer(GRPC_STATUS_UNIMPLEMENTED, "xyz", {});
  Expect(102, true);
  Expect(live_call + 2, true);
  // first request is finished, we should be able to start the second
  live_call = (live_call == 300) ? 400 : 300;
  Expect(live_call + 1, true);
  Step();
  auto s2 = RequestCall(201);
  Expect(201, true);
  Step();
  s2.NewBatch(202)
      .SendInitialMetadata({})
      .RecvCloseOnServer(client_close)
      .SendStatusFromServer(GRPC_STATUS_UNIMPLEMENTED, "xyz", {});
  Expect(live_call + 2, true);
  Expect(202, true);
  Step();
}

CORE_END2END_TEST(Http2SingleHopTest, MaxConcurrentStreamsTimeoutOnFirst) {
  SKIP_IF_MINSTACK();
  InitServer(ChannelArgs().Set(GRPC_ARG_MAX_CONCURRENT_STREAMS, 1));
  InitClient(ChannelArgs());
  // perform a ping-pong to ensure that settings have had a chance to round
  // trip
  SimpleRequestBody(*this);
  // perform another one to make sure that the one stream case still works
  SimpleRequestBody(*this);
  // start two requests - ensuring that the second is not accepted until
  // the first completes
  auto c1 = NewClientCall("/alpha").Timeout(Duration::Seconds(3)).Create();
  auto c2 = NewClientCall("/beta").Timeout(Duration::Seconds(1000)).Create();
  auto s1 = RequestCall(101);
  c1.NewBatch(301).SendInitialMetadata({}).SendCloseFromClient();
  IncomingMetadata server_initial_metadata1;
  IncomingStatusOnClient server_status1;
  c1.NewBatch(302)
      .RecvStatusOnClient(server_status1)
      .RecvInitialMetadata(server_initial_metadata1);
  Expect(101, true);
  Expect(301, true);
  Step();
  c2.NewBatch(401).SendInitialMetadata({}).SendCloseFromClient();
  IncomingMetadata server_initial_metadata2;
  IncomingStatusOnClient server_status2;
  c2.NewBatch(402)
      .RecvStatusOnClient(server_status2)
      .RecvInitialMetadata(server_initial_metadata2);
  auto s2 = RequestCall(201);
  Expect(302, true);
  // first request is finished, we should be able to start the second
  Expect(401, true);
  Expect(201, true);
  Step();
  IncomingCloseOnServer client_close2;
  s2.NewBatch(202)
      .SendInitialMetadata({})
      .RecvCloseOnServer(client_close2)
      .SendStatusFromServer(GRPC_STATUS_UNIMPLEMENTED, "xyz", {});
  Expect(402, true);
  Expect(202, true);
  Step();
}

CORE_END2END_TEST(Http2SingleHopTest, MaxConcurrentStreamsTimeoutOnSecond) {
  SKIP_IF_MINSTACK();
  InitServer(ChannelArgs().Set(GRPC_ARG_MAX_CONCURRENT_STREAMS, 1));
  InitClient(ChannelArgs());
  // perform a ping-pong to ensure that settings have had a chance to round
  // trip
  SimpleRequestBody(*this);
  // perform another one to make sure that the one stream case still works
  SimpleRequestBody(*this);
  // start two requests - ensuring that the second is not accepted until
  // the first completes , and the second request will timeout in the
  // concurrent_list
  auto c1 = NewClientCall("/alpha").Timeout(Duration::Seconds(1000)).Create();
  auto c2 = NewClientCall("/beta").Timeout(Duration::Seconds(3)).Create();
  auto s1 = RequestCall(101);
  c1.NewBatch(301).SendInitialMetadata({}).SendCloseFromClient();
  IncomingMetadata server_initial_metadata1;
  IncomingStatusOnClient server_status1;
  c1.NewBatch(302)
      .RecvStatusOnClient(server_status1)
      .RecvInitialMetadata(server_initial_metadata1);
  Expect(101, true);
  Expect(301, true);
  Step();
  c2.NewBatch(401).SendInitialMetadata({}).SendCloseFromClient();
  IncomingMetadata server_initial_metadata2;
  IncomingStatusOnClient server_status2;
  c2.NewBatch(402)
      .RecvStatusOnClient(server_status2)
      .RecvInitialMetadata(server_initial_metadata2);
  // the second request is time out
  Expect(401, false);
  Expect(402, true);
  Step();
  // now reply the first call
  IncomingCloseOnServer client_close1;
  s1.NewBatch(102)
      .SendInitialMetadata({})
      .RecvCloseOnServer(client_close1)
      .SendStatusFromServer(GRPC_STATUS_UNIMPLEMENTED, "xyz", {});
  Expect(302, true);
  Expect(102, true);
  Step();
}

}  // namespace
}  // namespace grpc_core
