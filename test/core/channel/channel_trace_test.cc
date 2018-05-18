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
#include "src/core/lib/channel/channel_trace_registry.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/json/json.h"

#include "test/core/util/test_config.h"
#include "test/cpp/util/channel_trace_proto_helper.h"

// remove me
#include <grpc/support/string_util.h>
#include <stdlib.h>
#include <string.h>

namespace grpc_core {
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
  grpc_json* start_time = GetJsonChild(json, "creationTime");
  ASSERT_NE(start_time, nullptr);
  size_t num_events_logged =
      (size_t)strtol(num_events_logged_json->value, nullptr, 0);
  ASSERT_EQ(num_events_logged, num_events_logged_expected);
  ValidateJsonArraySize(json, "events", actual_num_events_expected);
}

void AddSimpleTrace(RefCountedPtr<ChannelTrace> tracer) {
  tracer->AddTraceEvent(ChannelTrace::Severity::Info,
                        grpc_slice_from_static_string("simple trace"));
}

// checks for the existence of all the required members of the tracer.
void ValidateChannelTrace(RefCountedPtr<ChannelTrace> tracer,
                          size_t expected_num_event_logged, size_t max_nodes) {
  if (!max_nodes) return;
  char* json_str = tracer->RenderTrace();
  grpc::testing::ValidateChannelTraceProtoJsonTranslation(json_str);
  grpc_json* json = grpc_json_parse_string(json_str);
  ValidateChannelTraceData(json, expected_num_event_logged,
                           GPR_MIN(expected_num_event_logged, max_nodes));
  grpc_json_destroy(json);
  gpr_free(json_str);
}

void ValidateTraceDataMatchedUuidLookup(RefCountedPtr<ChannelTrace> tracer) {
  intptr_t uuid = tracer->GetUuid();
  if (uuid == -1) return;  // Doesn't make sense to lookup if tracing disabled
  char* tracer_json_str = tracer->RenderTrace();
  ChannelTrace* uuid_lookup =
      grpc_channel_trace_registry_get_channel_trace(uuid);
  char* uuid_lookup_json_str = uuid_lookup->RenderTrace();
  EXPECT_EQ(strcmp(tracer_json_str, uuid_lookup_json_str), 0);
  gpr_free(tracer_json_str);
  gpr_free(uuid_lookup_json_str);
}

}  // anonymous namespace

class ChannelTracerTest : public ::testing::TestWithParam<size_t> {};

// Tests basic ChannelTrace functionality like construction, adding trace, and
// lookups by uuid.
TEST_P(ChannelTracerTest, BasicTest) {
  grpc_core::ExecCtx exec_ctx;
  RefCountedPtr<ChannelTrace> tracer = MakeRefCounted<ChannelTrace>(GetParam());
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  ValidateTraceDataMatchedUuidLookup(tracer);
  tracer->AddTraceEvent(ChannelTrace::Severity::Info,
                        grpc_slice_from_static_string("trace three"));
  tracer->AddTraceEvent(ChannelTrace::Severity::Error,
                        grpc_slice_from_static_string("trace four error"));
  ValidateChannelTrace(tracer, 4, GetParam());
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  ValidateChannelTrace(tracer, 6, GetParam());
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  ValidateChannelTrace(tracer, 10, GetParam());
  ValidateTraceDataMatchedUuidLookup(tracer);
  tracer.reset(nullptr);
}

// Tests more complex functionality, like a parent channel tracking
// subchannles. This exercises the ref/unref patterns since the parent tracer
// and this function will both hold refs to the subchannel.
TEST_P(ChannelTracerTest, ComplexTest) {
  grpc_core::ExecCtx exec_ctx;
  RefCountedPtr<ChannelTrace> tracer = MakeRefCounted<ChannelTrace>(GetParam());
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  RefCountedPtr<ChannelTrace> sc1 = MakeRefCounted<ChannelTrace>(GetParam());
  tracer->AddTraceEventReferencingSubchannel(
      ChannelTrace::Severity::Info,
      grpc_slice_from_static_string("subchannel one created"), sc1);
  ValidateChannelTrace(tracer, 3, GetParam());
  AddSimpleTrace(sc1);
  AddSimpleTrace(sc1);
  AddSimpleTrace(sc1);
  ValidateChannelTrace(sc1, 3, GetParam());
  AddSimpleTrace(sc1);
  AddSimpleTrace(sc1);
  AddSimpleTrace(sc1);
  ValidateChannelTrace(sc1, 6, GetParam());
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  ValidateChannelTrace(tracer, 5, GetParam());
  ValidateTraceDataMatchedUuidLookup(tracer);
  RefCountedPtr<ChannelTrace> sc2 = MakeRefCounted<ChannelTrace>(GetParam());
  tracer->AddTraceEventReferencingChannel(
      ChannelTrace::Severity::Info,
      grpc_slice_from_static_string("LB channel two created"), sc2);
  tracer->AddTraceEventReferencingSubchannel(
      ChannelTrace::Severity::Warning,
      grpc_slice_from_static_string("subchannel one inactive"), sc1);
  ValidateChannelTrace(tracer, 7, GetParam());
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  ValidateTraceDataMatchedUuidLookup(tracer);
  tracer.reset(nullptr);
  sc1.reset(nullptr);
  sc2.reset(nullptr);
}

// Test a case in which the parent channel has subchannels and the subchannels
// have connections. Ensures that everything lives as long as it should then
// gets deleted.
TEST_P(ChannelTracerTest, TestNesting) {
  grpc_core::ExecCtx exec_ctx;
  RefCountedPtr<ChannelTrace> tracer = MakeRefCounted<ChannelTrace>(GetParam());
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  ValidateChannelTrace(tracer, 2, GetParam());
  RefCountedPtr<ChannelTrace> sc1 = MakeRefCounted<ChannelTrace>(GetParam());
  tracer->AddTraceEventReferencingChannel(
      ChannelTrace::Severity::Info,
      grpc_slice_from_static_string("subchannel one created"), sc1);
  ValidateChannelTrace(tracer, 3, GetParam());
  AddSimpleTrace(sc1);
  RefCountedPtr<ChannelTrace> conn1 = MakeRefCounted<ChannelTrace>(GetParam());
  // nesting one level deeper.
  sc1->AddTraceEventReferencingSubchannel(
      ChannelTrace::Severity::Info,
      grpc_slice_from_static_string("connection one created"), conn1);
  ValidateChannelTrace(tracer, 3, GetParam());
  AddSimpleTrace(conn1);
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  ValidateChannelTrace(tracer, 5, GetParam());
  ValidateChannelTrace(conn1, 1, GetParam());
  RefCountedPtr<ChannelTrace> sc2 = MakeRefCounted<ChannelTrace>(GetParam());
  tracer->AddTraceEventReferencingSubchannel(
      ChannelTrace::Severity::Info,
      grpc_slice_from_static_string("subchannel two created"), sc2);
  // this trace should not get added to the parents children since it is already
  // present in the tracer.
  tracer->AddTraceEventReferencingChannel(
      ChannelTrace::Severity::Warning,
      grpc_slice_from_static_string("subchannel one inactive"), sc1);
  AddSimpleTrace(tracer);
  ValidateChannelTrace(tracer, 8, GetParam());
  tracer.reset(nullptr);
  sc1.reset(nullptr);
  sc2.reset(nullptr);
  conn1.reset(nullptr);
}

INSTANTIATE_TEST_CASE_P(ChannelTracerTestSweep, ChannelTracerTest,
                        ::testing::Values(0, 1, 2, 6, 10, 15));

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
