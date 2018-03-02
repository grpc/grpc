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

#include "src/core/lib/channel/channel_tracer.h"
#include "src/core/lib/channel/object_registry.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/exec_ctx.h"

#include "test/core/util/channel_tracing_utils.h"
#include "test/core/util/test_config.h"

using grpc_core::ChannelTrace;
using grpc_core::MakeRefCounted;
using grpc_core::RefCountedPtr;

static void add_simple_trace_event(RefCountedPtr<ChannelTrace> tracer) {
  tracer->AddTraceEvent(grpc_slice_from_static_string("simple trace"),
                        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error"),
                        GRPC_CHANNEL_READY);
}

// checks for the existence of all the required members of the tracer.
static void validate_tracer(RefCountedPtr<ChannelTrace> tracer,
                            size_t expected_num_event_logged,
                            size_t max_nodes) {
  if (!max_nodes) return;
  char* json_str = tracer->RenderTrace();
  grpc_json* json = grpc_json_parse_string(json_str);
  validate_channel_trace_data(json, expected_num_event_logged,
                              GPR_MIN(expected_num_event_logged, max_nodes));
  grpc_json_destroy(json);
  gpr_free(json_str);
}

static void validate_tracer_data_matches_uuid_lookup(
    RefCountedPtr<ChannelTrace> tracer) {
  intptr_t uuid = tracer->GetUuid();
  if (uuid == -1) return;  // Doesn't make sense to lookup if tracing disabled
  char* tracer_json_str = tracer->RenderTrace();
  void* object;
  grpc_object_registry_type type =
      grpc_object_registry_get_object(uuid, &object);
  GPR_ASSERT(type == GRPC_OBJECT_REGISTRY_CHANNEL_TRACER);
  char* uuid_lookup_json_str =
      static_cast<ChannelTrace*>(object)->RenderTrace();
  GPR_ASSERT(strcmp(tracer_json_str, uuid_lookup_json_str) == 0);
  gpr_free(tracer_json_str);
  gpr_free(uuid_lookup_json_str);
}

// Tests basic ChannelTrace functionality like construction, adding trace, and
// lookups by uuid.
static void test_basic_channel_tracing(size_t max_nodes) {
  grpc_core::ExecCtx exec_ctx;
  RefCountedPtr<ChannelTrace> tracer = MakeRefCounted<ChannelTrace>(max_nodes);
  add_simple_trace_event(tracer);
  add_simple_trace_event(tracer);
  validate_tracer_data_matches_uuid_lookup(tracer);
  tracer->AddTraceEvent(
      grpc_slice_from_static_string("trace three"),
      grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error"),
                         GRPC_ERROR_INT_HTTP2_ERROR, 2),
      GRPC_CHANNEL_IDLE);
  tracer->AddTraceEvent(grpc_slice_from_static_string("trace four"),
                        GRPC_ERROR_NONE, GRPC_CHANNEL_SHUTDOWN);
  validate_tracer(tracer, 4, max_nodes);
  add_simple_trace_event(tracer);
  add_simple_trace_event(tracer);
  validate_tracer(tracer, 6, max_nodes);
  add_simple_trace_event(tracer);
  add_simple_trace_event(tracer);
  add_simple_trace_event(tracer);
  add_simple_trace_event(tracer);
  validate_tracer(tracer, 10, max_nodes);
  validate_tracer_data_matches_uuid_lookup(tracer);
  tracer.reset(nullptr);
}

// Calls basic test with various values for max_nodes (including 0, which turns
// the tracer off).
static void test_basic_channel_sizing() {
  test_basic_channel_tracing(0);
  test_basic_channel_tracing(1);
  test_basic_channel_tracing(2);
  test_basic_channel_tracing(6);
  test_basic_channel_tracing(10);
  test_basic_channel_tracing(15);
}

// Tests more complex functionality, like a parent channel tracking
// subchannles. This exercises the ref/unref patterns since the parent tracer
// and this function will both hold refs to the subchannel.
static void test_complex_channel_tracing(size_t max_nodes) {
  grpc_core::ExecCtx exec_ctx;
  RefCountedPtr<ChannelTrace> tracer = MakeRefCounted<ChannelTrace>(max_nodes);
  add_simple_trace_event(tracer);
  add_simple_trace_event(tracer);
  RefCountedPtr<ChannelTrace> sc1 = MakeRefCounted<ChannelTrace>(max_nodes);
  tracer->AddTraceEvent(grpc_slice_from_static_string("subchannel one created"),
                        GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, sc1);
  validate_tracer(tracer, 3, max_nodes);
  add_simple_trace_event(sc1);
  add_simple_trace_event(sc1);
  add_simple_trace_event(sc1);
  validate_tracer(sc1, 3, max_nodes);
  add_simple_trace_event(sc1);
  add_simple_trace_event(sc1);
  add_simple_trace_event(sc1);
  validate_tracer(sc1, 6, max_nodes);
  add_simple_trace_event(tracer);
  add_simple_trace_event(tracer);
  validate_tracer(tracer, 5, max_nodes);
  validate_tracer_data_matches_uuid_lookup(tracer);
  RefCountedPtr<ChannelTrace> sc2 = MakeRefCounted<ChannelTrace>(max_nodes);
  tracer->AddTraceEvent(grpc_slice_from_static_string("subchannel two created"),
                        GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, sc2);
  tracer->AddTraceEvent(
      grpc_slice_from_static_string("subchannel one inactive"), GRPC_ERROR_NONE,
      GRPC_CHANNEL_IDLE, sc1);
  validate_tracer(tracer, 7, max_nodes);
  add_simple_trace_event(tracer);
  add_simple_trace_event(tracer);
  add_simple_trace_event(tracer);
  add_simple_trace_event(tracer);
  add_simple_trace_event(tracer);
  add_simple_trace_event(tracer);
  validate_tracer_data_matches_uuid_lookup(tracer);
  tracer.reset(nullptr);
  sc1.reset(nullptr);
  sc2.reset(nullptr);
}

// Calls the complex test with a sweep of sizes for max_nodes.
static void test_complex_channel_sizing() {
  test_complex_channel_tracing(0);
  test_complex_channel_tracing(1);
  test_complex_channel_tracing(2);
  test_complex_channel_tracing(6);
  test_complex_channel_tracing(10);
  test_complex_channel_tracing(15);
}

// Test a case in which the parent channel has subchannels and the subchannels
// have connections. Ensures that everything lives as long as it should then
// gets deleted.
static void test_nesting() {
  grpc_core::ExecCtx exec_ctx;
  RefCountedPtr<ChannelTrace> tracer = MakeRefCounted<ChannelTrace>(10);
  add_simple_trace_event(tracer);
  add_simple_trace_event(tracer);
  RefCountedPtr<ChannelTrace> sc1 = MakeRefCounted<ChannelTrace>(5);
  tracer->AddTraceEvent(grpc_slice_from_static_string("subchannel one created"),
                        GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, sc1);
  add_simple_trace_event(sc1);
  RefCountedPtr<ChannelTrace> conn1 = MakeRefCounted<ChannelTrace>(5);
  // nesting one level deeper.
  sc1->AddTraceEvent(grpc_slice_from_static_string("connection one created"),
                     GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, conn1);
  add_simple_trace_event(conn1);
  add_simple_trace_event(tracer);
  add_simple_trace_event(tracer);
  RefCountedPtr<ChannelTrace> sc2 = MakeRefCounted<ChannelTrace>(5);
  tracer->AddTraceEvent(grpc_slice_from_static_string("subchannel two created"),
                        GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, sc2);
  // this trace should not get added to the parents children since it is already
  // present in the tracer.
  tracer->AddTraceEvent(
      grpc_slice_from_static_string("subchannel one inactive"), GRPC_ERROR_NONE,
      GRPC_CHANNEL_IDLE, sc1);
  add_simple_trace_event(tracer);
  tracer.reset(nullptr);
  sc1.reset(nullptr);
  sc2.reset(nullptr);
  conn1.reset(nullptr);
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_basic_channel_tracing(5);
  test_basic_channel_sizing();
  test_complex_channel_tracing(5);
  test_complex_channel_sizing();
  test_nesting();
  grpc_shutdown();
  return 0;
}
