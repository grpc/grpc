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

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "test/core/end2end/end2end_tests.h"

using testing::StartsWith;

namespace grpc_core {
namespace {

void TestMaxMessageLengthOnClientOnRequest(CoreEnd2endTest& test) {
  auto c = test.NewClientCall("/service/method").Create();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage("hello world")
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  test.Expect(1, true);
  test.Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_RESOURCE_EXHAUSTED);
  EXPECT_EQ(server_status.message(), "Sent message larger than max (11 vs. 5)");
}

void TestMaxMessageLengthOnServerOnRequest(CoreEnd2endTest& test) {
  auto c = test.NewClientCall("/service/method").Create();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage("hello world")
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = test.RequestCall(101);
  test.Expect(101, true);
  test.Step();
  CoreEnd2endTest::IncomingCloseOnServer client_close;
  CoreEnd2endTest::IncomingMessage client_message;
  s.NewBatch(102).RecvCloseOnServer(client_close).RecvMessage(client_message);
  test.Expect(102, true);
  test.Expect(1, true);
  test.Step();
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_TRUE(client_close.was_cancelled());
  EXPECT_EQ(server_status.status(), GRPC_STATUS_RESOURCE_EXHAUSTED);
  EXPECT_EQ(server_status.message(),
            "Received message larger than max (11 vs. 5)");
}

void TestMaxMessageLengthOnClientOnResponse(CoreEnd2endTest& test) {
  auto c = test.NewClientCall("/service/method").Create();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  CoreEnd2endTest::IncomingMessage server_message;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvMessage(server_message)
      .RecvStatusOnClient(server_status);
  auto s = test.RequestCall(101);
  test.Expect(101, true);
  test.Step();
  CoreEnd2endTest::IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .RecvCloseOnServer(client_close)
      .SendMessage("hello world")
      .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
  test.Expect(102, true);
  test.Expect(1, true);
  test.Step();
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_EQ(server_status.status(), GRPC_STATUS_RESOURCE_EXHAUSTED);
  EXPECT_EQ(server_status.message(),
            "Received message larger than max (11 vs. 5)");
}

void TestMaxMessageLengthOnServerOnResponse(CoreEnd2endTest& test) {
  auto c = test.NewClientCall("/service/method").Create();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  CoreEnd2endTest::IncomingMessage server_message;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvMessage(server_message)
      .RecvStatusOnClient(server_status);
  auto s = test.RequestCall(101);
  test.Expect(101, true);
  test.Step();
  CoreEnd2endTest::IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .RecvCloseOnServer(client_close)
      .SendMessage("hello world")
      .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
  test.Expect(102, true);
  test.Expect(1, true);
  test.Step();
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_EQ(server_status.status(), GRPC_STATUS_RESOURCE_EXHAUSTED);
  EXPECT_EQ(server_status.message(), "Sent message larger than max (11 vs. 5)");
}

CORE_END2END_TEST(CoreEnd2endTest,
                  MaxMessageLengthOnClientOnRequestViaChannelArg) {
  SKIP_IF_MINSTACK();
  SKIP_IF_CHAOTIC_GOOD();
  InitServer(ChannelArgs());
  InitClient(ChannelArgs().Set(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH, 5));
  TestMaxMessageLengthOnClientOnRequest(*this);
}

CORE_END2END_TEST(
    CoreEnd2endTest,
    MaxMessageLengthOnClientOnRequestViaServiceConfigWithStringJsonValue) {
  SKIP_IF_MINSTACK();
  SKIP_IF_CHAOTIC_GOOD();
  InitServer(ChannelArgs());
  InitClient(ChannelArgs().Set(
      GRPC_ARG_SERVICE_CONFIG,
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"service\", \"method\": \"method\" }\n"
      "    ],\n"
      "    \"maxRequestMessageBytes\": \"5\"\n"
      "  } ]\n"
      "}"));
  TestMaxMessageLengthOnClientOnRequest(*this);
}

CORE_END2END_TEST(
    CoreEnd2endTest,
    MaxMessageLengthOnClientOnRequestViaServiceConfigWithIntegerJsonValue) {
  SKIP_IF_MINSTACK();
  SKIP_IF_CHAOTIC_GOOD();
  InitServer(ChannelArgs());
  InitClient(ChannelArgs().Set(
      GRPC_ARG_SERVICE_CONFIG,
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"service\", \"method\": \"method\" }\n"
      "    ],\n"
      "    \"maxRequestMessageBytes\": 5\n"
      "  } ]\n"
      "}"));
  TestMaxMessageLengthOnClientOnRequest(*this);
}

CORE_END2END_TEST(CoreEnd2endTest,
                  MaxMessageLengthOnServerOnRequestViaChannelArg) {
  SKIP_IF_MINSTACK();
  SKIP_IF_CHAOTIC_GOOD();
  InitServer(ChannelArgs().Set(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, 5));
  InitClient(ChannelArgs());
  TestMaxMessageLengthOnServerOnRequest(*this);
}

CORE_END2END_TEST(CoreEnd2endTest,
                  MaxMessageLengthOnClientOnResponseViaChannelArg) {
  SKIP_IF_MINSTACK();
  SKIP_IF_CHAOTIC_GOOD();
  InitServer(ChannelArgs());
  InitClient(ChannelArgs().Set(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, 5));
  TestMaxMessageLengthOnClientOnResponse(*this);
}

CORE_END2END_TEST(
    CoreEnd2endTest,
    MaxMessageLengthOnClientOnResponseViaServiceConfigWithStringJsonValue) {
  SKIP_IF_MINSTACK();
  SKIP_IF_CHAOTIC_GOOD();
  InitServer(ChannelArgs());
  InitClient(ChannelArgs().Set(
      GRPC_ARG_SERVICE_CONFIG,
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"service\", \"method\": \"method\" }\n"
      "    ],\n"
      "    \"maxResponseMessageBytes\": \"5\"\n"
      "  } ]\n"
      "}"));
  TestMaxMessageLengthOnClientOnResponse(*this);
}

CORE_END2END_TEST(
    CoreEnd2endTest,
    MaxMessageLengthOnClientOnResponseViaServiceConfigWithIntegerJsonValue) {
  SKIP_IF_MINSTACK();
  SKIP_IF_CHAOTIC_GOOD();
  InitServer(ChannelArgs());
  InitClient(ChannelArgs().Set(
      GRPC_ARG_SERVICE_CONFIG,
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"service\", \"method\": \"method\" }\n"
      "    ],\n"
      "    \"maxResponseMessageBytes\": 5\n"
      "  } ]\n"
      "}"));
  TestMaxMessageLengthOnClientOnResponse(*this);
}

CORE_END2END_TEST(CoreEnd2endTest,
                  MaxMessageLengthOnServerOnResponseViaChannelArg) {
  SKIP_IF_MINSTACK();
  SKIP_IF_CHAOTIC_GOOD();
  InitServer(ChannelArgs().Set(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH, 5));
  InitClient(ChannelArgs());
  TestMaxMessageLengthOnServerOnResponse(*this);
}

CORE_END2END_TEST(Http2Test, MaxMessageLengthOnServerOnRequestWithCompression) {
  SKIP_IF_MINSTACK();
  SKIP_IF_CHAOTIC_GOOD();
  // Set limit via channel args.
  InitServer(ChannelArgs().Set(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, 5));
  InitClient(ChannelArgs());
  auto c = NewClientCall("/service/method").Create();
  IncomingStatusOnClient server_status;
  IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({{"grpc-internal-encoding-request", "gzip"}})
      .SendMessage(std::string(1024, 'a'))
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = RequestCall(101);
  Expect(101, true);
  Step();
  IncomingMessage client_message;
  IncomingCloseOnServer client_close;
  s.NewBatch(102).RecvMessage(client_message);
  s.NewBatch(103).RecvCloseOnServer(client_close);
  // WARNING!!
  // It's believed the following line (and the associated batch) is the only
  // test we have for failing a receive operation in a batch.
  Expect(102, false);
  Expect(103, true);
  Expect(1, true);
  Step();
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_TRUE(client_close.was_cancelled());
  EXPECT_EQ(server_status.status(), GRPC_STATUS_RESOURCE_EXHAUSTED);
  EXPECT_THAT(server_status.message(),
              StartsWith("Received message larger than max"));
}

CORE_END2END_TEST(Http2Test,
                  MaxMessageLengthOnClientOnResponseWithCompression) {
  SKIP_IF_MINSTACK();
  SKIP_IF_CHAOTIC_GOOD();
  // Set limit via channel args.
  InitServer(ChannelArgs());
  InitClient(ChannelArgs().Set(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, 5));
  auto c = NewClientCall("/service/method").Create();
  IncomingStatusOnClient server_status;
  IncomingMetadata server_initial_metadata;
  IncomingMessage server_message;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvMessage(server_message)
      .RecvStatusOnClient(server_status);
  auto s = RequestCall(101);
  Expect(101, true);
  Step();
  IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({{"grpc-internal-encoding-request", "gzip"}})
      .RecvCloseOnServer(client_close)
      .SendMessage(std::string(1024, 'a'))
      .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
  Expect(102, true);
  Expect(1, true);
  Step();
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_EQ(server_status.status(), GRPC_STATUS_RESOURCE_EXHAUSTED);
  EXPECT_THAT(server_status.message(),
              StartsWith("Received message larger than max"));
}

}  // namespace
}  // namespace grpc_core
