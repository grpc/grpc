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

#include "test/core/util/test_config.h"
#include "test/cpp/util/channel_trace_proto_helper.h"

#include <grpc/support/string_util.h>
#include <stdlib.h>
#include <string.h>

namespace grpc_core {
namespace channelz {
namespace testing {

// testing peer to access channel internals
class ChannelNodePeer {
 public:
  ChannelNodePeer(ChannelNode* channel) : channel_(channel) {}
  grpc_millis last_call_started_millis() {
    return (grpc_millis)gpr_atm_no_barrier_load(
        &channel_->last_call_started_millis_);
  }

 private:
  ChannelNode* channel_;
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

class ChannelFixture {
 public:
  ChannelFixture(int max_trace_nodes) {
    grpc_arg client_a[2];
    client_a[0].type = GRPC_ARG_INTEGER;
    client_a[0].key =
        const_cast<char*>(GRPC_ARG_MAX_CHANNEL_TRACE_EVENTS_PER_NODE);
    client_a[0].value.integer = max_trace_nodes;
    client_a[1].type = GRPC_ARG_INTEGER;
    client_a[1].key = const_cast<char*>(GRPC_ARG_ENABLE_CHANNELZ);
    client_a[1].value.integer = true;
    grpc_channel_args client_args = {GPR_ARRAY_SIZE(client_a), client_a};
    channel_ =
        grpc_insecure_channel_create("fake_target", &client_args, nullptr);
  }

  ~ChannelFixture() { grpc_channel_destroy(channel_); }

  grpc_channel* channel() { return channel_; }

 private:
  grpc_channel* channel_;
};

struct validate_channel_data_args {
  int64_t calls_started;
  int64_t calls_failed;
  int64_t calls_succeeded;
};

void ValidateChildInteger(grpc_json* json, int64_t expect, const char* key) {
  grpc_json* gotten_json = GetJsonChild(json, key);
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
  char* json_str = channel->RenderJSON();
  grpc::testing::ValidateChannelProtoJsonTranslation(json_str);
  ValidateCounters(json_str, args);
  gpr_free(json_str);
}

grpc_millis GetLastCallStartedMillis(ChannelNode* channel) {
  ChannelNodePeer peer(channel);
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
  char* json_str = channelz_channel->RenderJSON();
  ValidateCounters(json_str, {0, 0, 0});
  gpr_free(json_str);
}

TEST(ChannelzChannelTest, ChannelzDisabled) {
  grpc_core::ExecCtx exec_ctx;
  grpc_channel* channel =
      grpc_insecure_channel_create("fake_target", nullptr, nullptr);
  ChannelNode* channelz_channel = grpc_channel_get_channelz_node(channel);
  char* json_str = channelz_channel->RenderJSON();
  ASSERT_EQ(json_str, nullptr);
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
  ChannelFixture channel(GetParam());
  ChannelNode* channelz_channel =
      grpc_channel_get_channelz_node(channel.channel());
  // start a call to set the last call started timestamp
  channelz_channel->RecordCallStarted();
  grpc_millis millis1 = GetLastCallStartedMillis(channelz_channel);
  // time gone by should not affect the timestamp
  ChannelzSleep(100);
  grpc_millis millis2 = GetLastCallStartedMillis(channelz_channel);
  EXPECT_EQ(millis1, millis2);
  // calls succeeded or failed should not affect the timestamp
  ChannelzSleep(100);
  channelz_channel->RecordCallFailed();
  channelz_channel->RecordCallSucceeded();
  grpc_millis millis3 = GetLastCallStartedMillis(channelz_channel);
  EXPECT_EQ(millis1, millis3);
  // another call started should affect the timestamp
  // sleep for extra long to avoid flakes (since we cache Now())
  ChannelzSleep(5000);
  channelz_channel->RecordCallStarted();
  grpc_millis millis4 = GetLastCallStartedMillis(channelz_channel);
  EXPECT_NE(millis1, millis4);
}

INSTANTIATE_TEST_CASE_P(ChannelzChannelTestSweep, ChannelzChannelTest,
                        ::testing::Values(0, 1, 2, 6, 10, 15));

}  // namespace testing
}  // namespace channelz
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
