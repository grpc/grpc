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
#include <grpc/grpc_http.h>
#include <grpc/grpc_security.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "src/core/support/string.h"
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
static int got_sigint = 0;

typedef struct {
  gpr_refcount pending_ops;
  gpr_intmax bytes_read;
} call_state;

static void request_call(void) {
  call_state *tag = gpr_malloc(sizeof(*tag));
  gpr_ref_init(&tag->pending_ops, 2);
  tag->bytes_read = 0;
  grpc_server_request_call_old(server, tag);
}

static void assert_read_ok(call_state *s, grpc_byte_buffer *b) {
  grpc_byte_buffer_reader *bb_reader = NULL;
  gpr_slice read_slice;
  unsigned i;

  bb_reader = grpc_byte_buffer_reader_create(b);
  while (grpc_byte_buffer_reader_next(bb_reader, &read_slice)) {
    for (i = 0; i < GPR_SLICE_LENGTH(read_slice); i++) {
      GPR_ASSERT(GPR_SLICE_START_PTR(read_slice)[i] == s->bytes_read % 256);
      s->bytes_read++;
    }
    gpr_slice_unref(read_slice);
  }
  grpc_byte_buffer_reader_destroy(bb_reader);
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

#define MAX_ARGS 4
  grpc_arg arge[MAX_ARGS];
  grpc_arg *e;
  grpc_channel_args args = {0, NULL};

  grpc_http_server_page home_page = {"/", "text/html",
                                     "<head>\n"
                                     "<title>Echo Server</title>\n"
                                     "</head>\n"
                                     "<body>\n"
                                     "Welcome to the world of the future!\n"
                                     "</body>\n"};

  GPR_ASSERT(argc >= 1);
  fake_argv[0] = argv[0];
  grpc_test_init(1, fake_argv);

  grpc_init();
  srand(clock());
  memset(arge, 0, sizeof(arge));
  args.args = arge;

  cl = gpr_cmdline_create("echo server");
  gpr_cmdline_add_string(cl, "bind", "Bind host:port", &addr);
  gpr_cmdline_add_flag(cl, "secure", "Run with security?", &secure);
  gpr_cmdline_parse(cl, argc, argv);
  gpr_cmdline_destroy(cl);

  e = &arge[args.num_args++];
  e->type = GRPC_ARG_POINTER;
  e->key = GRPC_ARG_SERVE_OVER_HTTP;
  e->value.pointer.p = &home_page;

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
    server = grpc_server_create(cq, &args);
    GPR_ASSERT(grpc_server_add_secure_http2_port(server, addr, ssl_creds));
    grpc_server_credentials_release(ssl_creds);
  } else {
    server = grpc_server_create(cq, &args);
    GPR_ASSERT(grpc_server_add_http2_port(server, addr));
  }
  grpc_server_start(server);

  gpr_free(addr_buf);
  addr = addr_buf = NULL;

  request_call();

  signal(SIGINT, sigint_handler);
  while (!shutdown_finished) {
    if (got_sigint && !shutdown_started) {
      gpr_log(GPR_INFO, "Shutting down due to SIGINT");
      grpc_server_shutdown(server);
      grpc_completion_queue_shutdown(cq);
      shutdown_started = 1;
    }
    ev = grpc_completion_queue_next(
        cq, gpr_time_add(gpr_now(), gpr_time_from_seconds(1)));
    if (!ev) continue;
    s = ev->tag;
    switch (ev->type) {
      case GRPC_SERVER_RPC_NEW:
        if (ev->call != NULL) {
          /* initial ops are already started in request_call */
          grpc_call_server_accept_old(ev->call, cq, s);
          grpc_call_server_end_initial_metadata_old(ev->call,
                                                    GRPC_WRITE_BUFFER_HINT);
          GPR_ASSERT(grpc_call_start_read_old(ev->call, s) == GRPC_CALL_OK);
          request_call();
        } else {
          GPR_ASSERT(shutdown_started);
          gpr_free(s);
        }
        break;
      case GRPC_WRITE_ACCEPTED:
        GPR_ASSERT(ev->data.write_accepted == GRPC_OP_OK);
        GPR_ASSERT(grpc_call_start_read_old(ev->call, s) == GRPC_CALL_OK);
        break;
      case GRPC_READ:
        if (ev->data.read) {
          assert_read_ok(ev->tag, ev->data.read);
          GPR_ASSERT(grpc_call_start_write_old(ev->call, ev->data.read, s,
                                               GRPC_WRITE_BUFFER_HINT) ==
                     GRPC_CALL_OK);
        } else {
          GPR_ASSERT(grpc_call_start_write_status_old(ev->call, GRPC_STATUS_OK,
                                                      NULL, s) == GRPC_CALL_OK);
        }
        break;
      case GRPC_FINISH_ACCEPTED:
      case GRPC_FINISHED:
        if (gpr_unref(&s->pending_ops)) {
          grpc_call_destroy(ev->call);
          gpr_free(s);
        }
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

  grpc_server_destroy(server);
  grpc_completion_queue_destroy(cq);
  grpc_shutdown();

  return 0;
}
