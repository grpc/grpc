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

static void add_simple_trace(grpc_channel_tracer* tracer) {
  grpc_channel_tracer_add_trace(tracer,
                                grpc_slice_from_static_string("simple trace"),
                                GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error"),
                                GRPC_CHANNEL_READY, nullptr);
}

// checks for the existence of all the required members of the tracer.
static void validate_tracer(grpc_channel_tracer* tracer,
                            size_t expected_num_nodes_logged,
                            size_t max_nodes) {
  char* json_str = grpc_channel_tracer_render_trace(tracer, true);
  grpc_json* json = grpc_json_parse_string(json_str);
  validate_channel_data(json, expected_num_nodes_logged,
                        GPR_MIN(expected_num_nodes_logged, max_nodes));
  grpc_json_destroy(json);
  gpr_free(json_str);
}

// ensures the tracer has the correct number of children tracers.
static void validate_children(grpc_channel_tracer* tracer,
                              size_t expected_num_children) {
  char* json_str = grpc_channel_tracer_render_trace(tracer, true);
  grpc_json* json = grpc_json_parse_string(json_str);
  validate_json_array_size(json, "children", expected_num_children);
  grpc_json_destroy(json);
  gpr_free(json_str);
}

static void test_basic_channel_tracing(size_t max_nodes) {
  grpc_channel_tracer* tracer = GRPC_CHANNEL_TRACER_CREATE(max_nodes);
  grpc_core::ExecCtx exec_ctx;
  add_simple_trace(tracer);
  add_simple_trace(tracer);
  grpc_channel_tracer_add_trace(
      tracer, grpc_slice_from_static_string("trace three"),
      grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error"),
                         GRPC_ERROR_INT_HTTP2_ERROR, 2),
      GRPC_CHANNEL_IDLE, nullptr);
  grpc_channel_tracer_add_trace(
      tracer, grpc_slice_from_static_string("trace four"), GRPC_ERROR_NONE,
      GRPC_CHANNEL_SHUTDOWN, nullptr);
  validate_tracer(tracer, 4, max_nodes);
  add_simple_trace(tracer);
  add_simple_trace(tracer);
  validate_tracer(tracer, 6, max_nodes);
  add_simple_trace(tracer);
  add_simple_trace(tracer);
  add_simple_trace(tracer);
  add_simple_trace(tracer);
  validate_tracer(tracer, 10, max_nodes);
  GRPC_CHANNEL_TRACER_UNREF(tracer);
}

static void test_basic_channel_sizing() {
  test_basic_channel_tracing(0);
  test_basic_channel_tracing(1);
  test_basic_channel_tracing(2);
  test_basic_channel_tracing(6);
  test_basic_channel_tracing(10);
  test_basic_channel_tracing(15);
}

static void test_complex_channel_tracing(size_t max_nodes) {
  grpc_channel_tracer* tracer = GRPC_CHANNEL_TRACER_CREATE(max_nodes);
  grpc_core::ExecCtx exec_ctx;
  add_simple_trace(tracer);
  add_simple_trace(tracer);
  grpc_channel_tracer* sc1 = GRPC_CHANNEL_TRACER_CREATE(max_nodes);
  grpc_channel_tracer_add_trace(
      tracer, grpc_slice_from_static_string("subchannel one created"),
      GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, sc1);
  validate_tracer(tracer, 3, max_nodes);
  add_simple_trace(sc1);
  add_simple_trace(sc1);
  add_simple_trace(sc1);
  validate_tracer(sc1, 3, max_nodes);
  add_simple_trace(sc1);
  add_simple_trace(sc1);
  add_simple_trace(sc1);
  validate_tracer(sc1, 6, max_nodes);
  add_simple_trace(tracer);
  add_simple_trace(tracer);
  validate_tracer(tracer, 5, max_nodes);
  grpc_channel_tracer* sc2 = GRPC_CHANNEL_TRACER_CREATE(max_nodes);
  grpc_channel_tracer_add_trace(
      tracer, grpc_slice_from_static_string("subchannel two created"),
      GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, sc2);
  grpc_channel_tracer_add_trace(
      tracer, grpc_slice_from_static_string("subchannel one inactive"),
      GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, sc1);
  validate_tracer(tracer, 7, max_nodes);
  add_simple_trace(tracer);
  add_simple_trace(tracer);
  add_simple_trace(tracer);
  add_simple_trace(tracer);
  add_simple_trace(tracer);
  add_simple_trace(tracer);
  GRPC_CHANNEL_TRACER_UNREF(sc1);
  GRPC_CHANNEL_TRACER_UNREF(sc2);
  GRPC_CHANNEL_TRACER_UNREF(tracer);
}

static void test_complex_channel_sizing() {
  test_complex_channel_tracing(0);
  test_complex_channel_tracing(1);
  test_complex_channel_tracing(2);
  test_complex_channel_tracing(6);
  test_complex_channel_tracing(10);
  test_complex_channel_tracing(15);
}

static void test_delete_parent_first() {
  grpc_channel_tracer* tracer = GRPC_CHANNEL_TRACER_CREATE(3);
  grpc_core::ExecCtx exec_ctx;
  add_simple_trace(tracer);
  add_simple_trace(tracer);
  grpc_channel_tracer* sc1 = GRPC_CHANNEL_TRACER_CREATE(3);
  grpc_channel_tracer_add_trace(
      tracer, grpc_slice_from_static_string("subchannel one created"),
      GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, sc1);
  // this will cause the tracer destructor to run.
  GRPC_CHANNEL_TRACER_UNREF(tracer);
  GRPC_CHANNEL_TRACER_UNREF(sc1);
}

static void test_nesting() {
  grpc_channel_tracer* tracer = GRPC_CHANNEL_TRACER_CREATE(10);
  grpc_core::ExecCtx exec_ctx;
  add_simple_trace(tracer);
  add_simple_trace(tracer);
  grpc_channel_tracer* sc1 = GRPC_CHANNEL_TRACER_CREATE(5);
  grpc_channel_tracer_add_trace(
      tracer, grpc_slice_from_static_string("subchannel one created"),
      GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, sc1);
  // channel has only one subchannel right here.
  validate_children(tracer, 1);
  add_simple_trace(sc1);
  grpc_channel_tracer* conn1 = GRPC_CHANNEL_TRACER_CREATE(5);
  // nesting one level deeper.
  grpc_channel_tracer_add_trace(
      sc1, grpc_slice_from_static_string("connection one created"),
      GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, conn1);
  validate_children(sc1, 1);
  add_simple_trace(conn1);
  add_simple_trace(tracer);
  add_simple_trace(tracer);
  grpc_channel_tracer* sc2 = GRPC_CHANNEL_TRACER_CREATE(5);
  grpc_channel_tracer_add_trace(
      tracer, grpc_slice_from_static_string("subchannel two created"),
      GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, sc2);
  validate_children(tracer, 2);
  // this trace should not get added to the parents children since it is already
  // present in the tracer.
  grpc_channel_tracer_add_trace(
      tracer, grpc_slice_from_static_string("subchannel one inactive"),
      GRPC_ERROR_NONE, GRPC_CHANNEL_IDLE, sc1);
  validate_children(tracer, 2);
  add_simple_trace(tracer);

  GRPC_CHANNEL_TRACER_UNREF(conn1);
  GRPC_CHANNEL_TRACER_UNREF(sc1);
  GRPC_CHANNEL_TRACER_UNREF(sc2);
  GRPC_CHANNEL_TRACER_UNREF(tracer);
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
