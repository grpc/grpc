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
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/channel/channelz_registry.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/surface/channel.h"

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

size_t GetSizeofTraceEvent() { return sizeof(ChannelTrace::TraceEvent); }

namespace {

void ValidateJsonArraySize(const Json& array, size_t expected) {
  if (expected == 0) {
    ASSERT_EQ(array.type(), Json::Type::JSON_NULL);
  } else {
    ASSERT_EQ(array.type(), Json::Type::ARRAY);
    EXPECT_EQ(array.array_value().size(), expected);
  }
}

void ValidateChannelTraceData(const Json& json,
                              size_t num_events_logged_expected,
                              size_t actual_num_events_expected) {
  ASSERT_EQ(json.type(), Json::Type::OBJECT);
  Json::Object object = json.object_value();
  Json& num_events_logged_json = object["numEventsLogged"];
  ASSERT_EQ(num_events_logged_json.type(), Json::Type::STRING);
  size_t num_events_logged = static_cast<size_t>(
      strtol(num_events_logged_json.string_value().c_str(), nullptr, 0));
  ASSERT_EQ(num_events_logged, num_events_logged_expected);
  Json& start_time_json = object["creationTimestamp"];
  ASSERT_EQ(start_time_json.type(), Json::Type::STRING);
  ValidateJsonArraySize(object["events"], actual_num_events_expected);
}

void AddSimpleTrace(ChannelTrace* tracer) {
  tracer->AddTraceEvent(ChannelTrace::Severity::Info,
                        grpc_slice_from_static_string("simple trace"));
}

// checks for the existence of all the required members of the tracer.
void ValidateChannelTraceCustom(ChannelTrace* tracer, size_t num_events_logged,
                                size_t num_events_expected) {
  Json json = tracer->RenderJson();
  ASSERT_EQ(json.type(), Json::Type::OBJECT);
  std::string json_str = json.Dump();
  grpc::testing::ValidateChannelTraceProtoJsonTranslation(json_str.c_str());
  ValidateChannelTraceData(json, num_events_logged, num_events_expected);
}

void ValidateChannelTrace(ChannelTrace* tracer, size_t num_events_logged) {
  ValidateChannelTraceCustom(tracer, num_events_logged, num_events_logged);
}

class ChannelFixture {
 public:
  explicit ChannelFixture(int max_tracer_event_memory) {
    grpc_arg client_a = grpc_channel_arg_integer_create(
        const_cast<char*>(GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE),
        max_tracer_event_memory);
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

const int kEventListMemoryLimit = 1024 * 1024;

// Tests basic ChannelTrace functionality like construction, adding trace, and
// lookups by uuid.
TEST(ChannelTracerTest, BasicTest) {
  grpc_core::ExecCtx exec_ctx;
  ChannelTrace tracer(kEventListMemoryLimit);
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  tracer.AddTraceEvent(ChannelTrace::Severity::Info,
                       grpc_slice_from_static_string("trace three"));
  tracer.AddTraceEvent(ChannelTrace::Severity::Error,
                       grpc_slice_from_static_string("trace four error"));
  ValidateChannelTrace(&tracer, 4);
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  ValidateChannelTrace(&tracer, 6);
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  ValidateChannelTrace(&tracer, 10);
}

// Tests more complex functionality, like a parent channel tracking
// subchannles. This exercises the ref/unref patterns since the parent tracer
// and this function will both hold refs to the subchannel.
TEST(ChannelTracerTest, ComplexTest) {
  grpc_core::ExecCtx exec_ctx;
  ChannelTrace tracer(kEventListMemoryLimit);
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  ChannelFixture channel1(kEventListMemoryLimit);
  RefCountedPtr<ChannelNode> sc1 =
      MakeRefCounted<ChannelNode>("fake_target", kEventListMemoryLimit, 0);
  ChannelNodePeer sc1_peer(sc1.get());
  tracer.AddTraceEventWithReference(
      ChannelTrace::Severity::Info,
      grpc_slice_from_static_string("subchannel one created"), sc1);
  ValidateChannelTrace(&tracer, 3);
  AddSimpleTrace(sc1_peer.trace());
  AddSimpleTrace(sc1_peer.trace());
  AddSimpleTrace(sc1_peer.trace());
  ValidateChannelTrace(sc1_peer.trace(), 3);
  AddSimpleTrace(sc1_peer.trace());
  AddSimpleTrace(sc1_peer.trace());
  AddSimpleTrace(sc1_peer.trace());
  ValidateChannelTrace(sc1_peer.trace(), 6);
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  ValidateChannelTrace(&tracer, 5);
  ChannelFixture channel2(kEventListMemoryLimit);
  RefCountedPtr<ChannelNode> sc2 =
      MakeRefCounted<ChannelNode>("fake_target", kEventListMemoryLimit, 0);
  tracer.AddTraceEventWithReference(
      ChannelTrace::Severity::Info,
      grpc_slice_from_static_string("LB channel two created"), sc2);
  tracer.AddTraceEventWithReference(
      ChannelTrace::Severity::Warning,
      grpc_slice_from_static_string("subchannel one inactive"), sc1);
  ValidateChannelTrace(&tracer, 7);
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
TEST(ChannelTracerTest, TestNesting) {
  grpc_core::ExecCtx exec_ctx;
  ChannelTrace tracer(kEventListMemoryLimit);
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  ValidateChannelTrace(&tracer, 2);
  ChannelFixture channel1(kEventListMemoryLimit);
  RefCountedPtr<ChannelNode> sc1 =
      MakeRefCounted<ChannelNode>("fake_target", kEventListMemoryLimit, 0);
  ChannelNodePeer sc1_peer(sc1.get());
  tracer.AddTraceEventWithReference(
      ChannelTrace::Severity::Info,
      grpc_slice_from_static_string("subchannel one created"), sc1);
  ValidateChannelTrace(&tracer, 3);
  AddSimpleTrace(sc1_peer.trace());
  ChannelFixture channel2(kEventListMemoryLimit);
  RefCountedPtr<ChannelNode> conn1 =
      MakeRefCounted<ChannelNode>("fake_target", kEventListMemoryLimit, 0);
  ChannelNodePeer conn1_peer(conn1.get());
  // nesting one level deeper.
  sc1_peer.trace()->AddTraceEventWithReference(
      ChannelTrace::Severity::Info,
      grpc_slice_from_static_string("connection one created"), conn1);
  ValidateChannelTrace(&tracer, 3);
  AddSimpleTrace(conn1_peer.trace());
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  ValidateChannelTrace(&tracer, 5);
  ValidateChannelTrace(conn1_peer.trace(), 1);
  ChannelFixture channel3(kEventListMemoryLimit);
  RefCountedPtr<ChannelNode> sc2 =
      MakeRefCounted<ChannelNode>("fake_target", kEventListMemoryLimit, 0);
  tracer.AddTraceEventWithReference(
      ChannelTrace::Severity::Info,
      grpc_slice_from_static_string("subchannel two created"), sc2);
  // this trace should not get added to the parents children since it is already
  // present in the tracer.
  tracer.AddTraceEventWithReference(
      ChannelTrace::Severity::Warning,
      grpc_slice_from_static_string("subchannel one inactive"), sc1);
  AddSimpleTrace(&tracer);
  ValidateChannelTrace(&tracer, 8);
  sc1.reset();
  sc2.reset();
  conn1.reset();
}

TEST(ChannelTracerTest, TestSmallMemoryLimit) {
  grpc_core::ExecCtx exec_ctx;
  // doesn't make sense, but serves a testing purpose for the channel tracing
  // bookkeeping. All tracing events added should will get immediately garbage
  // collected.
  const int kSmallMemoryLimit = 1;
  ChannelTrace tracer(kSmallMemoryLimit);
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  tracer.AddTraceEvent(ChannelTrace::Severity::Info,
                       grpc_slice_from_static_string("trace three"));
  tracer.AddTraceEvent(ChannelTrace::Severity::Error,
                       grpc_slice_from_static_string("trace four error"));
  ValidateChannelTraceCustom(&tracer, 4, 0);
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  ValidateChannelTraceCustom(&tracer, 6, 0);
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  AddSimpleTrace(&tracer);
  ValidateChannelTraceCustom(&tracer, 10, 0);
}

TEST(ChannelTracerTest, TestEviction) {
  grpc_core::ExecCtx exec_ctx;
  const int kTraceEventSize = GetSizeofTraceEvent();
  const int kNumEvents = 5;
  ChannelTrace tracer(kTraceEventSize * kNumEvents);
  for (int i = 1; i <= kNumEvents; ++i) {
    AddSimpleTrace(&tracer);
    ValidateChannelTrace(&tracer, i);
  }
  // at this point the list is full, and each subsequent enntry will cause an
  // eviction.
  for (int i = 1; i <= kNumEvents; ++i) {
    AddSimpleTrace(&tracer);
    ValidateChannelTraceCustom(&tracer, kNumEvents + i, kNumEvents);
  }
}

TEST(ChannelTracerTest, TestMultipleEviction) {
  grpc_core::ExecCtx exec_ctx;
  const int kTraceEventSize = GetSizeofTraceEvent();
  const int kNumEvents = 5;
  ChannelTrace tracer(kTraceEventSize * kNumEvents);
  for (int i = 1; i <= kNumEvents; ++i) {
    AddSimpleTrace(&tracer);
    ValidateChannelTrace(&tracer, i);
  }
  // at this point the list is full, and each subsequent enntry will cause an
  // eviction. We will now add in a trace event that has a copied string. This
  // uses more memory, so it will cause a double eviciction
  tracer.AddTraceEvent(
      ChannelTrace::Severity::Info,
      grpc_slice_from_copied_string(
          "long enough string to trigger a multiple eviction"));
  ValidateChannelTraceCustom(&tracer, kNumEvents + 1, kNumEvents - 1);
}

TEST(ChannelTracerTest, TestTotalEviction) {
  grpc_core::ExecCtx exec_ctx;
  const int kTraceEventSize = GetSizeofTraceEvent();
  const int kNumEvents = 5;
  ChannelTrace tracer(kTraceEventSize * kNumEvents);
  for (int i = 1; i <= kNumEvents; ++i) {
    AddSimpleTrace(&tracer);
    ValidateChannelTrace(&tracer, i);
  }
  // at this point the list is full. Now we add such a big slice that
  // everything gets evicted.
  grpc_slice huge_slice = grpc_slice_malloc(kTraceEventSize * (kNumEvents + 1));
  tracer.AddTraceEvent(ChannelTrace::Severity::Info, huge_slice);
  ValidateChannelTraceCustom(&tracer, kNumEvents + 1, 0);
}

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
