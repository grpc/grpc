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

#include "test/core/util/test_config.h"
#include "test/cpp/util/channel_trace_proto_helper.h"

#include <stdlib.h>
#include <string.h>

namespace grpc_core {
namespace channelz {
namespace testing {

// testing peer to access channel internals
class ChannelNodePeer {
 public:
  explicit ChannelNodePeer(ChannelNode* node) : node_(node) {}
  ChannelTrace* trace() const { return &node_->trace_; }

 private:
  ChannelNode* node_;
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
  ASSERT_NE(arr, nullptr);
  ASSERT_EQ(arr->type, GRPC_JSON_ARRAY);
  size_t count = 0;
  for (grpc_json* child = arr->child; child != nullptr; child = child->next) {
    ++count;
  }
  ASSERT_EQ(count, expected_size);
}

void ValidateChannelTraceData(grpc_json* json,
                              size_t num_events_logged_expected,
                              size_t actual_num_events_expected) {
  ASSERT_NE(json, nullptr);
  grpc_json* num_events_logged_json = GetJsonChild(json, "numEventsLogged");
  ASSERT_NE(num_events_logged_json, nullptr);
  grpc_json* start_time = GetJsonChild(json, "creationTimestamp");
  ASSERT_NE(start_time, nullptr);
  size_t num_events_logged =
      (size_t)strtol(num_events_logged_json->value, nullptr, 0);
  ASSERT_EQ(num_events_logged, num_events_logged_expected);
  ValidateJsonArraySize(json, "events", actual_num_events_expected);
}

void AddSimpleTrace(ChannelTrace* tracer) {
  tracer->AddTraceEvent(ChannelTrace::Severity::Info,
                        grpc_slice_from_static_string("simple trace"));
}

// checks for the existence of all the required members of the tracer.
void ValidateChannelTrace(ChannelTrace* tracer,
                          size_t expected_num_event_logged, size_t max_nodes) {
  if (!max_nodes) return;
  grpc_json* json = tracer->RenderJson();
  EXPECT_NE(json, nullptr);
  char* json_str = grpc_json_dump_to_string(json, 0);
  grpc_json_destroy(json);
  grpc::testing::ValidateChannelTraceProtoJsonTranslation(json_str);
  grpc_json* parsed_json = grpc_json_parse_string(json_str);
  ValidateChannelTraceData(parsed_json, expected_num_event_logged,
                           GPR_MIN(expected_num_event_logged, max_nodes));
  grpc_json_destroy(parsed_json);
  gpr_free(json_str);
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

}  // anonymous namespace

class ChannelTracerTest : public ::testing::TestWithParam<size_t> {};

// Tests basic ChannelTrace functionality like construction, adding trace, and
// lookups by uuid.
TEST_P(ChannelTracerTest, BasicTest) {
  grpc_core::ExecCtx exec_ctx;
  ChannelTrace tracer(GetParam());
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  tracer.AddTraceEvent(ChannelTrace::Severity::Info,
                       grpc_slice_from_static_string("trace three"));
  tracer.AddTraceEvent(ChannelTrace::Severity::Error,
                       grpc_slice_from_static_string("trace four error"));
  ValidateChannelTrace(&tracer, 4, GetParam());
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  ValidateChannelTrace(&tracer, 6, GetParam());
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  ValidateChannelTrace(&tracer, 10, GetParam());
}

// Tests more complex functionality, like a parent channel tracking
// subchannles. This exercises the ref/unref patterns since the parent tracer
// and this function will both hold refs to the subchannel.
TEST_P(ChannelTracerTest, ComplexTest) {
  grpc_core::ExecCtx exec_ctx;
  ChannelTrace tracer(GetParam());
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  ChannelFixture channel1(GetParam());
  RefCountedPtr<ChannelNode> sc1 =
      MakeRefCounted<ChannelNode>(channel1.channel(), GetParam(), true);
  ChannelNodePeer sc1_peer(sc1.get());
  tracer.AddTraceEventWithReference(
      ChannelTrace::Severity::Info,
      grpc_slice_from_static_string("subchannel one created"), sc1);
  ValidateChannelTrace(&tracer, 3, GetParam());
  AddSimpleTrace(sc1_peer.trace());
  AddSimpleTrace(sc1_peer.trace());
  AddSimpleTrace(sc1_peer.trace());
  ValidateChannelTrace(sc1_peer.trace(), 3, GetParam());
  AddSimpleTrace(sc1_peer.trace());
  AddSimpleTrace(sc1_peer.trace());
  AddSimpleTrace(sc1_peer.trace());
  ValidateChannelTrace(sc1_peer.trace(), 6, GetParam());
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  ValidateChannelTrace(&tracer, 5, GetParam());
  ChannelFixture channel2(GetParam());
  RefCountedPtr<ChannelNode> sc2 =
      MakeRefCounted<ChannelNode>(channel2.channel(), GetParam(), true);
  tracer.AddTraceEventWithReference(
      ChannelTrace::Severity::Info,
      grpc_slice_from_static_string("LB channel two created"), sc2);
  tracer.AddTraceEventWithReference(
      ChannelTrace::Severity::Warning,
      grpc_slice_from_static_string("subchannel one inactive"), sc1);
  ValidateChannelTrace(&tracer, 7, GetParam());
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  sc1.reset();
  sc2.reset();
}

// Test a case in which the parent channel has subchannels and the subchannels
// have connections. Ensures that everything lives as long as it should then
// gets deleted.
TEST_P(ChannelTracerTest, TestNesting) {
  grpc_core::ExecCtx exec_ctx;
  ChannelTrace tracer(GetParam());
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  ValidateChannelTrace(&tracer, 2, GetParam());
  ChannelFixture channel1(GetParam());
  RefCountedPtr<ChannelNode> sc1 =
      MakeRefCounted<ChannelNode>(channel1.channel(), GetParam(), true);
  ChannelNodePeer sc1_peer(sc1.get());
  tracer.AddTraceEventWithReference(
      ChannelTrace::Severity::Info,
      grpc_slice_from_static_string("subchannel one created"), sc1);
  ValidateChannelTrace(&tracer, 3, GetParam());
  AddSimpleTrace(sc1_peer.trace());
  ChannelFixture channel2(GetParam());
  RefCountedPtr<ChannelNode> conn1 =
      MakeRefCounted<ChannelNode>(channel2.channel(), GetParam(), true);
  ChannelNodePeer conn1_peer(conn1.get());
  // nesting one level deeper.
  sc1_peer.trace()->AddTraceEventWithReference(
      ChannelTrace::Severity::Info,
      grpc_slice_from_static_string("connection one created"), conn1);
  ValidateChannelTrace(&tracer, 3, GetParam());
  AddSimpleTrace(conn1_peer.trace());
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  ValidateChannelTrace(&tracer, 5, GetParam());
  ValidateChannelTrace(conn1_peer.trace(), 1, GetParam());
  ChannelFixture channel3(GetParam());
  RefCountedPtr<ChannelNode> sc2 =
      MakeRefCounted<ChannelNode>(channel3.channel(), GetParam(), true);
  tracer.AddTraceEventWithReference(
      ChannelTrace::Severity::Info,
      grpc_slice_from_static_string("subchannel two created"), sc2);
  // this trace should not get added to the parents children since it is already
  // present in the tracer.
  tracer.AddTraceEventWithReference(
      ChannelTrace::Severity::Warning,
      grpc_slice_from_static_string("subchannel one inactive"), sc1);
  AddSimpleTrace(&tracer);
  ValidateChannelTrace(&tracer, 8, GetParam());
  sc1.reset();
  sc2.reset();
  conn1.reset();
}

INSTANTIATE_TEST_CASE_P(ChannelTracerTestSweep, ChannelTracerTest,
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
