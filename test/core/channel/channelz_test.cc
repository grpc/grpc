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
    grpc_arg client_a;
    client_a.type = GRPC_ARG_INTEGER;
    client_a.key =
        const_cast<char*>(GRPC_ARG_MAX_CHANNEL_TRACE_EVENTS_PER_NODE);
    client_a.value.integer = max_trace_nodes;
    grpc_channel_args client_args = {1, &client_a};
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
  EXPECT_NE(gotten_json, nullptr);
  int64_t gotten_number = (int64_t)strtol(gotten_json->value, nullptr, 0);
  EXPECT_EQ(gotten_number, expect);
}

void ValidateChannel(Channel* channel, validate_channel_data_args args) {
  char* json_str = channel->RenderJSON();
  grpc::testing::ValidateChannelProtoJsonTranslation(json_str);
  grpc_json* json = grpc_json_parse_string(json_str);
  EXPECT_NE(json, nullptr);
  grpc_json* data = GetJsonChild(json, "data");
  ValidateChildInteger(data, args.calls_started, "callsStarted");
  ValidateChildInteger(data, args.calls_failed, "callsFailed");
  ValidateChildInteger(data, args.calls_succeeded, "callsSucceeded");
  grpc_json_destroy(json);
  gpr_free(json_str);
}

char* GetLastCallStartedTimestamp(Channel* channel) {
  char* json_str = channel->RenderJSON();
  grpc_json* json = grpc_json_parse_string(json_str);
  grpc_json* data = GetJsonChild(json, "data");
  grpc_json* timestamp = GetJsonChild(data, "lastCallStartedTimestamp");
  char* ts_str = grpc_json_dump_to_string(timestamp, 0);
  grpc_json_destroy(json);
  gpr_free(json_str);
  return ts_str;
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
  intptr_t uuid = grpc_channel_get_uuid(channel.channel());
  Channel* channelz_channel = ChannelzRegistry::Get<Channel>(uuid);
  ValidateChannel(channelz_channel, {-1, -1, -1});
}

TEST_P(ChannelzChannelTest, BasicChannelAPIFunctionality) {
  grpc_core::ExecCtx exec_ctx;
  ChannelFixture channel(GetParam());
  intptr_t uuid = grpc_channel_get_uuid(channel.channel());
  Channel* channelz_channel = ChannelzRegistry::Get<Channel>(uuid);
  channelz_channel->CallStarted();
  channelz_channel->CallFailed();
  channelz_channel->CallSucceeded();
  ValidateChannel(channelz_channel, {1, 1, 1});
  channelz_channel->CallStarted();
  channelz_channel->CallFailed();
  channelz_channel->CallSucceeded();
  channelz_channel->CallStarted();
  channelz_channel->CallFailed();
  channelz_channel->CallSucceeded();
  ValidateChannel(channelz_channel, {3, 3, 3});
}

TEST_P(ChannelzChannelTest, LastCallStartedTimestamp) {
  grpc_core::ExecCtx exec_ctx;
  ChannelFixture channel(GetParam());
  intptr_t uuid = grpc_channel_get_uuid(channel.channel());
  Channel* channelz_channel = ChannelzRegistry::Get<Channel>(uuid);

  // start a call to set the last call started timestamp
  channelz_channel->CallStarted();
  char* ts1 = GetLastCallStartedTimestamp(channelz_channel);

  // time gone by should not affect the timestamp
  ChannelzSleep(100);
  char* ts2 = GetLastCallStartedTimestamp(channelz_channel);
  EXPECT_STREQ(ts1, ts2);

  // calls succeeded or failed should not affect the timestamp
  ChannelzSleep(100);
  channelz_channel->CallFailed();
  channelz_channel->CallSucceeded();
  char* ts3 = GetLastCallStartedTimestamp(channelz_channel);
  EXPECT_STREQ(ts1, ts3);

  // another call started should affect the timestamp
  // sleep for extra long to avoid flakes (since we cache Now())
  ChannelzSleep(5000);
  grpc_core::ExecCtx::Get()->InvalidateNow();
  channelz_channel->CallStarted();
  char* ts4 = GetLastCallStartedTimestamp(channelz_channel);
  EXPECT_STRNE(ts1, ts4);

  // clean up
  gpr_free(ts1);
  gpr_free(ts2);
  gpr_free(ts3);
  gpr_free(ts4);
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
