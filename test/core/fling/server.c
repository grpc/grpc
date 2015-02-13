/*
 *
 * Copyright 2014, Google Inc.
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

#include "test/core/util/grpc_profiler.h"
#include "test/core/util/test_config.h"
#include <grpc/support/alloc.h>
#include <grpc/support/cmdline.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include "test/core/util/port.h"
#include "test/core/end2end/data/ssl_test_data.h"

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

static void *tag(gpr_intptr t) { return (void *)t; }

typedef struct {
  gpr_refcount pending_ops;
  gpr_uint32 flags;
} call_state;

static void request_call(void) {
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);
  grpc_server_request_call(server, &call, &call_details, &request_metadata_recv,
                           cq, tag(101));
}

static void handle_unary_method(void) {
  grpc_metadata_array_init(&initial_metadata_send);
  unary_ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
  unary_ops[0].data.send_initial_metadata.count = 0;
  unary_ops[1].op = GRPC_OP_RECV_MESSAGE;
  unary_ops[1].data.recv_message = &terminal_buffer;
  unary_ops[2].op = GRPC_OP_SEND_MESSAGE;
  if (payload_buffer == NULL) {
    gpr_log(GPR_INFO, "NULL payload buffer !!!");
  }
  unary_ops[2].data.send_message = payload_buffer;
  unary_ops[3].op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  unary_ops[3].data.send_status_from_server.status = GRPC_STATUS_OK;
  unary_ops[3].data.send_status_from_server.trailing_metadata_count = 0;
  unary_ops[3].data.send_status_from_server.status_details = "";
  unary_ops[4].op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  unary_ops[4].data.recv_close_on_server.cancelled = &was_cancelled;

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call, unary_ops, 5, tag(6)));
}

static void send_initial_metadata(void) {
  grpc_metadata_array_init(&initial_metadata_send);
  metadata_send_op.op = GRPC_OP_SEND_INITIAL_METADATA;
  metadata_send_op.data.send_initial_metadata.count = 0;
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_batch(call, &metadata_send_op, 1, tag(3)));
}

static void start_read_op(int t) {
  /* Starting read at server */
  read_op.op = GRPC_OP_RECV_MESSAGE;
  read_op.data.recv_message = &payload_buffer;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call, &read_op, 1, tag(t)));
}

static void start_write_op(void) {
  /* Starting write at server */
  write_op.op = GRPC_OP_SEND_MESSAGE;
  if (payload_buffer == NULL) {
    gpr_log(GPR_INFO, "NULL payload buffer !!!");
  }
  write_op.data.send_message = payload_buffer;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call, &write_op, 1, tag(2)));
}

static void start_send_status(void) {
  status_op[0].op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  status_op[0].data.send_status_from_server.status = GRPC_STATUS_OK;
  status_op[0].data.send_status_from_server.trailing_metadata_count = 0;
  status_op[0].data.send_status_from_server.status_details = "";
  status_op[1].op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  status_op[1].data.recv_close_on_server.cancelled = &was_cancelled;

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call, status_op, 2, tag(4)));
}

static void sigint_handler(int x) { got_sigint = 1; }

int main(int argc, char **argv) {
  grpc_event *ev;
  call_state *s;
  char *addr_buf = NULL;
  gpr_cmdline *cl;
  int shutdown_started = 0;
  int shutdown_finished = 0;

  int secure = 0;
  char *addr = NULL;

  char *fake_argv[1];

  GPR_ASSERT(argc >= 1);
  fake_argv[0] = argv[0];
  grpc_test_init(1, fake_argv);

  grpc_init();
  srand(clock());

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

  cq = grpc_completion_queue_create();
  if (secure) {
    grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {test_server1_key,
                                                    test_server1_cert};
    grpc_server_credentials *ssl_creds =
        grpc_ssl_server_credentials_create(NULL, &pem_key_cert_pair, 1);
    server = grpc_secure_server_create(ssl_creds, cq, NULL);
    GPR_ASSERT(grpc_server_add_secure_http2_port(server, addr));
    grpc_server_credentials_release(ssl_creds);
  } else {
    server = grpc_server_create(cq, NULL);
    GPR_ASSERT(grpc_server_add_http2_port(server, addr));
  }
  grpc_server_start(server);

  gpr_free(addr_buf);
  addr = addr_buf = NULL;

  request_call();

  grpc_profiler_start("server.prof");
  signal(SIGINT, sigint_handler);
  while (!shutdown_finished) {
    if (got_sigint && !shutdown_started) {
      gpr_log(GPR_INFO, "Shutting down due to SIGINT");
      grpc_server_shutdown(server);
      grpc_completion_queue_shutdown(cq);
      shutdown_started = 1;
    }
    ev = grpc_completion_queue_next(
        cq, gpr_time_add(gpr_now(), gpr_time_from_micros(1000000)));
    if (!ev) continue;
    s = ev->tag;
    switch (ev->type) {
      case GRPC_OP_COMPLETE:
        switch ((gpr_intptr)s) {
          case 101:
            if (call != NULL) {
              if (0 ==
                  strcmp(call_details.method, "/Reflector/reflectStream")) {
                /* Received streaming call. Send metadata here. */
                start_read_op(1);
                send_initial_metadata();
              } else {
                /* Received unary call. Can do all ops in one batch. */
                start_read_op(5);
              }
            } else {
              GPR_ASSERT(shutdown_started);
            }
            /*	    request_call();
             */
            break;
          case 1:
            if (payload_buffer != NULL) {
              /* Received payload from client. */
              start_write_op();
            } else {
              /* Received end of stream from client. */
              start_send_status();
            }
            break;
          case 2:
            /* Write completed at server  */
            start_read_op(1);
            break;
          case 3:
            /* Metadata send completed at server */
            break;
          case 4:
            /* Send status and close completed at server */
            grpc_call_destroy(call);
            request_call();
            break;
          case 5:
            /* Finished payload read for unary. Start all reamaining
             *  unary ops in a batch.
             */
            handle_unary_method();
            break;
          case 6:
            /* Finished unary call. */
            grpc_call_destroy(call);
            request_call();
            break;
        }
        break;
      case GRPC_SERVER_RPC_NEW:
      case GRPC_WRITE_ACCEPTED:
      case GRPC_READ:
      case GRPC_FINISH_ACCEPTED:
      case GRPC_FINISHED:
        gpr_log(GPR_ERROR, "Unexpected event type.");
        GPR_ASSERT(0);
        break;
      case GRPC_QUEUE_SHUTDOWN:
        GPR_ASSERT(shutdown_started);
        shutdown_finished = 1;
        break;
      default:
        GPR_ASSERT(0);
    }
    grpc_event_finish(ev);
  }
  grpc_profiler_stop();

  grpc_server_destroy(server);
  grpc_completion_queue_destroy(cq);
  grpc_shutdown();
  return 0;
}
