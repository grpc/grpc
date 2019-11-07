/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/channel/channelz_registry.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/server.h"

#include "test/core/util/test_config.h"
#include "test/cpp/util/channel_trace_proto_helper.h"

#include <grpc/support/string_util.h>
#include <stdlib.h>
#include <string.h>

namespace grpc_core {
namespace channelz {
namespace testing {

// testing peer to access channel internals
class CallCountingHelperPeer {
 public:
  explicit CallCountingHelperPeer(CallCountingHelper* node) : node_(node) {}
  grpc_millis last_call_started_millis() const {
    CallCountingHelper::CounterData data;
    node_->CollectData(&data);
    gpr_timespec ts = gpr_cycle_counter_to_time(data.last_call_started_cycle);
    return grpc_timespec_to_millis_round_up(ts);
  }

 private:
  CallCountingHelper* node_;
};

namespace {

grpc_json* GetJsonChild(grpc_json* parent, const char* key) {
  EXPECT_NE(parent, nullptr);
  for (grpc_json* child = parent->child; child != nullptr;
       child = child->next) {
    if (child->key != nullptr && strcmp(child->key, key) == 0) return child;
  }
  return nullptr;
}

void ValidateJsonArraySize(grpc_json* json, const char* key,
                           size_t expected_size) {
  grpc_json* arr = GetJsonChild(json, key);
  if (expected_size == 0) {
    ASSERT_EQ(arr, nullptr);
    return;
  }
  ASSERT_NE(arr, nullptr);
  ASSERT_EQ(arr->type, GRPC_JSON_ARRAY);
  size_t count = 0;
  for (grpc_json* child = arr->child; child != nullptr; child = child->next) {
    ++count;
  }
  EXPECT_EQ(count, expected_size);
}

std::vector<intptr_t> GetUuidListFromArray(grpc_json* arr) {
  EXPECT_EQ(arr->type, GRPC_JSON_ARRAY);
  std::vector<intptr_t> uuids;
  for (grpc_json* child = arr->child; child != nullptr; child = child->next) {
    grpc_json* it = GetJsonChild(child, "ref");
    EXPECT_NE(it, nullptr);
    it = GetJsonChild(it, "channelId");
    EXPECT_NE(it, nullptr);
    uuids.push_back(atoi(it->value));
  }
  return uuids;
}

void ValidateGetTopChannels(size_t expected_channels) {
  char* json_str = ChannelzRegistry::GetTopChannels(0);
  grpc::testing::ValidateGetTopChannelsResponseProtoJsonTranslation(json_str);
  grpc_json* parsed_json = grpc_json_parse_string(json_str);
  // This check will naturally have to change when we support pagination.
  // tracked: https://github.com/grpc/grpc/issues/16019.
  ValidateJsonArraySize(parsed_json, "channel", expected_channels);
  grpc_json* end = GetJsonChild(parsed_json, "end");
  ASSERT_NE(end, nullptr);
  EXPECT_EQ(end->type, GRPC_JSON_TRUE);
  grpc_json_destroy(parsed_json);
  gpr_free(json_str);
  // also check that the core API formats this correctly
  char* core_api_json_str = grpc_channelz_get_top_channels(0);
  grpc::testing::ValidateGetTopChannelsResponseProtoJsonTranslation(
      core_api_json_str);
  gpr_free(core_api_json_str);
}

void ValidateGetServers(size_t expected_servers) {
  char* json_str = ChannelzRegistry::GetServers(0);
  grpc::testing::ValidateGetServersResponseProtoJsonTranslation(json_str);
  grpc_json* parsed_json = grpc_json_parse_string(json_str);
  // This check will naturally have to change when we support pagination.
  // tracked: https://github.com/grpc/grpc/issues/16019.
  ValidateJsonArraySize(parsed_json, "server", expected_servers);
  grpc_json* end = GetJsonChild(parsed_json, "end");
  ASSERT_NE(end, nullptr);
  EXPECT_EQ(end->type, GRPC_JSON_TRUE);
  grpc_json_destroy(parsed_json);
  gpr_free(json_str);
  // also check that the core API formats this correctly
  char* core_api_json_str = grpc_channelz_get_servers(0);
  grpc::testing::ValidateGetServersResponseProtoJsonTranslation(
      core_api_json_str);
  gpr_free(core_api_json_str);
}

class ChannelFixture {
 public:
  ChannelFixture(int max_tracer_event_memory = 0) {
    grpc_arg client_a[] = {
        grpc_channel_arg_integer_create(
            const_cast<char*>(GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE),
            max_tracer_event_memory),
        grpc_channel_arg_integer_create(
            const_cast<char*>(GRPC_ARG_ENABLE_CHANNELZ), true)};
    grpc_channel_args client_args = {GPR_ARRAY_SIZE(client_a), client_a};
    channel_ =
        grpc_insecure_channel_create("fake_target", &client_args, nullptr);
  }

  ~ChannelFixture() { grpc_channel_destroy(channel_); }

  grpc_channel* channel() { return channel_; }

 private:
  grpc_channel* channel_;
};

class ServerFixture {
 public:
  explicit ServerFixture(int max_tracer_event_memory = 0) {
    grpc_arg server_a[] = {
        grpc_channel_arg_integer_create(
            const_cast<char*>(GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE),
            max_tracer_event_memory),
        grpc_channel_arg_integer_create(
            const_cast<char*>(GRPC_ARG_ENABLE_CHANNELZ), true),
    };
    grpc_channel_args server_args = {GPR_ARRAY_SIZE(server_a), server_a};
    server_ = grpc_server_create(&server_args, nullptr);
  }

  ~ServerFixture() { grpc_server_destroy(server_); }

  grpc_server* server() const { return server_; }

 private:
  grpc_server* server_;
};

struct validate_channel_data_args {
  int64_t calls_started;
  int64_t calls_failed;
  int64_t calls_succeeded;
};

void ValidateChildInteger(grpc_json* json, int64_t expect, const char* key) {
  grpc_json* gotten_json = GetJsonChild(json, key);
  if (expect == 0) {
    ASSERT_EQ(gotten_json, nullptr);
    return;
  }
  ASSERT_NE(gotten_json, nullptr);
  int64_t gotten_number = (int64_t)strtol(gotten_json->value, nullptr, 0);
  EXPECT_EQ(gotten_number, expect);
}

void ValidateCounters(char* json_str, validate_channel_data_args args) {
  grpc_json* json = grpc_json_parse_string(json_str);
  ASSERT_NE(json, nullptr);
  grpc_json* data = GetJsonChild(json, "data");
  ValidateChildInteger(data, args.calls_started, "callsStarted");
  ValidateChildInteger(data, args.calls_failed, "callsFailed");
  ValidateChildInteger(data, args.calls_succeeded, "callsSucceeded");
  grpc_json_destroy(json);
}

void ValidateChannel(ChannelNode* channel, validate_channel_data_args args) {
  char* json_str = channel->RenderJsonString();
  grpc::testing::ValidateChannelProtoJsonTranslation(json_str);
  ValidateCounters(json_str, args);
  gpr_free(json_str);
  // also check that the core API formats this the correct way
  char* core_api_json_str = grpc_channelz_get_channel(channel->uuid());
  grpc::testing::ValidateGetChannelResponseProtoJsonTranslation(
      core_api_json_str);
  gpr_free(core_api_json_str);
}

void ValidateServer(ServerNode* server, validate_channel_data_args args) {
  char* json_str = server->RenderJsonString();
  grpc::testing::ValidateServerProtoJsonTranslation(json_str);
  ValidateCounters(json_str, args);
  gpr_free(json_str);
  // also check that the core API formats this the correct way
  char* core_api_json_str = grpc_channelz_get_server(server->uuid());
  grpc::testing::ValidateGetServerResponseProtoJsonTranslation(
      core_api_json_str);
  gpr_free(core_api_json_str);
}

grpc_millis GetLastCallStartedMillis(CallCountingHelper* channel) {
  CallCountingHelperPeer peer(channel);
  return peer.last_call_started_millis();
}

void ChannelzSleep(int64_t sleep_us) {
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                               gpr_time_from_micros(sleep_us, GPR_TIMESPAN)));
  grpc_core::ExecCtx::Get()->InvalidateNow();
}

}  // anonymous namespace

class ChannelzChannelTest : public ::testing::TestWithParam<size_t> {};

TEST_P(ChannelzChannelTest, BasicChannel) {
  grpc_core::ExecCtx exec_ctx;
  ChannelFixture channel(GetParam());
  ChannelNode* channelz_channel =
      grpc_channel_get_channelz_node(channel.channel());
  ValidateChannel(channelz_channel, {0, 0, 0});
}

TEST(ChannelzChannelTest, ChannelzDisabled) {
  grpc_core::ExecCtx exec_ctx;
  // explicitly disable channelz
  grpc_arg arg[] = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE),
          0),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_ENABLE_CHANNELZ), false)};
  grpc_channel_args args = {GPR_ARRAY_SIZE(arg), arg};
  grpc_channel* channel =
      grpc_insecure_channel_create("fake_target", &args, nullptr);
  ChannelNode* channelz_channel = grpc_channel_get_channelz_node(channel);
  ASSERT_EQ(channelz_channel, nullptr);
  grpc_channel_destroy(channel);
}

TEST_P(ChannelzChannelTest, BasicChannelAPIFunctionality) {
  grpc_core::ExecCtx exec_ctx;
  ChannelFixture channel(GetParam());
  ChannelNode* channelz_channel =
      grpc_channel_get_channelz_node(channel.channel());
  channelz_channel->RecordCallStarted();
  channelz_channel->RecordCallFailed();
  channelz_channel->RecordCallSucceeded();
  ValidateChannel(channelz_channel, {1, 1, 1});
  channelz_channel->RecordCallStarted();
  channelz_channel->RecordCallFailed();
  channelz_channel->RecordCallSucceeded();
  channelz_channel->RecordCallStarted();
  channelz_channel->RecordCallFailed();
  channelz_channel->RecordCallSucceeded();
  ValidateChannel(channelz_channel, {3, 3, 3});
}

TEST_P(ChannelzChannelTest, LastCallStartedMillis) {
  grpc_core::ExecCtx exec_ctx;
  CallCountingHelper counter;
  // start a call to set the last call started timestamp
  counter.RecordCallStarted();
  grpc_millis millis1 = GetLastCallStartedMillis(&counter);
  // time gone by should not affect the timestamp
  ChannelzSleep(100);
  grpc_millis millis2 = GetLastCallStartedMillis(&counter);
  EXPECT_EQ(millis1, millis2);
  // calls succeeded or failed should not affect the timestamp
  ChannelzSleep(100);
  counter.RecordCallFailed();
  counter.RecordCallSucceeded();
  grpc_millis millis3 = GetLastCallStartedMillis(&counter);
  EXPECT_EQ(millis1, millis3);
  // another call started should affect the timestamp
  // sleep for extra long to avoid flakes (since we cache Now())
  ChannelzSleep(5000);
  counter.RecordCallStarted();
  grpc_millis millis4 = GetLastCallStartedMillis(&counter);
  EXPECT_NE(millis1, millis4);
}

class ChannelzRegistryBasedTest : public ::testing::TestWithParam<size_t> {
 protected:
  // ensure we always have a fresh registry for tests.
  void SetUp() override {
    ChannelzRegistry::Shutdown();
    ChannelzRegistry::Init();
  }

  void TearDown() override {
    ChannelzRegistry::Shutdown();
    ChannelzRegistry::Init();
  }
};

TEST_F(ChannelzRegistryBasedTest, BasicGetTopChannelsTest) {
  grpc_core::ExecCtx exec_ctx;
  ChannelFixture channel;
  ValidateGetTopChannels(1);
}

TEST_F(ChannelzRegistryBasedTest, NoChannelsTest) {
  grpc_core::ExecCtx exec_ctx;
  ValidateGetTopChannels(0);
}

TEST_F(ChannelzRegistryBasedTest, ManyChannelsTest) {
  grpc_core::ExecCtx exec_ctx;
  ChannelFixture channels[10];
  (void)channels;  // suppress unused variable error
  ValidateGetTopChannels(10);
}

TEST_F(ChannelzRegistryBasedTest, GetTopChannelsPagination) {
  grpc_core::ExecCtx exec_ctx;
  // this is over the pagination limit.
  ChannelFixture channels[150];
  (void)channels;  // suppress unused variable error
  char* json_str = ChannelzRegistry::GetTopChannels(0);
  grpc::testing::ValidateGetTopChannelsResponseProtoJsonTranslation(json_str);
  grpc_json* parsed_json = grpc_json_parse_string(json_str);
  // 100 is the pagination limit.
  ValidateJsonArraySize(parsed_json, "channel", 100);
  grpc_json* end = GetJsonChild(parsed_json, "end");
  EXPECT_EQ(end, nullptr);
  grpc_json_destroy(parsed_json);
  gpr_free(json_str);
  // Now we get the rest
  json_str = ChannelzRegistry::GetTopChannels(101);
  grpc::testing::ValidateGetTopChannelsResponseProtoJsonTranslation(json_str);
  parsed_json = grpc_json_parse_string(json_str);
  ValidateJsonArraySize(parsed_json, "channel", 50);
  end = GetJsonChild(parsed_json, "end");
  ASSERT_NE(end, nullptr);
  EXPECT_EQ(end->type, GRPC_JSON_TRUE);
  grpc_json_destroy(parsed_json);
  gpr_free(json_str);
}

TEST_F(ChannelzRegistryBasedTest, GetTopChannelsUuidCheck) {
  const intptr_t kNumChannels = 50;
  grpc_core::ExecCtx exec_ctx;
  ChannelFixture channels[kNumChannels];
  (void)channels;  // suppress unused variable error
  char* json_str = ChannelzRegistry::GetTopChannels(0);
  grpc_json* parsed_json = grpc_json_parse_string(json_str);
  ValidateJsonArraySize(parsed_json, "channel", kNumChannels);
  grpc_json* json_channels = GetJsonChild(parsed_json, "channel");
  std::vector<intptr_t> uuids = GetUuidListFromArray(json_channels);
  for (int i = 0; i < kNumChannels; ++i) {
    EXPECT_EQ(i + 1, uuids[i]);
  }
  grpc_json_destroy(parsed_json);
  gpr_free(json_str);
}

TEST_F(ChannelzRegistryBasedTest, GetTopChannelsMiddleUuidCheck) {
  const intptr_t kNumChannels = 50;
  const intptr_t kMidQuery = 40;
  grpc_core::ExecCtx exec_ctx;
  ChannelFixture channels[kNumChannels];
  (void)channels;  // suppress unused variable error
  // only query for the end of the channels
  char* json_str = ChannelzRegistry::GetTopChannels(kMidQuery);
  grpc_json* parsed_json = grpc_json_parse_string(json_str);
  ValidateJsonArraySize(parsed_json, "channel", kNumChannels - kMidQuery + 1);
  grpc_json* json_channels = GetJsonChild(parsed_json, "channel");
  std::vector<intptr_t> uuids = GetUuidListFromArray(json_channels);
  for (size_t i = 0; i < uuids.size(); ++i) {
    EXPECT_EQ(static_cast<intptr_t>(kMidQuery + i), uuids[i]);
  }
  grpc_json_destroy(parsed_json);
  gpr_free(json_str);
}

TEST_F(ChannelzRegistryBasedTest, GetTopChannelsNoHitUuid) {
  grpc_core::ExecCtx exec_ctx;
  ChannelFixture pre_channels[40];  // will take uuid[1, 40]
  (void)pre_channels;               // suppress unused variable error
  ServerFixture servers[10];        // will take uuid[41, 50]
  (void)servers;                    // suppress unused variable error
  ChannelFixture channels[10];      // will take uuid[51, 60]
  (void)channels;                   // suppress unused variable error
  // query in the middle of the server channels
  char* json_str = ChannelzRegistry::GetTopChannels(45);
  grpc_json* parsed_json = grpc_json_parse_string(json_str);
  ValidateJsonArraySize(parsed_json, "channel", 10);
  grpc_json* json_channels = GetJsonChild(parsed_json, "channel");
  std::vector<intptr_t> uuids = GetUuidListFromArray(json_channels);
  for (size_t i = 0; i < uuids.size(); ++i) {
    EXPECT_EQ(static_cast<intptr_t>(51 + i), uuids[i]);
  }
  grpc_json_destroy(parsed_json);
  gpr_free(json_str);
}

TEST_F(ChannelzRegistryBasedTest, GetTopChannelsMoreGaps) {
  grpc_core::ExecCtx exec_ctx;
  ChannelFixture channel_with_uuid1;
  { ServerFixture channel_with_uuid2; }
  ChannelFixture channel_with_uuid3;
  { ServerFixture server_with_uuid4; }
  ChannelFixture channel_with_uuid5;
  // Current state of list: [1, NULL, 3, NULL, 5]
  char* json_str = ChannelzRegistry::GetTopChannels(2);
  grpc_json* parsed_json = grpc_json_parse_string(json_str);
  ValidateJsonArraySize(parsed_json, "channel", 2);
  grpc_json* json_channels = GetJsonChild(parsed_json, "channel");
  std::vector<intptr_t> uuids = GetUuidListFromArray(json_channels);
  EXPECT_EQ(static_cast<intptr_t>(3), uuids[0]);
  EXPECT_EQ(static_cast<intptr_t>(5), uuids[1]);
  grpc_json_destroy(parsed_json);
  gpr_free(json_str);
  json_str = ChannelzRegistry::GetTopChannels(4);
  parsed_json = grpc_json_parse_string(json_str);
  ValidateJsonArraySize(parsed_json, "channel", 1);
  json_channels = GetJsonChild(parsed_json, "channel");
  uuids = GetUuidListFromArray(json_channels);
  EXPECT_EQ(static_cast<intptr_t>(5), uuids[0]);
  grpc_json_destroy(parsed_json);
  gpr_free(json_str);
}

TEST_F(ChannelzRegistryBasedTest, GetTopChannelsUuidAfterCompaction) {
  const intptr_t kLoopIterations = 50;
  grpc_core::ExecCtx exec_ctx;
  std::vector<std::unique_ptr<ChannelFixture>> even_channels;
  {
    // these will delete and unregister themselves after this block.
    std::vector<std::unique_ptr<ChannelFixture>> odd_channels;
    for (int i = 0; i < kLoopIterations; i++) {
      odd_channels.push_back(MakeUnique<ChannelFixture>());
      even_channels.push_back(MakeUnique<ChannelFixture>());
    }
  }
  char* json_str = ChannelzRegistry::GetTopChannels(0);
  grpc_json* parsed_json = grpc_json_parse_string(json_str);
  ValidateJsonArraySize(parsed_json, "channel", kLoopIterations);
  grpc_json* json_channels = GetJsonChild(parsed_json, "channel");
  std::vector<intptr_t> uuids = GetUuidListFromArray(json_channels);
  for (int i = 0; i < kLoopIterations; ++i) {
    // only the even uuids will still be present.
    EXPECT_EQ((i + 1) * 2, uuids[i]);
  }
  grpc_json_destroy(parsed_json);
  gpr_free(json_str);
}

TEST_F(ChannelzRegistryBasedTest, InternalChannelTest) {
  grpc_core::ExecCtx exec_ctx;
  ChannelFixture channels[10];
  (void)channels;  // suppress unused variable error
  // create an internal channel
  grpc_arg client_a[2];
  client_a[0] = grpc_core::channelz::MakeParentUuidArg(1);
  client_a[1] = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_ENABLE_CHANNELZ), true);
  grpc_channel_args client_args = {GPR_ARRAY_SIZE(client_a), client_a};
  grpc_channel* internal_channel =
      grpc_insecure_channel_create("fake_target", &client_args, nullptr);
  // The internal channel should not be returned from the request
  ValidateGetTopChannels(10);
  grpc_channel_destroy(internal_channel);
}

TEST(ChannelzServerTest, BasicServerAPIFunctionality) {
  grpc_core::ExecCtx exec_ctx;
  ServerFixture server(10);
  ServerNode* channelz_server = grpc_server_get_channelz_node(server.server());
  channelz_server->RecordCallStarted();
  channelz_server->RecordCallFailed();
  channelz_server->RecordCallSucceeded();
  ValidateServer(channelz_server, {1, 1, 1});
  channelz_server->RecordCallStarted();
  channelz_server->RecordCallFailed();
  channelz_server->RecordCallSucceeded();
  channelz_server->RecordCallStarted();
  channelz_server->RecordCallFailed();
  channelz_server->RecordCallSucceeded();
  ValidateServer(channelz_server, {3, 3, 3});
}

TEST_F(ChannelzRegistryBasedTest, BasicGetServersTest) {
  grpc_core::ExecCtx exec_ctx;
  ServerFixture server;
  ValidateGetServers(1);
}

TEST_F(ChannelzRegistryBasedTest, NoServersTest) {
  grpc_core::ExecCtx exec_ctx;
  ValidateGetServers(0);
}

TEST_F(ChannelzRegistryBasedTest, ManyServersTest) {
  grpc_core::ExecCtx exec_ctx;
  ServerFixture servers[10];
  (void)servers;  // suppress unused variable error
  ValidateGetServers(10);
}

INSTANTIATE_TEST_SUITE_P(ChannelzChannelTestSweep, ChannelzChannelTest,
                         ::testing::Values(0, 8, 64, 1024, 1024 * 1024));

}  // namespace testing
}  // namespace channelz
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
