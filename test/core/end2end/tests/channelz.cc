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

#include "src/core/lib/channel/channelz.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/server.h"
#include "test/core/end2end/end2end_tests.h"

using testing::HasSubstr;
using testing::Not;

namespace grpc_core {
namespace {

void RunOneRequest(CoreEnd2endTest& test, bool request_is_success) {
  auto c = test.NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();
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
      .SendStatusFromServer(
          request_is_success ? GRPC_STATUS_OK : GRPC_STATUS_UNIMPLEMENTED,
          "xyz", {})
      .RecvCloseOnServer(client_close);
  test.Expect(102, true);
  test.Expect(1, true);
  test.Step();
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/foo");
}

CORE_END2END_TEST(CoreEnd2endTest, Channelz) {
  SKIP_IF_CHAOTIC_GOOD();
  auto args = ChannelArgs()
                  .Set(GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE, 0)
                  .Set(GRPC_ARG_ENABLE_CHANNELZ, true);
  InitServer(args);
  InitClient(args);

  channelz::ChannelNode* channelz_channel =
      grpc_channel_get_channelz_node(client());
  ASSERT_NE(channelz_channel, nullptr);

  channelz::ServerNode* channelz_server =
      Server::FromC(server())->channelz_node();
  ASSERT_NE(channelz_server, nullptr);

  std::string json = channelz_channel->RenderJsonString();
  // nothing is present yet
  EXPECT_THAT(json, Not(HasSubstr("\"callsStarted\"")));
  EXPECT_THAT(json, Not(HasSubstr("\"callsFailed\"")));
  EXPECT_THAT(json, Not(HasSubstr("\"callsSucceeded\"")));

  // one successful request
  RunOneRequest(*this, true);

  json = channelz_channel->RenderJsonString();
  EXPECT_THAT(json, HasSubstr("\"callsStarted\":\"1\""));
  EXPECT_THAT(json, HasSubstr("\"callsSucceeded\":\"1\""));

  // one failed request
  RunOneRequest(*this, false);

  json = channelz_channel->RenderJsonString();
  EXPECT_THAT(json, HasSubstr("\"callsStarted\":\"2\""));
  EXPECT_THAT(json, HasSubstr("\"callsFailed\":\"1\""));
  EXPECT_THAT(json, HasSubstr("\"callsSucceeded\":\"1\""));
  // channel tracing is not enabled, so these should not be preset.
  EXPECT_THAT(json, Not(HasSubstr("\"trace\"")));
  EXPECT_THAT(json, Not(HasSubstr("\"description\":\"Channel created\"")));
  EXPECT_THAT(json, Not(HasSubstr("\"severity\":\"CT_INFO\"")));

  json = channelz_server->RenderJsonString();
  EXPECT_THAT(json, HasSubstr("\"callsStarted\":\"2\""));
  EXPECT_THAT(json, HasSubstr("\"callsFailed\":\"1\""));
  EXPECT_THAT(json, HasSubstr("\"callsSucceeded\":\"1\""));
  // channel tracing is not enabled, so these should not be preset.
  EXPECT_THAT(json, Not(HasSubstr("\"trace\"")));
  EXPECT_THAT(json, Not(HasSubstr("\"description\":\"Channel created\"")));
  EXPECT_THAT(json, Not(HasSubstr("\"severity\":\"CT_INFO\"")));

  json = channelz_server->RenderServerSockets(0, 100);
  EXPECT_THAT(json, HasSubstr("\"end\":true"));
}

CORE_END2END_TEST(CoreEnd2endTest, ChannelzWithChannelTrace) {
  SKIP_IF_CHAOTIC_GOOD();
  auto args =
      ChannelArgs()
          .Set(GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE, 1024 * 1024)
          .Set(GRPC_ARG_ENABLE_CHANNELZ, true);
  InitServer(args);
  InitClient(args);

  channelz::ChannelNode* channelz_channel =
      grpc_channel_get_channelz_node(client());
  ASSERT_NE(channelz_channel, nullptr);

  channelz::ServerNode* channelz_server =
      Server::FromC(server())->channelz_node();
  ASSERT_NE(channelz_server, nullptr);

  RunOneRequest(*this, true);

  std::string json = channelz_channel->RenderJsonString();
  EXPECT_THAT(json, HasSubstr("\"trace\""));
  EXPECT_THAT(json, HasSubstr("\"description\":\"Channel created\""));
  EXPECT_THAT(json, HasSubstr("\"severity\":\"CT_INFO\""));

  json = channelz_server->RenderJsonString();
  EXPECT_THAT(json, HasSubstr("\"trace\""));
  EXPECT_THAT(json, HasSubstr("\"description\":\"Server created\""));
  EXPECT_THAT(json, HasSubstr("\"severity\":\"CT_INFO\""));
}

CORE_END2END_TEST(CoreEnd2endTest, ChannelzDisabled) {
  SKIP_IF_CHAOTIC_GOOD();
  auto args = ChannelArgs()
                  .Set(GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE, 0)
                  .Set(GRPC_ARG_ENABLE_CHANNELZ, false);
  InitServer(args);
  InitClient(args);
  channelz::ChannelNode* channelz_channel =
      grpc_channel_get_channelz_node(client());
  EXPECT_EQ(channelz_channel, nullptr);
  RunOneRequest(*this, true);
}

}  // namespace
}  // namespace grpc_core
