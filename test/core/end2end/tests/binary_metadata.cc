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
#include "src/core/lib/slice/slice.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {

static void BinaryMetadata(CoreEnd2endTest& test, bool server_true_binary,
                           bool client_true_binary) {
  test.InitServer(
      ChannelArgs().Set(GRPC_ARG_HTTP2_ENABLE_TRUE_BINARY, server_true_binary));
  test.InitClient(
      ChannelArgs().Set(GRPC_ARG_HTTP2_ENABLE_TRUE_BINARY, client_true_binary));

  auto key1_payload = RandomBinarySlice(32);
  auto key2_payload = RandomBinarySlice(18);
  auto key3_payload = RandomBinarySlice(17);
  auto key4_payload = RandomBinarySlice(68);
  auto key5_payload = RandomBinarySlice(33);
  auto key6_payload = RandomBinarySlice(2);
  auto request_payload = RandomBinarySlice(7);
  auto response_payload = RandomBinarySlice(9);
  auto status_string = RandomBinarySlice(256);

  auto c = test.NewClientCall("/foo").Timeout(Duration::Minutes(1)).Create();
  CoreEnd2endTest::IncomingMetadata server_initial_md;
  CoreEnd2endTest::IncomingMessage server_message;
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({
          {"key1-bin", key1_payload.as_string_view()},
          {"key2-bin", key2_payload.as_string_view()},
      })
      .SendMessage(request_payload.Ref())
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_md)
      .RecvMessage(server_message)
      .RecvStatusOnClient(server_status);
  auto s = test.RequestCall(101);
  test.Expect(101, true);
  test.Step();
  CoreEnd2endTest::IncomingMessage client_message;
  s.NewBatch(102)
      .SendInitialMetadata({
          {"key3-bin", key3_payload.as_string_view()},
          {"key4-bin", key4_payload.as_string_view()},
      })
      .RecvMessage(client_message);
  test.Expect(102, true);
  test.Step();
  CoreEnd2endTest::IncomingCloseOnServer client_close;
  s.NewBatch(103)
      .RecvCloseOnServer(client_close)
      .SendMessage(response_payload.Ref())
      .SendStatusFromServer(GRPC_STATUS_OK, status_string.as_string_view(),
                            {
                                {"key5-bin", key5_payload.as_string_view()},
                                {"key6-bin", key6_payload.as_string_view()},
                            });
  test.Expect(103, true);
  test.Expect(1, true);
  test.Step();

  EXPECT_EQ(server_status.status(), GRPC_STATUS_OK);
  EXPECT_EQ(server_status.message(), status_string.as_string_view());
  EXPECT_EQ(s.method(), "/foo");
  EXPECT_FALSE(client_close.was_cancelled());
  EXPECT_EQ(client_message.payload(), request_payload);
  EXPECT_EQ(server_message.payload(), response_payload);
  EXPECT_EQ(s.GetInitialMetadata("key1-bin"), key1_payload.as_string_view());
  EXPECT_EQ(s.GetInitialMetadata("key2-bin"), key2_payload.as_string_view());
  EXPECT_EQ(server_initial_md.Get("key3-bin"), key3_payload.as_string_view());
  EXPECT_EQ(server_initial_md.Get("key4-bin"), key4_payload.as_string_view());
  EXPECT_EQ(server_status.GetTrailingMetadata("key5-bin"),
            key5_payload.as_string_view());
  EXPECT_EQ(server_status.GetTrailingMetadata("key6-bin"),
            key6_payload.as_string_view());
}

CORE_END2END_TEST(CoreEnd2endTest,
                  BinaryMetadataServerTrueBinaryClientHttp2Fallback) {
  BinaryMetadata(*this, true, false);
}

CORE_END2END_TEST(CoreEnd2endTest,
                  BinaryMetadataServerHttp2FallbackClientTrueBinary) {
  BinaryMetadata(*this, false, true);
}

CORE_END2END_TEST(CoreEnd2endTest,
                  BinaryMetadataServerTrueBinaryClientTrueBinary) {
  BinaryMetadata(*this, true, true);
}

CORE_END2END_TEST(CoreEnd2endTest,
                  BinaryMetadataServerHttp2FallbackClientHttp2Fallback) {
  // TODO(vigneshbabu): re-enable these before release
  SKIP_IF_USES_EVENT_ENGINE_CLIENT();
  BinaryMetadata(*this, false, false);
}

}  // namespace grpc_core
