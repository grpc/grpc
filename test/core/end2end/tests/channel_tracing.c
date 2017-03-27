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

#include "test/core/end2end/end2end_tests.h"

#include <stdio.h>
#include <string.h>

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_tracer.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/channel.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/channel_tracing_utils.h"

static void *tag(intptr_t t) { return (void *)t; }

static grpc_end2end_test_fixture begin_test(grpc_end2end_test_config config,
                                            const char *test_name,
                                            grpc_channel_args *client_args,
                                            grpc_channel_args *server_args) {
  grpc_end2end_test_fixture f;
  gpr_log(GPR_INFO, "Running test: %s/%s", test_name, config.name);
  f = config.create_fixture(client_args, server_args);
  config.init_server(&f, server_args);
  config.init_client(&f, client_args);
  return f;
}

static gpr_timespec n_seconds_time(int n) {
  return grpc_timeout_seconds_to_deadline(n);
}

static gpr_timespec five_seconds_time(void) { return n_seconds_time(5); }

static void drain_cq(grpc_completion_queue *cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, five_seconds_time(), NULL);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

static void shutdown_server(grpc_end2end_test_fixture *f) {
  if (!f->server) return;
  grpc_server_shutdown_and_notify(f->server, f->cq, tag(1000));
  GPR_ASSERT(grpc_completion_queue_pluck(
                 f->cq, tag(1000), grpc_timeout_seconds_to_deadline(5), NULL)
                 .type == GRPC_OP_COMPLETE);
  grpc_server_destroy(f->server);
  f->server = NULL;
}

static void shutdown_client(grpc_end2end_test_fixture *f) {
  if (!f->client) return;
  grpc_channel_destroy(f->client);
  f->client = NULL;
}

static void end_test(grpc_end2end_test_fixture *f) {
  shutdown_server(f);
  shutdown_client(f);

  grpc_completion_queue_shutdown(f->cq);
  drain_cq(f->cq);
  grpc_completion_queue_destroy(f->cq);
}

static void check_channel_trace(grpc_end2end_test_config config,
                                grpc_end2end_test_fixture f, size_t max_nodes) {
  char *json_str = grpc_channel_get_trace(f.client);
  GPR_ASSERT(json_str);
  gpr_log(GPR_DEBUG, "\n%s", json_str);
  grpc_json *json = grpc_json_parse_string(json_str);
  GPR_ASSERT(json);
  validate_channel_data(json, 1, max_nodes);
  grpc_json_destroy(json);
  gpr_free(json_str);
}

static void test_create_channel(grpc_end2end_test_config config,
                                size_t max_nodes) {
  grpc_end2end_test_fixture f;

  // ensure ths fixture has tracing enabled
  grpc_arg arg;
  arg.type = GRPC_ARG_INTEGER;
  arg.key = GRPC_ARG_CHANNEL_TRACING_MAX_NODES;
  arg.value.integer = (int)max_nodes;

  grpc_channel_args chan_args;

  chan_args.num_args = 1;
  chan_args.args = &arg;

  f = begin_test(config, "test_channel_tracing", &chan_args, NULL);
  check_channel_trace(config, f, max_nodes);
  end_test(&f);
  config.tear_down_data(&f);
}

static void test_create_channel_no_tracing(grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f;
  f = begin_test(config, "test_channel_tracing_no_tracing", NULL, NULL);
  char *json_str = grpc_channel_get_trace(f.client);
  GPR_ASSERT(!json_str);
  end_test(&f);
  config.tear_down_data(&f);
}

void channel_tracing(grpc_end2end_test_config config) {
  test_create_channel(config, 0);
  test_create_channel(config, 1);
  test_create_channel_no_tracing(config);
}

void channel_tracing_pre_init(void) {}
