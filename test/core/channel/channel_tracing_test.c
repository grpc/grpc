/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include "src/core/lib/channel/channel_tracer.h"

#include "test/core/util/test_config.h"

static void add_simple_trace(grpc_channel_tracer* tracer) {
  grpc_channel_tracer_add_trace(tracer, "simple trace",
                                GRPC_ERROR_CREATE("Error"), GRPC_CHANNEL_READY,
                                NULL);
}

static grpc_json* get_json_child(grpc_json* parent, const char* key) {
  grpc_json* child = parent->child;
  while (child) {
    if (child->key && !strcmp(child->key, key)) {
      return child;
    }
    child = child->next;
  }
  return NULL;
}

static size_t get_json_array_size(grpc_json* arr) {
  GPR_ASSERT(arr->type == GRPC_JSON_ARRAY);
  size_t count = 0;
  grpc_json* child = arr->child;
  while (child) {
    count++;
    child = child->next;
  }
  return count;
}

static void validate_channel_data(grpc_channel_tracer* tracer,
                                  size_t num_nodes_logged_golden,
                                  size_t actual_num_nodes_golden) {
  grpc_json* json = grpc_channel_tracer_get_trace(tracer);
  grpc_json* channel_data = get_json_child(json, "channelData");

  grpc_json* num_nodes_logged_json =
      get_json_child(channel_data, "numNodesLogged");
  GPR_ASSERT(num_nodes_logged_json);
  size_t num_nodes_logged =
      (size_t)strtol(num_nodes_logged_json->value, NULL, 0);
  GPR_ASSERT(num_nodes_logged == num_nodes_logged_golden);

  grpc_json* nodes = get_json_child(channel_data, "nodes");
  GPR_ASSERT(nodes);
  size_t actual_num_nodes = get_json_array_size(nodes);
  GPR_ASSERT(actual_num_nodes == actual_num_nodes_golden);

  grpc_json_destroy(json);
}

static void test_basic_channel_tracing(size_t max_nodes) {
  grpc_channel_tracer* tracer = GRPC_CHANNEL_TRACER_CREATE(max_nodes);

  // add random trace elements with various elements.
  add_simple_trace(tracer);
  add_simple_trace(tracer);
  grpc_channel_tracer_add_trace(
      tracer, "trace three", grpc_error_set_int(GRPC_ERROR_CREATE("Error"),
                                                GRPC_ERROR_INT_HTTP2_ERROR, 2),
      GRPC_CHANNEL_INIT, NULL);
  grpc_channel_tracer_add_trace(tracer, "trace four", GRPC_ERROR_NONE,
                                GRPC_CHANNEL_SHUTDOWN, NULL);

  validate_channel_data(tracer, 4, GPR_MIN(4, max_nodes));

  add_simple_trace(tracer);
  add_simple_trace(tracer);

  validate_channel_data(tracer, 6, GPR_MIN(6, max_nodes));

  add_simple_trace(tracer);
  add_simple_trace(tracer);
  add_simple_trace(tracer);
  add_simple_trace(tracer);

  validate_channel_data(tracer, 10, GPR_MIN(10, max_nodes));

  GRPC_CHANNEL_TRACER_UNREF(tracer);
}

static void test_basic_channel_sizing() {
  test_basic_channel_tracing(1);
  test_basic_channel_tracing(2);
  test_basic_channel_tracing(6);
  test_basic_channel_tracing(10);
  test_basic_channel_tracing(15);
}

static void test_complex_channel_tracing(size_t max_nodes) {
  grpc_channel_tracer* tracer = GRPC_CHANNEL_TRACER_CREATE(max_nodes);

  add_simple_trace(tracer);
  add_simple_trace(tracer);

  grpc_channel_tracer* sc1 = GRPC_CHANNEL_TRACER_CREATE(max_nodes);

  grpc_channel_tracer_add_trace(tracer, "subchannel one created",
                                GRPC_ERROR_NONE, GRPC_CHANNEL_INIT, sc1);

  validate_channel_data(tracer, 3, GPR_MIN(3, max_nodes));

  add_simple_trace(sc1);
  add_simple_trace(sc1);
  add_simple_trace(sc1);

  validate_channel_data(sc1, 3, GPR_MIN(3, max_nodes));

  add_simple_trace(sc1);
  add_simple_trace(sc1);
  add_simple_trace(sc1);

  validate_channel_data(sc1, 6, GPR_MIN(6, max_nodes));

  add_simple_trace(tracer);
  add_simple_trace(tracer);

  validate_channel_data(tracer, 5, GPR_MIN(5, max_nodes));

  grpc_channel_tracer* sc2 = GRPC_CHANNEL_TRACER_CREATE(max_nodes);

  grpc_channel_tracer_add_trace(tracer, "subchannel two created",
                                GRPC_ERROR_NONE, GRPC_CHANNEL_INIT, sc2);

  grpc_channel_tracer_add_trace(tracer, "subchannel one inactive",
                                GRPC_ERROR_NONE, GRPC_CHANNEL_INIT, sc1);

  validate_channel_data(tracer, 7, GPR_MIN(7, max_nodes));

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
  test_complex_channel_tracing(1);
  test_complex_channel_tracing(2);
  test_complex_channel_tracing(6);
  test_complex_channel_tracing(10);
  test_complex_channel_tracing(15);
}

static void test_delete_parent_first() {
  grpc_channel_tracer* tracer = GRPC_CHANNEL_TRACER_CREATE(3);

  add_simple_trace(tracer);
  add_simple_trace(tracer);

  grpc_channel_tracer* sc1 = GRPC_CHANNEL_TRACER_CREATE(3);

  grpc_channel_tracer_add_trace(tracer, "subchannel one created",
                                GRPC_ERROR_NONE, GRPC_CHANNEL_INIT, sc1);

  GRPC_CHANNEL_TRACER_UNREF(tracer);
  GRPC_CHANNEL_TRACER_UNREF(sc1);
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_basic_channel_tracing(5);
  test_basic_channel_sizing();
  test_complex_channel_tracing(5);
  test_complex_channel_sizing();
  test_delete_parent_first();
  grpc_shutdown();
  return 0;
}
