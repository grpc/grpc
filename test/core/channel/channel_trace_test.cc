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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include <gtest/gtest.h>

#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/channel/channel_trace_registry.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/exec_ctx.h"

#include "test/core/util/channel_tracing_utils.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

void AddSimpleTrace(RefCountedPtr<ChannelTrace> tracer) {
  tracer->AddTraceEvent(grpc_slice_from_static_string("simple trace"),
                        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error"),
                        GRPC_CHANNEL_READY);
}

// checks for the existence of all the required members of the tracer.
void ValidateChannelTrace(RefCountedPtr<ChannelTrace> tracer,
                          size_t expected_num_event_logged, size_t max_nodes) {
  if (!max_nodes) return;
  char* json_str = tracer->RenderTrace();
  grpc_json* json = grpc_json_parse_string(json_str);
  validate_channel_trace_data(json, expected_num_event_logged,
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
  GPR_ASSERT(strcmp(tracer_json_str, uuid_lookup_json_str) == 0);
  gpr_free(tracer_json_str);
  gpr_free(uuid_lookup_json_str);
}

// Tests basic ChannelTrace functionality like construction, adding trace, and
// lookups by uuid.
void TestBasicChannelTrace(size_t max_nodes) {
  grpc_core::ExecCtx exec_ctx;
  RefCountedPtr<ChannelTrace> tracer = MakeRefCounted<ChannelTrace>(max_nodes);
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  ValidateTraceDataMatchedUuidLookup(tracer);
  tracer->AddTraceEvent(
      grpc_slice_from_static_string("trace three"),
      grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error"),
                         GRPC_ERROR_INT_HTTP2_ERROR, 2),
      GRPC_CHANNEL_IDLE);
  tracer->AddTraceEvent(grpc_slice_from_static_string("trace four"),
                        GRPC_ERROR_NONE, GRPC_CHANNEL_SHUTDOWN);
  ValidateChannelTrace(tracer, 4, max_nodes);
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  ValidateChannelTrace(tracer, 6, max_nodes);
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  ValidateChannelTrace(tracer, 10, max_nodes);
  ValidateTraceDataMatchedUuidLookup(tracer);
  tracer.reset(nullptr);
}

// Tests more complex functionality, like a parent channel tracking
// subchannles. This exercises the ref/unref patterns since the parent tracer
// and this function will both hold refs to the subchannel.
void TestComplexChannelTrace(size_t max_nodes) {
  grpc_core::ExecCtx exec_ctx;
  RefCountedPtr<ChannelTrace> tracer = MakeRefCounted<ChannelTrace>(max_nodes);
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  RefCountedPtr<ChannelTrace> sc1 = MakeRefCounted<ChannelTrace>(max_nodes);
  tracer->AddTraceEvent(grpc_slice_from_static_string("subchannel one created"),
                        GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, sc1);
  ValidateChannelTrace(tracer, 3, max_nodes);
  AddSimpleTrace(sc1);
  AddSimpleTrace(sc1);
  AddSimpleTrace(sc1);
  ValidateChannelTrace(sc1, 3, max_nodes);
  AddSimpleTrace(sc1);
  AddSimpleTrace(sc1);
  AddSimpleTrace(sc1);
  ValidateChannelTrace(sc1, 6, max_nodes);
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  ValidateChannelTrace(tracer, 5, max_nodes);
  ValidateTraceDataMatchedUuidLookup(tracer);
  RefCountedPtr<ChannelTrace> sc2 = MakeRefCounted<ChannelTrace>(max_nodes);
  tracer->AddTraceEvent(grpc_slice_from_static_string("subchannel two created"),
                        GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, sc2);
  tracer->AddTraceEvent(
      grpc_slice_from_static_string("subchannel one inactive"), GRPC_ERROR_NONE,
      GRPC_CHANNEL_IDLE, sc1);
  ValidateChannelTrace(tracer, 7, max_nodes);
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

}  // anonymous namespace

// Calls basic test with various values for max_nodes (including 0, which turns
// the tracer off).
TEST(ChannelTracerTest, BasicTest) {
  TestBasicChannelTrace(0);
  TestBasicChannelTrace(1);
  TestBasicChannelTrace(2);
  TestBasicChannelTrace(6);
  TestBasicChannelTrace(10);
  TestBasicChannelTrace(15);
}

// Calls the complex test with a sweep of sizes for max_nodes.
TEST(ChannelTracerTest, ComplexTest) {
  TestComplexChannelTrace(0);
  TestComplexChannelTrace(1);
  TestComplexChannelTrace(2);
  TestComplexChannelTrace(6);
  TestComplexChannelTrace(10);
  TestComplexChannelTrace(15);
}

// Test a case in which the parent channel has subchannels and the subchannels
// have connections. Ensures that everything lives as long as it should then
// gets deleted.
TEST(ChannelTracerTest, TestNesting) {
  grpc_core::ExecCtx exec_ctx;
  RefCountedPtr<ChannelTrace> tracer = MakeRefCounted<ChannelTrace>(10);
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  RefCountedPtr<ChannelTrace> sc1 = MakeRefCounted<ChannelTrace>(5);
  tracer->AddTraceEvent(grpc_slice_from_static_string("subchannel one created"),
                        GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, sc1);
  AddSimpleTrace(sc1);
  RefCountedPtr<ChannelTrace> conn1 = MakeRefCounted<ChannelTrace>(5);
  // nesting one level deeper.
  sc1->AddTraceEvent(grpc_slice_from_static_string("connection one created"),
                     GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, conn1);
  AddSimpleTrace(conn1);
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  RefCountedPtr<ChannelTrace> sc2 = MakeRefCounted<ChannelTrace>(5);
  tracer->AddTraceEvent(grpc_slice_from_static_string("subchannel two created"),
                        GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, sc2);
  // this trace should not get added to the parents children since it is already
  // present in the tracer.
  tracer->AddTraceEvent(
      grpc_slice_from_static_string("subchannel one inactive"), GRPC_ERROR_NONE,
      GRPC_CHANNEL_IDLE, sc1);
  AddSimpleTrace(tracer);
  tracer.reset(nullptr);
  sc1.reset(nullptr);
  sc2.reset(nullptr);
  conn1.reset(nullptr);
}

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
