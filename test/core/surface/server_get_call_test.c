/*
 *
 * Copyright 2017, Google Inc.
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
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "test/core/util/port.h"
#include "src/core/lib/support/string.h"
#include "test/core/end2end/cq_verifier.h"

static void *tag(intptr_t t) { return (void *)t; }

static gpr_timespec n_seconds_from_now(int n) {
  return grpc_timeout_seconds_to_deadline(n);
}

static gpr_timespec five_seconds_from_now(void) {
  return n_seconds_from_now(5);
}

static void drain_cq(grpc_completion_queue *cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, five_seconds_from_now(), NULL);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

static void send_partial_request(grpc_channel *channel) {
  grpc_completion_queue *cq = grpc_completion_queue_create_for_next(NULL);
  cq_verifier *cqv = cq_verifier_create(cq);
  gpr_timespec deadline = five_seconds_from_now();
  grpc_call *c = grpc_channel_create_call(
      channel, NULL, GRPC_PROPAGATE_DEFAULTS, cq,
      grpc_slice_from_static_string("/foo"), NULL, deadline, NULL);
  GPR_ASSERT(c != NULL);
  // Send initial metadata and wait for it to complete.
  grpc_op ops[2];
  memset(ops, 0, sizeof(ops));
  grpc_op *op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  ++op;
  grpc_call_error error =
      grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(1), true);
  cq_verify(cqv);
  // Sleep for a few seconds.
  gpr_sleep_until(n_seconds_from_now(3));
  // Now cancel the call.
  error = grpc_call_cancel(c, NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  // Clean up.
  grpc_call_unref(c);
  cq_verifier_destroy(cqv);
  grpc_completion_queue_shutdown(cq);
  drain_cq(cq);
  grpc_completion_queue_destroy(cq);
}

typedef struct {
  grpc_server *server;
  void *registered_method;
  grpc_completion_queue *cq;
} server_args;

static void server_thread(void *arg) {
  server_args *args = arg;
  // Request call.
  gpr_timespec deadline;
  grpc_metadata_array request_metadata_recv;
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_byte_buffer *request = NULL;
  grpc_call *s;
  grpc_call_error error = grpc_server_request_registered_call(
      args->server, args->registered_method, &s, &deadline,
      &request_metadata_recv, &request, args->cq, args->cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  cq_verifier *cqv = cq_verifier_create(args->cq);
  // Success should always be false here, because the completion queue
  // should only return the tag when it shuts down.
  CQ_EXPECT_COMPLETION(cqv, tag(101), false);
  cq_verify(cqv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  cq_verifier_destroy(cqv);
}

void server_get_call_test() {
  // Pick server port.
  int port = grpc_pick_unused_port_or_die();
  char *localaddr;
  gpr_join_host_port(&localaddr, "localhost", port);
  // Create and start server.
  grpc_server *server = grpc_server_create(NULL, NULL);
  grpc_completion_queue *cq = grpc_completion_queue_create_for_next(NULL);
  grpc_server_register_completion_queue(server, cq, NULL);
  GPR_ASSERT(grpc_server_add_insecure_http2_port(server, localaddr));
  void *registered_method = grpc_server_register_method(
      server, "/foo", "foo.test.google.fr:1234",
      GRPC_SRM_PAYLOAD_READ_INITIAL_BYTE_BUFFER, 0 /* flags */);
  GPR_ASSERT(registered_method != NULL);
  grpc_server_start(server);
  // Spawn server thread.
  gpr_thd_id server_thread_id;
  gpr_thd_options thdopt = gpr_thd_options_default();
  gpr_thd_options_set_joinable(&thdopt);
  server_args args = { server, registered_method, cq };
  GPR_ASSERT(gpr_thd_new(&server_thread_id, server_thread, &args, &thdopt));
  // Send partial request from client.
  grpc_channel *channel = grpc_insecure_channel_create(localaddr, NULL, NULL);
  gpr_free(localaddr);
  send_partial_request(channel);
  grpc_channel_destroy(channel);
  // Shut down server.
  grpc_completion_queue *shutdown_cq =
      grpc_completion_queue_create_for_pluck(NULL);
  grpc_server_shutdown_and_notify(server, shutdown_cq, tag(1000));
  GPR_ASSERT(grpc_completion_queue_pluck(shutdown_cq, tag(1000),
                                         grpc_timeout_seconds_to_deadline(5),
                                         NULL)
                 .type == GRPC_OP_COMPLETE);
  grpc_server_destroy(server);
  gpr_thd_join(server_thread_id);
}

int main(int argc, char* argv[]) {
  grpc_test_init(argc, argv);
  grpc_init();
  server_get_call_test();
  grpc_shutdown();
}
