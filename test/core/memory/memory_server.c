/*
 *
 * Copyright 2016, Google Inc.
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

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <grpc/support/alloc.h>
#include <grpc/support/cmdline.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include "test/core/util/memory_counters.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

static grpc_completion_queue *cq;
static grpc_server *server;
static grpc_call *call;
static grpc_call_details call_details;
static grpc_metadata_array request_metadata_recv;
static grpc_metadata_array initial_metadata_send;
static grpc_byte_buffer *payload_buffer = NULL;
/* Used to drain the terminal read in unary calls. */
static grpc_byte_buffer *terminal_buffer = NULL;

static grpc_op read_op;
static int was_cancelled = 2;
static grpc_op unary_ops[6];
static int got_sigint = 0;

static struct grpc_memory_counters counters, previous_counters;

static void show_counters() {
  gpr_log(GPR_INFO, "  actual memory allocated:       %zu", counters.total_size_relative);
  gpr_log(GPR_INFO, "  total memory allocated:        %zu", counters.total_size_absolute);
  gpr_log(GPR_INFO, "  current number of allocations: %zu", counters.total_allocs_relative);
  gpr_log(GPR_INFO, "  total number of allocations:   %zu", counters.total_allocs_absolute);
}

static void show_difference() {
  gpr_log(GPR_INFO, "  actual memory allocated:       %zi", counters.total_size_relative - previous_counters.total_size_relative);
  gpr_log(GPR_INFO, "  total memory allocated:        %zi", counters.total_size_absolute - previous_counters.total_size_absolute);
  gpr_log(GPR_INFO, "  current number of allocations: %zi", counters.total_allocs_relative - previous_counters.total_allocs_relative);
  gpr_log(GPR_INFO, "  total number of allocations:   %zi", counters.total_allocs_absolute - previous_counters.total_allocs_absolute);
}

static void memory_probe(const char *op) {
  gpr_log(GPR_INFO, "Server - Memory usage after %s:", op);
  counters = grpc_memory_counters_snapshot();
  show_counters();
  gpr_log(GPR_INFO, "Server - Difference since last probe:");
  show_difference();
  previous_counters = counters;
  gpr_log(GPR_INFO, "----------------");
}

static void *tag(intptr_t t) { return (void *)t; }

typedef enum {
  MEMORY_SERVER_NEW_REQUEST = 1,
  MEMORY_SERVER_READ_FOR_UNARY,
  MEMORY_SERVER_BATCH_OPS_FOR_UNARY,
} memory_server_tags;

typedef struct {
  gpr_refcount pending_ops;
  uint32_t flags;
} call_state;

static void request_call(void) {
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_server_request_call(server, &call, &call_details, &request_metadata_recv,
                           cq, cq, tag(MEMORY_SERVER_NEW_REQUEST));
  memory_probe("grpc_metadata_array_init & grpc_server_request_call");
}

static void handle_unary_method(void) {
  grpc_op *op;
  grpc_call_error error;

  grpc_metadata_array_init(&initial_metadata_send);

  op = unary_ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message = &terminal_buffer;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  if (payload_buffer == NULL) {
    gpr_log(GPR_INFO, "NULL payload buffer !!!");
  }
  op->data.send_message = payload_buffer;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status_details = "";
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;

  error = grpc_call_start_batch(call, unary_ops, (size_t)(op - unary_ops),
                                tag(MEMORY_SERVER_BATCH_OPS_FOR_UNARY), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  memory_probe("grpc_call_start_batch (5 ops :: handle method)");
}

static void start_read_op(int t) {
  grpc_call_error error;
  /* Starting read at server */
  read_op.op = GRPC_OP_RECV_MESSAGE;
  read_op.data.recv_message = &payload_buffer;
  error = grpc_call_start_batch(call, &read_op, 1, tag(t), NULL);
  memory_probe("grpc_call_start_batch (1 op :: start read op)");
  GPR_ASSERT(GRPC_CALL_OK == error);
}

static void sigint_handler(int x) { got_sigint = 1; }

int main(int argc, char **argv) {
  grpc_event ev;
  call_state *s;
  char *addr_buf = NULL;
  gpr_cmdline *cl;
  int shutdown_started = 0;
  int shutdown_finished = 0;

  char *addr = NULL;

  char *fake_argv[1];

  grpc_memory_counters_init();
  previous_counters = grpc_memory_counters_snapshot();

  GPR_ASSERT(argc >= 1);
  fake_argv[0] = argv[0];
  grpc_test_init(1, fake_argv);

  grpc_init();
  srand((unsigned)clock());

  cl = gpr_cmdline_create("memory server");
  gpr_cmdline_add_string(cl, "bind", "Bind host:port", &addr);
  gpr_cmdline_parse(cl, argc, argv);
  gpr_cmdline_destroy(cl);

  if (addr == NULL) {
    gpr_log(GPR_ERROR, "Please specificy the --bind argument.");
    abort();
  }
  gpr_log(GPR_INFO, "creating server on: %s", addr);

  memory_probe("initialization");

  cq = grpc_completion_queue_create(NULL);
  memory_probe("grpc_completion_queue_create");
  server = grpc_server_create(NULL, NULL);
  memory_probe("grpc_server_create");
  GPR_ASSERT(grpc_server_add_insecure_http2_port(server, addr));
  memory_probe("grpc_server_add_insecure_http2_port");
  grpc_server_register_completion_queue(server, cq, NULL);
  memory_probe("grpc_server_register_completion_queue");
  grpc_server_start(server);

  gpr_free(addr_buf);
  addr = addr_buf = NULL;
  memory_probe("grpc_server_start");

  grpc_call_details_init(&call_details);
  memory_probe("grpc_call_details_init");

  request_call();

  signal(SIGINT, sigint_handler);
  while (!shutdown_finished) {
    if (got_sigint && !shutdown_started) {
      gpr_log(GPR_INFO, "Shutting down due to SIGINT");
      grpc_server_shutdown_and_notify(server, cq, tag(1000));
      memory_probe("grpc_server_shutdown_and_notify");
      GPR_ASSERT(grpc_completion_queue_pluck(
                     cq, tag(1000), GRPC_TIMEOUT_SECONDS_TO_DEADLINE(5), NULL)
                     .type == GRPC_OP_COMPLETE);
      memory_probe("grpc_completion_queue_pluck");
      grpc_completion_queue_shutdown(cq);
      shutdown_started = 1;
      memory_probe("grpc_completion_queue_shutdown");
    }
    ev = grpc_completion_queue_next(
        cq, gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                         gpr_time_from_micros(1000000, GPR_TIMESPAN)),
        NULL);
    s = ev.tag;
    memory_probe("grpc_completion_queue_next");
    switch (ev.type) {
      case GRPC_OP_COMPLETE:
        switch ((intptr_t)s) {
          case MEMORY_SERVER_NEW_REQUEST:
            if (call != NULL) {
              /* Received unary call. Can do all ops in one batch. */
              start_read_op(MEMORY_SERVER_READ_FOR_UNARY);
            } else {
              GPR_ASSERT(shutdown_started);
            }
            /*      request_call();
             */
            break;
          case MEMORY_SERVER_READ_FOR_UNARY:
            /* Finished payload read for unary. Start all reamaining
             *  unary ops in a batch.
             */
            handle_unary_method();
            break;
          case MEMORY_SERVER_BATCH_OPS_FOR_UNARY:
            /* Finished unary call. */
            grpc_byte_buffer_destroy(payload_buffer);
            payload_buffer = NULL;
            grpc_call_destroy(call);
            memory_probe("grpc_byte_buffer_destroy & grpc_call_destroy");
            if (!shutdown_started) request_call();
            break;
        }
        break;
      case GRPC_QUEUE_SHUTDOWN:
        GPR_ASSERT(shutdown_started);
        shutdown_finished = 1;
        break;
      case GRPC_QUEUE_TIMEOUT:
        break;
    }
  }
  grpc_call_details_destroy(&call_details);
  memory_probe("grpc_call_details_destroy");

  grpc_server_destroy(server);
  memory_probe("grpc_server_destroy");
  grpc_completion_queue_destroy(cq);
  memory_probe("grpc_completion_queue_destroy");
  grpc_shutdown();
  memory_probe("grpc_shutdown");
  grpc_memory_counters_destroy();
  return 0;
}
