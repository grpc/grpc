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

#include <cstddef>
#include <string>

#include "src/core/lib/channel/channel_args.h"
#include "test/core/end2end/end2end_tests.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::HasSubstr;

namespace grpc_core {
namespace {

// Should be generous enough to not error out during handshake.
constexpr int kMaxMessageLength = 1024;

std::string GetStringOfLength(size_t length) {
  return std::string(length, 'a');
}

std::string GetTooLargeMessage() {
  return GetStringOfLength(kMaxMessageLength + 1);
}

void TestMaxMessageLengthOnClientOnRequest(CoreEnd2endTest& test) {
  auto c = test.NewClientCall("/service/method").Create();
  IncomingStatusOnClient server_status;
  IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage(GetTooLargeMessage())
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  test.Expect(1, true);
  test.Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_RESOURCE_EXHAUSTED);
  EXPECT_THAT(server_status.message(),
              HasSubstr("CLIENT: Sent message larger than max"));
}

void TestMaxMessageLengthOnServerOnRequest(CoreEnd2endTest& test) {
  auto c = test.NewClientCall("/service/method").Create();
  IncomingStatusOnClient server_status;
  IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage(GetTooLargeMessage())
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = test.RequestCall(101);
  test.Expect(101, true);
  test.Step();
  IncomingCloseOnServer client_close;
  IncomingMessage client_message;
  s.NewBatch(102).RecvCloseOnServer(client_close).RecvMessage(client_message);
  test.Expect(102, true);
  test.Expect(1, true);
  test.Step();
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_TRUE(client_close.was_cancelled());
  EXPECT_EQ(server_status.status(), GRPC_STATUS_RESOURCE_EXHAUSTED);
  EXPECT_THAT(server_status.message(),
              HasSubstr("SERVER: Received message larger than max"));
}

void TestMaxMessageLengthOnServerOnRequestEarlyClose(CoreEnd2endTest& test) {
  // Only implemented in ChaoticGood.
  auto c = test.NewClientCall("/service/method").Create();
  IncomingStatusOnClient server_status;
  IncomingMetadata server_initial_metadata;
  c.NewBatch(1).SendInitialMetadata({});
  test.Expect(1, true);
  test.Step();
  auto s = test.RequestCall(101);
  test.Expect(101, true);
  test.Step();
  c.NewBatch(2)
      .SendMessage(GetTooLargeMessage())
      .RecvStatusOnClient(server_status);
  test.Expect(2, true);
  test.Step();

  // Client just receives a socket closed error since the server closes the
  // transport.
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNAVAILABLE);
  EXPECT_THAT(server_status.message(), HasSubstr("CLIENT:"));
  EXPECT_THAT(server_status.message(), HasSubstr("Socket closed"));
}

void TestMaxMessageLengthOnClientOnResponse(CoreEnd2endTest& test) {
  auto c = test.NewClientCall("/service/method").Create();
  IncomingStatusOnClient server_status;
  IncomingMetadata server_initial_metadata;
  IncomingMessage server_message;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvMessage(server_message)
      .RecvStatusOnClient(server_status);
  auto s = test.RequestCall(101);
  test.Expect(101, true);
  test.Step();
  IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .RecvCloseOnServer(client_close)
      .SendMessage(GetTooLargeMessage())
      .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
  test.Expect(102, true);
  test.Expect(1, true);
  test.Step();
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_EQ(server_status.status(), GRPC_STATUS_RESOURCE_EXHAUSTED);
  EXPECT_THAT(server_status.message(),
              HasSubstr("CLIENT: Received message larger than max"));
}

void TestMaxMessageLengthOnServerOnResponse(CoreEnd2endTest& test) {
  auto c = test.NewClientCall("/service/method").Create();
  IncomingStatusOnClient server_status;
  IncomingMetadata server_initial_metadata;
  IncomingMessage server_message;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvMessage(server_message)
      .RecvStatusOnClient(server_status);
  auto s = test.RequestCall(101);
  test.Expect(101, true);
  test.Step();
  IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .RecvCloseOnServer(client_close)
      .SendMessage(GetTooLargeMessage())
      .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
  test.Expect(102, true);
  test.Expect(1, true);
  test.Step();
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_EQ(server_status.status(), GRPC_STATUS_RESOURCE_EXHAUSTED);
  EXPECT_THAT(server_status.message(),
              HasSubstr("SERVER: Sent message larger than max"));
}

CORE_END2END_TEST(CoreEnd2endTests,
                  MaxMessageLengthOnClientOnRequestViaChannelArg) {
  SKIP_IF_MINSTACK();
  InitServer(DefaultServerArgs());
  InitClient(
      ChannelArgs().Set(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH, kMaxMessageLength));
  TestMaxMessageLengthOnClientOnRequest(*this);
}

CORE_END2END_TEST(
    CoreEnd2endTests,
    MaxMessageLengthOnClientOnRequestViaServiceConfigWithStringJsonValue) {
  SKIP_IF_MINSTACK();
  InitServer(DefaultServerArgs());
  InitClient(ChannelArgs().Set(
      GRPC_ARG_SERVICE_CONFIG,
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"service\", \"method\": \"method\" }\n"
      "    ],\n"
      "    \"maxRequestMessageBytes\": \"1024\"\n"
      "  } ]\n"
      "}"));
  TestMaxMessageLengthOnClientOnRequest(*this);
}

CORE_END2END_TEST(
    CoreEnd2endTests,
    MaxMessageLengthOnClientOnRequestViaServiceConfigWithIntegerJsonValue) {
  SKIP_IF_MINSTACK();
  InitServer(DefaultServerArgs());
  InitClient(ChannelArgs().Set(
      GRPC_ARG_SERVICE_CONFIG,
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"service\", \"method\": \"method\" }\n"
      "    ],\n"
      "    \"maxRequestMessageBytes\": 1024\n"
      "  } ]\n"
      "}"));
  TestMaxMessageLengthOnClientOnRequest(*this);
}

CORE_END2END_TEST(CoreEnd2endTests,
                  MaxMessageLengthOnServerOnRequestViaChannelArg) {
  SKIP_IF_MINSTACK();
  if (test_config()->feature_mask &
      FEATURE_MASK_CHECKS_MAX_MESSAGE_LENGTH_IN_TRANSPORT) {
    GTEST_SKIP() << "Skipping test as the transport checks max message length.";
  }
  InitServer(DefaultServerArgs().Set(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH,
                                     kMaxMessageLength));
  InitClient(ChannelArgs());
  TestMaxMessageLengthOnServerOnRequest(*this);
}

CORE_END2END_TEST(CoreEnd2endTests,
                  MaxMessageLengthOnServerOnRequestViaChannelArgEarlyClose) {
  SKIP_IF_MINSTACK();
  if (!(test_config()->feature_mask &
        FEATURE_MASK_CHECKS_MAX_MESSAGE_LENGTH_IN_TRANSPORT)) {
    GTEST_SKIP()
        << "Skipping test as the transport does not check max message length.";
  }
  InitServer(DefaultServerArgs().Set(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH,
                                     kMaxMessageLength));
  InitClient(ChannelArgs());
  TestMaxMessageLengthOnServerOnRequestEarlyClose(*this);
}

CORE_END2END_TEST(CoreEnd2endTests,
                  MaxMessageLengthOnClientOnResponseViaChannelArg) {
  SKIP_IF_MINSTACK();
  InitServer(DefaultServerArgs());
  InitClient(ChannelArgs().Set(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH,
                               kMaxMessageLength));
  TestMaxMessageLengthOnClientOnResponse(*this);
}

CORE_END2END_TEST(
    CoreEnd2endTests,
    MaxMessageLengthOnClientOnResponseViaServiceConfigWithStringJsonValue) {
  SKIP_IF_MINSTACK();
  InitServer(DefaultServerArgs());
  InitClient(ChannelArgs().Set(
      GRPC_ARG_SERVICE_CONFIG,
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"service\", \"method\": \"method\" }\n"
      "    ],\n"
      "    \"maxResponseMessageBytes\": \"1024\"\n"
      "  } ]\n"
      "}"));
  TestMaxMessageLengthOnClientOnResponse(*this);
}

CORE_END2END_TEST(
    CoreEnd2endTests,
    MaxMessageLengthOnClientOnResponseViaServiceConfigWithIntegerJsonValue) {
  SKIP_IF_MINSTACK();
  InitServer(DefaultServerArgs());
  InitClient(ChannelArgs().Set(
      GRPC_ARG_SERVICE_CONFIG,
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"service\", \"method\": \"method\" }\n"
      "    ],\n"
      "    \"maxResponseMessageBytes\": 1024\n"
      "  } ]\n"
      "}"));
  TestMaxMessageLengthOnClientOnResponse(*this);
}

CORE_END2END_TEST(CoreEnd2endTests,
                  MaxMessageLengthOnServerOnResponseViaChannelArg) {
  SKIP_IF_MINSTACK();
  InitServer(DefaultServerArgs().Set(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH,
                                     kMaxMessageLength));
  InitClient(ChannelArgs());
  TestMaxMessageLengthOnServerOnResponse(*this);
}

CORE_END2END_TEST(Http2Tests,
                  MaxMessageLengthOnServerOnRequestWithCompression) {
  SKIP_IF_MINSTACK();
  // Set limit via channel args.
  InitServer(DefaultServerArgs().Set(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, 5));
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
              HasSubstr("SERVER: Received message larger than max"));
}

CORE_END2END_TEST(Http2Tests,
                  MaxMessageLengthOnClientOnResponseWithCompression) {
  SKIP_IF_MINSTACK();
  // Set limit via channel args.
  InitServer(DefaultServerArgs());
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
              HasSubstr("CLIENT: Received message larger than max"));
}

}  // namespace
}  // namespace grpc_core
