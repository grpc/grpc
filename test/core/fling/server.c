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

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
/* This is for _exit() below, which is temporary. */
#include <unistd.h>
#endif

#include <grpc/support/alloc.h>
#include <grpc/support/cmdline.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include "src/core/lib/profiling/timers.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/util/grpc_profiler.h"
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
static grpc_op metadata_send_op;
static grpc_op write_op;
static grpc_op status_op[2];
static int was_cancelled = 2;
static grpc_op unary_ops[6];
static int got_sigint = 0;

static void *tag(intptr_t t) { return (void *)t; }

typedef enum {
  FLING_SERVER_NEW_REQUEST = 1,
  FLING_SERVER_READ_FOR_UNARY,
  FLING_SERVER_BATCH_OPS_FOR_UNARY,
  FLING_SERVER_SEND_INIT_METADATA_FOR_STREAMING,
  FLING_SERVER_READ_FOR_STREAMING,
  FLING_SERVER_WRITE_FOR_STREAMING,
  FLING_SERVER_SEND_STATUS_FOR_STREAMING
} fling_server_tags;

typedef struct {
  gpr_refcount pending_ops;
  uint32_t flags;
} call_state;

static void request_call(void) {
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_server_request_call(server, &call, &call_details, &request_metadata_recv,
                           cq, cq, tag(FLING_SERVER_NEW_REQUEST));
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
  op->data.recv_message.recv_message = &terminal_buffer;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  if (payload_buffer == NULL) {
    gpr_log(GPR_INFO, "NULL payload buffer !!!");
  }
  op->data.send_message.send_message = payload_buffer;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status_details = NULL;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;

  error = grpc_call_start_batch(call, unary_ops, (size_t)(op - unary_ops),
                                tag(FLING_SERVER_BATCH_OPS_FOR_UNARY), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
}

static void send_initial_metadata(void) {
  grpc_call_error error;
  void *tagarg = tag(FLING_SERVER_SEND_INIT_METADATA_FOR_STREAMING);
  grpc_metadata_array_init(&initial_metadata_send);
  metadata_send_op.op = GRPC_OP_SEND_INITIAL_METADATA;
  metadata_send_op.data.send_initial_metadata.count = 0;
  error = grpc_call_start_batch(call, &metadata_send_op, 1, tagarg, NULL);

  GPR_ASSERT(GRPC_CALL_OK == error);
}

static void start_read_op(int t) {
  grpc_call_error error;
  /* Starting read at server */
  read_op.op = GRPC_OP_RECV_MESSAGE;
  read_op.data.recv_message.recv_message = &payload_buffer;
  error = grpc_call_start_batch(call, &read_op, 1, tag(t), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
}

static void start_write_op(void) {
  grpc_call_error error;
  void *tagarg = tag(FLING_SERVER_WRITE_FOR_STREAMING);
  /* Starting write at server */
  write_op.op = GRPC_OP_SEND_MESSAGE;
  if (payload_buffer == NULL) {
    gpr_log(GPR_INFO, "NULL payload buffer !!!");
  }
  write_op.data.send_message.send_message = payload_buffer;
  error = grpc_call_start_batch(call, &write_op, 1, tagarg, NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
}

static void start_send_status(void) {
  grpc_call_error error;
  void *tagarg = tag(FLING_SERVER_SEND_STATUS_FOR_STREAMING);
  status_op[0].op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  status_op[0].data.send_status_from_server.status = GRPC_STATUS_OK;
  status_op[0].data.send_status_from_server.trailing_metadata_count = 0;
  status_op[0].data.send_status_from_server.status_details = NULL;
  status_op[1].op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  status_op[1].data.recv_close_on_server.cancelled = &was_cancelled;

  error = grpc_call_start_batch(call, status_op, 2, tagarg, NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
}

/* We have some sort of deadlock, so let's not exit gracefully for now.
   When that is resolved, please remove the #include <unistd.h> above. */
static void sigint_handler(int x) { _exit(0); }

int main(int argc, char **argv) {
  grpc_event ev;
  call_state *s;
  char *addr_buf = NULL;
  gpr_cmdline *cl;
  int shutdown_started = 0;
  int shutdown_finished = 0;

  int secure = 0;
  char *addr = NULL;

  char *fake_argv[1];

  gpr_timers_set_log_filename("latency_trace.fling_server.txt");

  GPR_ASSERT(argc >= 1);
  fake_argv[0] = argv[0];
  grpc_test_init(1, fake_argv);

  grpc_init();
  srand((unsigned)clock());

  cl = gpr_cmdline_create("fling server");
  gpr_cmdline_add_string(cl, "bind", "Bind host:port", &addr);
  gpr_cmdline_add_flag(cl, "secure", "Run with security?", &secure);
  gpr_cmdline_parse(cl, argc, argv);
  gpr_cmdline_destroy(cl);

  if (addr == NULL) {
    gpr_join_host_port(&addr_buf, "::", grpc_pick_unused_port_or_die());
    addr = addr_buf;
  }
  gpr_log(GPR_INFO, "creating server on: %s", addr);

  cq = grpc_completion_queue_create(NULL);
  if (secure) {
    grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {test_server1_key,
                                                    test_server1_cert};
    grpc_server_credentials *ssl_creds = grpc_ssl_server_credentials_create(
        NULL, &pem_key_cert_pair, 1, 0, NULL);
    server = grpc_server_create(NULL, NULL);
    GPR_ASSERT(grpc_server_add_secure_http2_port(server, addr, ssl_creds));
    grpc_server_credentials_release(ssl_creds);
  } else {
    server = grpc_server_create(NULL, NULL);
    GPR_ASSERT(grpc_server_add_insecure_http2_port(server, addr));
  }
  grpc_server_register_completion_queue(server, cq, NULL);
  grpc_server_start(server);

  gpr_free(addr_buf);
  addr = addr_buf = NULL;

  grpc_call_details_init(&call_details);

  request_call();

  grpc_profiler_start("server.prof");
  signal(SIGINT, sigint_handler);
  while (!shutdown_finished) {
    if (got_sigint && !shutdown_started) {
      gpr_log(GPR_INFO, "Shutting down due to SIGINT");
      grpc_server_shutdown_and_notify(server, cq, tag(1000));
      GPR_ASSERT(grpc_completion_queue_pluck(
                     cq, tag(1000), grpc_timeout_seconds_to_deadline(5), NULL)
                     .type == GRPC_OP_COMPLETE);
      grpc_completion_queue_shutdown(cq);
      shutdown_started = 1;
    }
    ev = grpc_completion_queue_next(
        cq, gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                         gpr_time_from_micros(1000000, GPR_TIMESPAN)),
        NULL);
    s = ev.tag;
    switch (ev.type) {
      case GRPC_OP_COMPLETE:
        switch ((intptr_t)s) {
          case FLING_SERVER_NEW_REQUEST:
            if (call != NULL) {
              if (0 == grpc_slice_str_cmp(call_details.method,
                                          "/Reflector/reflectStream")) {
                /* Received streaming call. Send metadata here. */
                start_read_op(FLING_SERVER_READ_FOR_STREAMING);
                send_initial_metadata();
              } else {
                /* Received unary call. Can do all ops in one batch. */
                start_read_op(FLING_SERVER_READ_FOR_UNARY);
              }
            } else {
              GPR_ASSERT(shutdown_started);
            }
            /*      request_call();
             */
            break;
          case FLING_SERVER_READ_FOR_STREAMING:
            if (payload_buffer != NULL) {
              /* Received payload from client. */
              start_write_op();
            } else {
              /* Received end of stream from client. */
              start_send_status();
            }
            break;
          case FLING_SERVER_WRITE_FOR_STREAMING:
            /* Write completed at server  */
            grpc_byte_buffer_destroy(payload_buffer);
            payload_buffer = NULL;
            start_read_op(FLING_SERVER_READ_FOR_STREAMING);
            break;
          case FLING_SERVER_SEND_INIT_METADATA_FOR_STREAMING:
            /* Metadata send completed at server */
            break;
          case FLING_SERVER_SEND_STATUS_FOR_STREAMING:
            /* Send status and close completed at server */
            grpc_call_destroy(call);
            if (!shutdown_started) request_call();
            break;
          case FLING_SERVER_READ_FOR_UNARY:
            /* Finished payload read for unary. Start all reamaining
             *  unary ops in a batch.
             */
            handle_unary_method();
            break;
          case FLING_SERVER_BATCH_OPS_FOR_UNARY:
            /* Finished unary call. */
            grpc_byte_buffer_destroy(payload_buffer);
            payload_buffer = NULL;
            grpc_call_destroy(call);
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
  grpc_profiler_stop();
  grpc_call_details_destroy(&call_details);

  grpc_server_destroy(server);
  grpc_completion_queue_destroy(cq);
  grpc_shutdown();
  return 0;
}
