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

#include "test/core/util/channel_tracing_utils.h"
#include "test/core/util/test_config.h"

static void add_simple_trace(grpc_channel_tracer* tracer) {
  grpc_channel_tracer_add_trace(
      tracer, grpc_slice_from_static_string("simple trace"),
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error"), GRPC_CHANNEL_READY, NULL);
}

static void validate_tracer(grpc_channel_tracer* tracer, size_t expected,
                            size_t max_nodes) {
  grpc_json* json = grpc_channel_tracer_get_trace(tracer);
  validate_channel_data(json, expected, GPR_MIN(expected, max_nodes));
  grpc_json_destroy(json);
}

static void test_basic_channel_tracing(size_t max_nodes) {
  grpc_channel_tracer* tracer = GRPC_CHANNEL_TRACER_CREATE(max_nodes);

  // add random trace elements with various elements.
  add_simple_trace(tracer);
  add_simple_trace(tracer);
  grpc_channel_tracer_add_trace(
      tracer, grpc_slice_from_static_string("trace three"),
      grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error"),
                         GRPC_ERROR_INT_HTTP2_ERROR, 2),
      GRPC_CHANNEL_INIT, NULL);
  grpc_channel_tracer_add_trace(tracer,
                                grpc_slice_from_static_string("trace four"),
                                GRPC_ERROR_NONE, GRPC_CHANNEL_SHUTDOWN, NULL);

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

  add_simple_trace(tracer);
  add_simple_trace(tracer);

  grpc_channel_tracer* sc1 = GRPC_CHANNEL_TRACER_CREATE(max_nodes);

  grpc_channel_tracer_add_trace(
      tracer, grpc_slice_from_static_string("subchannel one created"),
      GRPC_ERROR_NONE, GRPC_CHANNEL_INIT, sc1);

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
      GRPC_ERROR_NONE, GRPC_CHANNEL_INIT, sc2);

  grpc_channel_tracer_add_trace(
      tracer, grpc_slice_from_static_string("subchannel one inactive"),
      GRPC_ERROR_NONE, GRPC_CHANNEL_INIT, sc1);

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

  add_simple_trace(tracer);
  add_simple_trace(tracer);

  grpc_channel_tracer* sc1 = GRPC_CHANNEL_TRACER_CREATE(3);

  grpc_channel_tracer_add_trace(
      tracer, grpc_slice_from_static_string("subchannel one created"),
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
