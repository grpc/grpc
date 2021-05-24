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

  gpr_timespec last_call_started_time() const {
    CallCountingHelper::CounterData data;
    node_->CollectData(&data);
    return gpr_cycle_counter_to_time(data.last_call_started_cycle);
  }

 private:
  CallCountingHelper* node_;
};

namespace {

std::vector<intptr_t> GetUuidListFromArray(const Json::Array& arr) {
  std::vector<intptr_t> uuids;
  for (const Json& value : arr) {
    EXPECT_EQ(value.type(), Json::Type::OBJECT);
    if (value.type() != Json::Type::OBJECT) continue;
    const Json::Object& object = value.object_value();
    auto it = object.find("ref");
    EXPECT_NE(it, object.end());
    if (it == object.end()) continue;
    EXPECT_EQ(it->second.type(), Json::Type::OBJECT);
    if (it->second.type() != Json::Type::OBJECT) continue;
    const Json::Object& ref_object = it->second.object_value();
    it = ref_object.find("channelId");
    EXPECT_NE(it, ref_object.end());
    if (it != ref_object.end()) {
      uuids.push_back(atoi(it->second.string_value().c_str()));
    }
  }
  return uuids;
}

void ValidateJsonArraySize(const Json& array, size_t expected) {
  if (expected == 0) {
    ASSERT_EQ(array.type(), Json::Type::JSON_NULL);
  } else {
    ASSERT_EQ(array.type(), Json::Type::ARRAY);
    EXPECT_EQ(array.array_value().size(), expected);
  }
}

void ValidateJsonEnd(const Json& json, bool end) {
  auto it = json.object_value().find("end");
  if (end) {
    ASSERT_NE(it, json.object_value().end());
    EXPECT_EQ(it->second.type(), Json::Type::JSON_TRUE);
  } else {
    ASSERT_EQ(it, json.object_value().end());
  }
}

void ValidateGetTopChannels(size_t expected_channels) {
  std::string json_str = ChannelzRegistry::GetTopChannels(0);
  grpc::testing::ValidateGetTopChannelsResponseProtoJsonTranslation(
      json_str.c_str());
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json parsed_json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  ASSERT_EQ(parsed_json.type(), Json::Type::OBJECT);
  // This check will naturally have to change when we support pagination.
  // tracked: https://github.com/grpc/grpc/issues/16019.
  ValidateJsonArraySize((*parsed_json.mutable_object())["channel"],
                        expected_channels);
  ValidateJsonEnd(parsed_json, true);
  // Also check that the core API formats this correctly.
  char* core_api_json_str = grpc_channelz_get_top_channels(0);
  grpc::testing::ValidateGetTopChannelsResponseProtoJsonTranslation(
      core_api_json_str);
  gpr_free(core_api_json_str);
}

void ValidateGetServers(size_t expected_servers) {
  std::string json_str = ChannelzRegistry::GetServers(0);
  grpc::testing::ValidateGetServersResponseProtoJsonTranslation(
      json_str.c_str());
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json parsed_json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  ASSERT_EQ(parsed_json.type(), Json::Type::OBJECT);
  // This check will naturally have to change when we support pagination.
  // tracked: https://github.com/grpc/grpc/issues/16019.
  ValidateJsonArraySize((*parsed_json.mutable_object())["server"],
                        expected_servers);
  ValidateJsonEnd(parsed_json, true);
  // Also check that the core API formats this correctly.
  char* core_api_json_str = grpc_channelz_get_servers(0);
  grpc::testing::ValidateGetServersResponseProtoJsonTranslation(
      core_api_json_str);
  gpr_free(core_api_json_str);
}

class ChannelFixture {
 public:
  explicit ChannelFixture(int max_tracer_event_memory = 0) {
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

struct ValidateChannelDataArgs {
  int64_t calls_started;
  int64_t calls_failed;
  int64_t calls_succeeded;
};

void ValidateChildInteger(const Json::Object& object, const std::string& key,
                          int64_t expected) {
  auto it = object.find(key);
  if (expected == 0) {
    ASSERT_EQ(it, object.end());
    return;
  }
  ASSERT_NE(it, object.end());
  ASSERT_EQ(it->second.type(), Json::Type::STRING);
  int64_t gotten_number = static_cast<int64_t>(
      strtol(it->second.string_value().c_str(), nullptr, 0));
  EXPECT_EQ(gotten_number, expected);
}

void ValidateCounters(const std::string& json_str,
                      const ValidateChannelDataArgs& args) {
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  ASSERT_EQ(json.type(), Json::Type::OBJECT);
  Json::Object* object = json.mutable_object();
  Json& data = (*object)["data"];
  ASSERT_EQ(data.type(), Json::Type::OBJECT);
  ValidateChildInteger(data.object_value(), "callsStarted", args.calls_started);
  ValidateChildInteger(data.object_value(), "callsFailed", args.calls_failed);
  ValidateChildInteger(data.object_value(), "callsSucceeded",
                       args.calls_succeeded);
}

void ValidateChannel(ChannelNode* channel,
                     const ValidateChannelDataArgs& args) {
  std::string json_str = channel->RenderJsonString();
  grpc::testing::ValidateChannelProtoJsonTranslation(json_str.c_str());
  ValidateCounters(json_str, args);
  // also check that the core API formats this the correct way
  char* core_api_json_str = grpc_channelz_get_channel(channel->uuid());
  grpc::testing::ValidateGetChannelResponseProtoJsonTranslation(
      core_api_json_str);
  gpr_free(core_api_json_str);
}

void ValidateServer(ServerNode* server, const ValidateChannelDataArgs& args) {
  std::string json_str = server->RenderJsonString();
  grpc::testing::ValidateServerProtoJsonTranslation(json_str.c_str());
  ValidateCounters(json_str, args);
  // also check that the core API formats this the correct way
  char* core_api_json_str = grpc_channelz_get_server(server->uuid());
  grpc::testing::ValidateGetServerResponseProtoJsonTranslation(
      core_api_json_str);
  gpr_free(core_api_json_str);
}

gpr_timespec GetLastCallStartedTime(CallCountingHelper* channel) {
  CallCountingHelperPeer peer(channel);
  return peer.last_call_started_time();
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

TEST_P(ChannelzChannelTest, LastCallStartedTime) {
  grpc_core::ExecCtx exec_ctx;
  CallCountingHelper counter;
  // start a call to set the last call started timestamp
  counter.RecordCallStarted();
  gpr_timespec time1 = GetLastCallStartedTime(&counter);
  // time gone by should not affect the timestamp
  ChannelzSleep(100);
  gpr_timespec time2 = GetLastCallStartedTime(&counter);
  EXPECT_EQ(gpr_time_cmp(time1, time2), 0);
  // calls succeeded or failed should not affect the timestamp
  ChannelzSleep(100);
  counter.RecordCallFailed();
  counter.RecordCallSucceeded();
  gpr_timespec time3 = GetLastCallStartedTime(&counter);
  EXPECT_EQ(gpr_time_cmp(time1, time3), 0);
  // another call started should affect the timestamp
  // sleep for extra long to avoid flakes (since we cache Now())
  ChannelzSleep(5000);
  counter.RecordCallStarted();
  gpr_timespec time4 = GetLastCallStartedTime(&counter);
  EXPECT_NE(gpr_time_cmp(time1, time4), 0);
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
  // This is over the pagination limit.
  ChannelFixture channels[150];
  (void)channels;  // suppress unused variable error
  std::string json_str = ChannelzRegistry::GetTopChannels(0);
  grpc::testing::ValidateGetTopChannelsResponseProtoJsonTranslation(
      json_str.c_str());
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json parsed_json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  ASSERT_EQ(parsed_json.type(), Json::Type::OBJECT);
  // 100 is the pagination limit.
  ValidateJsonArraySize((*parsed_json.mutable_object())["channel"], 100);
  ValidateJsonEnd(parsed_json, false);
  // Now we get the rest.
  json_str = ChannelzRegistry::GetTopChannels(101);
  grpc::testing::ValidateGetTopChannelsResponseProtoJsonTranslation(
      json_str.c_str());
  error = GRPC_ERROR_NONE;
  parsed_json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  ASSERT_EQ(parsed_json.type(), Json::Type::OBJECT);
  ValidateJsonArraySize((*parsed_json.mutable_object())["channel"], 50);
  ValidateJsonEnd(parsed_json, true);
}

TEST_F(ChannelzRegistryBasedTest, GetTopChannelsUuidCheck) {
  const intptr_t kNumChannels = 50;
  grpc_core::ExecCtx exec_ctx;
  ChannelFixture channels[kNumChannels];
  (void)channels;  // suppress unused variable error
  std::string json_str = ChannelzRegistry::GetTopChannels(0);
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json parsed_json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  ASSERT_EQ(parsed_json.type(), Json::Type::OBJECT);
  Json& array = (*parsed_json.mutable_object())["channel"];
  ValidateJsonArraySize(array, kNumChannels);
  std::vector<intptr_t> uuids = GetUuidListFromArray(array.array_value());
  for (int i = 0; i < kNumChannels; ++i) {
    EXPECT_EQ(i + 1, uuids[i]);
  }
}

TEST_F(ChannelzRegistryBasedTest, GetTopChannelsMiddleUuidCheck) {
  const intptr_t kNumChannels = 50;
  const intptr_t kMidQuery = 40;
  grpc_core::ExecCtx exec_ctx;
  ChannelFixture channels[kNumChannels];
  (void)channels;  // suppress unused variable error
  // Only query for the end of the channels.
  std::string json_str = ChannelzRegistry::GetTopChannels(kMidQuery);
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json parsed_json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  ASSERT_EQ(parsed_json.type(), Json::Type::OBJECT);
  Json& array = (*parsed_json.mutable_object())["channel"];
  ValidateJsonArraySize(array, kNumChannels - kMidQuery + 1);
  std::vector<intptr_t> uuids = GetUuidListFromArray(array.array_value());
  for (size_t i = 0; i < uuids.size(); ++i) {
    EXPECT_EQ(static_cast<intptr_t>(kMidQuery + i), uuids[i]);
  }
}

TEST_F(ChannelzRegistryBasedTest, GetTopChannelsNoHitUuid) {
  grpc_core::ExecCtx exec_ctx;
  ChannelFixture pre_channels[40];  // will take uuid[1, 40]
  (void)pre_channels;               // suppress unused variable error
  ServerFixture servers[10];        // will take uuid[41, 50]
  (void)servers;                    // suppress unused variable error
  ChannelFixture channels[10];      // will take uuid[51, 60]
  (void)channels;                   // suppress unused variable error
  // Query in the middle of the server channels.
  std::string json_str = ChannelzRegistry::GetTopChannels(45);
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json parsed_json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  ASSERT_EQ(parsed_json.type(), Json::Type::OBJECT);
  Json& array = (*parsed_json.mutable_object())["channel"];
  ValidateJsonArraySize(array, 10);
  std::vector<intptr_t> uuids = GetUuidListFromArray(array.array_value());
  for (size_t i = 0; i < uuids.size(); ++i) {
    EXPECT_EQ(static_cast<intptr_t>(51 + i), uuids[i]);
  }
}

TEST_F(ChannelzRegistryBasedTest, GetTopChannelsMoreGaps) {
  grpc_core::ExecCtx exec_ctx;
  ChannelFixture channel_with_uuid1;
  { ServerFixture channel_with_uuid2; }
  ChannelFixture channel_with_uuid3;
  { ServerFixture server_with_uuid4; }
  ChannelFixture channel_with_uuid5;
  // Current state of list: [1, NULL, 3, NULL, 5]
  std::string json_str = ChannelzRegistry::GetTopChannels(2);
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json parsed_json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  ASSERT_EQ(parsed_json.type(), Json::Type::OBJECT);
  Json array = (*parsed_json.mutable_object())["channel"];
  ValidateJsonArraySize(array, 2);
  std::vector<intptr_t> uuids = GetUuidListFromArray(array.array_value());
  EXPECT_EQ(static_cast<intptr_t>(3), uuids[0]);
  EXPECT_EQ(static_cast<intptr_t>(5), uuids[1]);
  json_str = ChannelzRegistry::GetTopChannels(4);
  error = GRPC_ERROR_NONE;
  parsed_json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  ASSERT_EQ(parsed_json.type(), Json::Type::OBJECT);
  array = (*parsed_json.mutable_object())["channel"];
  ValidateJsonArraySize(array, 1);
  uuids = GetUuidListFromArray(array.array_value());
  EXPECT_EQ(static_cast<intptr_t>(5), uuids[0]);
}

TEST_F(ChannelzRegistryBasedTest, GetTopChannelsUuidAfterCompaction) {
  const intptr_t kLoopIterations = 50;
  grpc_core::ExecCtx exec_ctx;
  std::vector<std::unique_ptr<ChannelFixture>> even_channels;
  {
    // these will delete and unregister themselves after this block.
    std::vector<std::unique_ptr<ChannelFixture>> odd_channels;
    for (int i = 0; i < kLoopIterations; i++) {
      odd_channels.push_back(absl::make_unique<ChannelFixture>());
      even_channels.push_back(absl::make_unique<ChannelFixture>());
    }
  }
  std::string json_str = ChannelzRegistry::GetTopChannels(0);
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json parsed_json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  ASSERT_EQ(parsed_json.type(), Json::Type::OBJECT);
  Json& array = (*parsed_json.mutable_object())["channel"];
  ValidateJsonArraySize(array, kLoopIterations);
  std::vector<intptr_t> uuids = GetUuidListFromArray(array.array_value());
  for (int i = 0; i < kLoopIterations; ++i) {
    // only the even uuids will still be present.
    EXPECT_EQ((i + 1) * 2, uuids[i]);
  }
}

TEST_F(ChannelzRegistryBasedTest, InternalChannelTest) {
  grpc_core::ExecCtx exec_ctx;
  ChannelFixture channels[10];
  (void)channels;  // suppress unused variable error
  // create an internal channel
  grpc_arg client_a[] = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_CHANNELZ_IS_INTERNAL_CHANNEL), 1),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_ENABLE_CHANNELZ), true),
  };
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
  ServerNode* channelz_server = server.server()->core_server->channelz_node();
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
