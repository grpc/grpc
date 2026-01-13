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

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include <memory>

#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/time.h"
#include "test/core/end2end/end2end_tests.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace {

void SimpleRequestBody(CoreEnd2endTest& test) {
  auto c = test.NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();
  IncomingStatusOnClient server_status;
  IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = test.RequestCall(101);
  test.Expect(101, true);
  test.Step();
  IncomingCloseOnServer client_close;
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

CORE_END2END_TEST(Http2SingleHopTests, MaxConcurrentStreams) {
  SKIP_IF_MINSTACK();
  InitServer(
      DefaultServerArgs()
          .Set(GRPC_ARG_MAX_CONCURRENT_STREAMS, 1)
          .Set(GRPC_ARG_MAX_CONCURRENT_STREAMS_OVERLOAD_PROTECTION, false));
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
  // Spin for a little: we expect to see no events now - and this time allows
  // any overload protection machinery in the transport to settle (chttp2 tries
  // to ensure calls are finished deleting before allowing more requests
  // through).
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

CORE_END2END_TEST(Http2SingleHopTests, MaxConcurrentStreamsTimeoutOnFirst) {
  SKIP_IF_MINSTACK();
  InitServer(
      DefaultServerArgs()
          .Set(GRPC_ARG_MAX_CONCURRENT_STREAMS, 1)
          .Set(GRPC_ARG_MAX_CONCURRENT_STREAMS_OVERLOAD_PROTECTION, false));
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

CORE_END2END_TEST(Http2SingleHopTests, MaxConcurrentStreamsTimeoutOnSecond) {
  SKIP_IF_MINSTACK();
  InitServer(
      DefaultServerArgs()
          .Set(GRPC_ARG_MAX_CONCURRENT_STREAMS, 1)
          .Set(GRPC_ARG_MAX_CONCURRENT_STREAMS_OVERLOAD_PROTECTION, false));
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
  EXPECT_EQ(server_status2.status(), GRPC_STATUS_DEADLINE_EXCEEDED);
}

CORE_END2END_TEST(Http2SingleHopTests, MaxConcurrentStreamsRejectOnClient) {
  SKIP_IF_MINSTACK();
  InitServer(
      DefaultServerArgs()
          .Set(GRPC_ARG_MAX_CONCURRENT_STREAMS, 1)
          .Set(GRPC_ARG_MAX_CONCURRENT_STREAMS_OVERLOAD_PROTECTION, false));
  InitClient(ChannelArgs()
                 .Set(GRPC_ARG_MAX_CONCURRENT_STREAMS_REJECT_ON_CLIENT, true)
                 .Set(GRPC_ARG_ENABLE_RETRIES, false));
  // perform a ping-pong to ensure that settings have had a chance to round
  // trip
  SimpleRequestBody(*this);
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
  // the second request fails
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
  EXPECT_EQ(server_status2.status(), GRPC_STATUS_RESOURCE_EXHAUSTED);
}

CORE_END2END_TEST(Http2SingleHopTests, ServerMaxConcurrentStreams) {
  SKIP_IF_MINSTACK();

  grpc_resource_quota* resource_quota =
      grpc_resource_quota_create("test_server");
  // Set the max outstanding streams on the server to 1.
  grpc_resource_quota_set_max_outstanding_streams(resource_quota, 1);

  InitServer(
      ChannelArgs()
          .Set(GRPC_ARG_RESOURCE_QUOTA,
               ChannelArgs::Pointer(resource_quota,
                                    grpc_resource_quota_arg_vtable()))
          // Set the max concurrent streams to 100.  This is to ensure that the
          // per connection limit does not interfere with the effective limits,
          // and the server wide limit is the effective limit.
          .Set(GRPC_ARG_MAX_CONCURRENT_STREAMS, 100)
          .Set(GRPC_ARG_MAX_CONCURRENT_STREAMS_OVERLOAD_PROTECTION, false));
  InitClient(ChannelArgs()
                 .Set(GRPC_ARG_MAX_CONCURRENT_STREAMS_REJECT_ON_CLIENT, true)
                 .Set(GRPC_ARG_ENABLE_RETRIES, false));

  // perform a few ping-pongs to ensure that server settings have reached the
  // client.
  for (int i = 0; i < 10; ++i) {
    SimpleRequestBody(*this);
  }

  auto c1 = NewClientCall("/alpha").Timeout(Duration::Seconds(1000)).Create();
  auto c2 = NewClientCall("/beta").Timeout(Duration::Seconds(1000)).Create();
  auto c3 = NewClientCall("/gamma").Timeout(Duration::Seconds(3)).Create();

  bool c2_started = false;

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
  // We don't know if c2 will succeed or fail, so we just add expectations
  // to acknowledge the completion of these tags.
  Expect(401, MaybePerformAction{[&c2_started](bool ok) { c2_started = ok; }});
  Expect(402, MaybePerformAction{[](bool ok) {}});

  c3.NewBatch(501).SendInitialMetadata({}).SendCloseFromClient();
  IncomingMetadata server_initial_metadata3;
  IncomingStatusOnClient server_status3;
  c3.NewBatch(502)
      .RecvStatusOnClient(server_status3)
      .RecvInitialMetadata(server_initial_metadata3);

  // We expect c3 to fail with RESOURCE_EXHAUSTED.
  Expect(501, false);
  Expect(502, true);
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

  // If c2 was started, we need to reply to it.
  if (c2_started) {
    auto s2 = RequestCall(201);
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

  // The third request MUST fail with RESOURCE_EXHAUSTED.
  EXPECT_EQ(server_status3.status(), GRPC_STATUS_RESOURCE_EXHAUSTED);
  EXPECT_THAT(server_status3.message(),
              ::testing::HasSubstr("Too many streams"));
}

}  // namespace
}  // namespace grpc_core
