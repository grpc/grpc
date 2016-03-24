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
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/test_config.h"

static void *tag(intptr_t i) { return (void *)i; }

int main(int argc, char **argv) {
  grpc_channel *chan;
  grpc_call *call;
  gpr_timespec deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(2);
  grpc_completion_queue *cq;
  cq_verifier *cqv;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array trailing_metadata_recv;
  grpc_status_code status;
  char *details = NULL;
  size_t details_capacity = 0;

  grpc_test_init(argc, argv);
  grpc_init();

  grpc_metadata_array_init(&trailing_metadata_recv);

  cq = grpc_completion_queue_create(NULL);
  cqv = cq_verifier_create(cq);

  /* reserve two ports */
  int port1 = grpc_pick_unused_port_or_die();
  int port2 = grpc_pick_unused_port_or_die();

  char *addr;

  /* create a channel that picks first amongst the servers */
  gpr_asprintf(&addr, "ipv4:127.0.0.1:%d,127.0.0.1:%d", port1, port2);
  grpc_channel *chan = grpc_insecure_channel_create(addr, NULL, NULL);
  /* and an initial call to them */
  grpc_call *call1 = grpc_channel_create_call(
      chan, NULL, GRPC_PROPAGATE_DEFAULTS, cq, "/foo", "127.0.0.1",
      GRPC_TIMEOUT_SECONDS_TO_DEADLINE(20), NULL);
  /* send initial metadata to probe connectivity */
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call1, ops,
                                                   (size_t)(op - ops),
                                                   tag(0x101), NULL));
  /* and receive status to probe termination */
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv1;
  op->data.recv_status_on_client.status = &status1;
  op->data.recv_status_on_client.status_details = &details1;
  op->data.recv_status_on_client.status_details_capacity = &details_capacity1;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call1, ops,
                                                   (size_t)(op - ops),
                                                   tag(0x102), NULL));

  /* bring a server up on the first port */
  grpc_server *server1 = grpc_server_create(NULL, NULL);
  gpr_asprintf(&addr, "127.0.0.1:%d", port1);
  grpc_server_add_insecure_http2_port(server1, addr);
  gpr_free(addr);
  grpc_server_start(server1);

  /* request a call to the server */
  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(server1, &server_call1,
                                                      &request_metadata1, cq,
                                                      cq, tag(0x301)));

  /* first call should now start */
  cq_expect_completion(cqv, tag(0x101), 1);
  cq_expect_completion(cqv, tag(0x301), 1);
  cq_verify(cqv);

  /* shutdown first server: we should see nothing */
  grpc_server_shutdown_and_notify(server1, cq, tag(0xdead1));
  cq_verify_empty(cqv);

  /* and a new call: should go through to server2 when we start it */
  grpc_call *call2 = grpc_channel_create_call(
      chan, NULL, GRPC_PROPAGATE_DEFAULTS, cq, "/foo", "127.0.0.1",
      GRPC_TIMEOUT_SECONDS_TO_DEADLINE(20), NULL);
  /* send initial metadata to probe connectivity */
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call2, ops,
                                                   (size_t)(op - ops),
                                                   tag(0x201), NULL));
  /* and receive status to probe termination */
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv2;
  op->data.recv_status_on_client.status = &status2;
  op->data.recv_status_on_client.status_details = &details2;
  op->data.recv_status_on_client.status_details_capacity = &details_capacity2;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call1, ops,
                                                   (size_t)(op - ops),
                                                   tag(0x202), NULL));

  /* and bring up second server */
  grpc_server *server2 = grpc_server_create(NULL, NULL);
  gpr_asprintf(&addr, "127.0.0.1:%d", port2);
  grpc_server_add_insecure_http2_port(server2, addr);
  gpr_free(addr);
  grpc_server_start(server2);

  /* request a call to the server */
  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(server1, &server_call1,
                                                      &request_metadata1, cq,
                                                      cq, tag(0x401)));

  /* second call should now start */
  cq_expect_completion(cqv, tag(0x201), 1);
  cq_expect_completion(cqv, tag(0x401), 1);
  cq_verify(cqv);

  /* shutdown second server: we should see nothing */
  grpc_server_shutdown_and_notify(server1, cq, tag(0xdead2));
  cq_verify_empty(cqv);

  /* now everything else should finish */
  cq_expect_completion(cqv, tag(0x102), 1);
  cq_expect_completion(cqv, tag(0x202), 1);
  cq_expect_completion(cqv, tag(0x302), 1);
  cq_expect_completion(cqv, tag(0x402), 1);
  cq_expect_completion(cqv, tag(0xdead1), 1);
  cq_expect_completion(cqv, tag(0xdead2), 1);
  cq_verify(cqv);

  gpr_free(details);
  grpc_metadata_array_destroy(&trailing_metadata_recv);

  grpc_shutdown();

  return 0;
}
