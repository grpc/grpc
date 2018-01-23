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
#include <grpc/support/useful.h>

#include "src/core/lib/channel/channel_tracer.h"
#include "src/core/lib/iomgr/exec_ctx.h"

#include "test/core/util/channel_tracing_utils.h"
#include "test/core/util/test_config.h"

using grpc_core::ChannelTracer;

static void add_simple_trace(ChannelTracer* tracer) {
  tracer->AddTrace(grpc_slice_from_static_string("simple trace"),
                   GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error"),
                   GRPC_CHANNEL_READY, nullptr);
}

// checks for the existence of all the required members of the tracer.
static void validate_tracer(ChannelTracer* tracer,
                            size_t expected_num_nodes_logged,
                            size_t max_nodes) {
  if (!max_nodes) return;
  char* json_str = tracer->RenderTrace(true);
  grpc_json* json = grpc_json_parse_string(json_str);
  validate_channel_data(json, expected_num_nodes_logged,
                        GPR_MIN(expected_num_nodes_logged, max_nodes));
  grpc_json_destroy(json);
  gpr_free(json_str);
}

static void validate_tracer_data_matches_uuid_lookup(ChannelTracer* tracer) {
  intptr_t uuid = tracer->GetUuid();
  if (uuid == -1) return;  // Doesn't make sense to lookup if tracing disabled
  char* tracer_json_str = tracer->RenderTrace(true);
  char* uuid_lookup_json_str =
      ChannelTracer::GetChannelTraceFromUuid(uuid, true);
  strcmp(tracer_json_str, uuid_lookup_json_str);
  gpr_free(tracer_json_str);
  gpr_free(uuid_lookup_json_str);
}

// ensures the tracer has the correct number of children tracers.
static void validate_children(ChannelTracer* tracer,
                              size_t expected_num_children) {
  char* json_str = tracer->RenderTrace(true);
  grpc_json* json = grpc_json_parse_string(json_str);
  validate_json_array_size(json, "children", expected_num_children);
  grpc_json_destroy(json);
  gpr_free(json_str);
}

// Tests basic ChannelTracer functionality like construction, adding trace, and
// lookups by uuid.
static void test_basic_channel_tracing(size_t max_nodes) {
  grpc_core::ExecCtx exec_ctx;
  ChannelTracer tracer(max_nodes);
  add_simple_trace(&tracer);
  add_simple_trace(&tracer);
  validate_tracer_data_matches_uuid_lookup(&tracer);
  tracer.AddTrace(
      grpc_slice_from_static_string("trace three"),
      grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error"),
                         GRPC_ERROR_INT_HTTP2_ERROR, 2),
      GRPC_CHANNEL_IDLE, nullptr);
  tracer.AddTrace(grpc_slice_from_static_string("trace four"), GRPC_ERROR_NONE,
                  GRPC_CHANNEL_SHUTDOWN, nullptr);
  validate_tracer(&tracer, 4, max_nodes);
  add_simple_trace(&tracer);
  add_simple_trace(&tracer);
  validate_tracer(&tracer, 6, max_nodes);
  add_simple_trace(&tracer);
  add_simple_trace(&tracer);
  add_simple_trace(&tracer);
  add_simple_trace(&tracer);
  validate_tracer(&tracer, 10, max_nodes);
  validate_tracer_data_matches_uuid_lookup(&tracer);
  tracer.Unref();
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
  ChannelTracer tracer(max_nodes);
  add_simple_trace(&tracer);
  add_simple_trace(&tracer);
  ChannelTracer sc1(max_nodes);
  tracer.AddTrace(grpc_slice_from_static_string("subchannel one created"),
                  GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, &sc1);
  validate_tracer(&tracer, 3, max_nodes);
  add_simple_trace(&sc1);
  add_simple_trace(&sc1);
  add_simple_trace(&sc1);
  validate_tracer(&sc1, 3, max_nodes);
  add_simple_trace(&sc1);
  add_simple_trace(&sc1);
  add_simple_trace(&sc1);
  validate_tracer(&sc1, 6, max_nodes);
  add_simple_trace(&tracer);
  add_simple_trace(&tracer);
  validate_tracer(&tracer, 5, max_nodes);
  validate_tracer_data_matches_uuid_lookup(&tracer);
  ChannelTracer sc2(max_nodes);
  tracer.AddTrace(grpc_slice_from_static_string("subchannel two created"),
                  GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, &sc2);
  tracer.AddTrace(grpc_slice_from_static_string("subchannel one inactive"),
                  GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, &sc1);
  validate_tracer(&tracer, 7, max_nodes);
  add_simple_trace(&tracer);
  add_simple_trace(&tracer);
  add_simple_trace(&tracer);
  add_simple_trace(&tracer);
  add_simple_trace(&tracer);
  add_simple_trace(&tracer);
  // validate_tracer_data_matches_uuid_lookup(&tracer);
  sc1.Unref();
  sc2.Unref();
  tracer.Unref();
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

// Tests edge case in which the parent channel tracer is destroyed before the
// subchannel. Not sure if it is a realistic scenario, but the refcounting
// balance should still hold.
static void test_delete_parent_first() {
  grpc_core::ExecCtx exec_ctx;
  ChannelTracer tracer(3);
  add_simple_trace(&tracer);
  add_simple_trace(&tracer);
  ChannelTracer sc1(3);
  tracer.AddTrace(grpc_slice_from_static_string("subchannel one created"),
                  GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, &sc1);
  // this will cause the tracer destructor to run.
  tracer.Unref();
  sc1.Unref();
}

// Test a case in which the parent channel has subchannels and the subchannels
// have connections. Ensures that everything lives as long as it should then
// gets deleted.
static void test_nesting() {
  grpc_core::ExecCtx exec_ctx;
  ChannelTracer tracer(10);
  add_simple_trace(&tracer);
  add_simple_trace(&tracer);
  ChannelTracer sc1(5);
  tracer.AddTrace(grpc_slice_from_static_string("subchannel one created"),
                  GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, &sc1);
  // channel has only one subchannel right here.
  validate_children(&tracer, 1);
  add_simple_trace(&sc1);
  ChannelTracer conn1(5);
  // nesting one level deeper.
  sc1.AddTrace(grpc_slice_from_static_string("connection one created"),
               GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, &conn1);
  validate_children(&sc1, 1);
  add_simple_trace(&conn1);
  add_simple_trace(&tracer);
  add_simple_trace(&tracer);
  ChannelTracer sc2(5);
  tracer.AddTrace(grpc_slice_from_static_string("subchannel two created"),
                  GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, &sc2);
  validate_children(&tracer, 2);
  // this trace should not get added to the parents children since it is already
  // present in the tracer.
  tracer.AddTrace(grpc_slice_from_static_string("subchannel one inactive"),
                  GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, &sc1);
  validate_children(&tracer, 2);
  add_simple_trace(&tracer);

  conn1.Unref();
  sc1.Unref();
  sc2.Unref();
  tracer.Unref();
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_basic_channel_tracing(5);
  test_basic_channel_sizing();
  test_complex_channel_tracing(5);
  test_complex_channel_sizing();
  test_delete_parent_first();
  test_nesting();
  grpc_shutdown();
  return 0;
}
