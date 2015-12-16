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

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "src/core/channel/channel_stack.h"
#include "src/core/iomgr/closure.h"
#include "src/core/surface/channel.h"
#include "src/core/transport/transport.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/test_config.h"

grpc_closure transport_op_cb;

static void *tag(gpr_intptr x) { return (void *)x; }

void verify_connectivity(grpc_exec_ctx *exec_ctx, void *arg, int success) {
  grpc_transport_op *op = arg;
  GPR_ASSERT(GRPC_CHANNEL_FATAL_FAILURE == *op->connectivity_state);
  GPR_ASSERT(success);
}

void do_nothing(grpc_exec_ctx *exec_ctx, void *arg, int success) {}

void test_transport_op(grpc_channel *channel) {
  grpc_transport_op op;
  grpc_channel_element *elem;
  grpc_connectivity_state state = GRPC_CHANNEL_IDLE;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  memset(&op, 0, sizeof(op));
  grpc_closure_init(&transport_op_cb, verify_connectivity, &op);

  op.on_connectivity_state_change = &transport_op_cb;
  op.connectivity_state = &state;
  elem = grpc_channel_stack_element(grpc_channel_get_channel_stack(channel), 0);
  elem->filter->start_transport_op(&exec_ctx, elem, &op);
  grpc_exec_ctx_finish(&exec_ctx);

  memset(&op, 0, sizeof(op));
  grpc_closure_init(&transport_op_cb, do_nothing, NULL);
  op.on_consumed = &transport_op_cb;
  elem->filter->start_transport_op(&exec_ctx, elem, &op);
  grpc_exec_ctx_finish(&exec_ctx);
}

int main(int argc, char **argv) {
  grpc_channel *chan;
  grpc_call *call;
  grpc_completion_queue *cq;
  cq_verifier *cqv;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_status_code status;
  grpc_call_error error;
  char *details = NULL;
  size_t details_capacity = 0;
  char *peer;

  grpc_test_init(argc, argv);
  grpc_init();

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);

  chan = grpc_lame_client_channel_create(
      "lampoon:national", GRPC_STATUS_UNKNOWN, "Rpc sent on a lame channel.");
  GPR_ASSERT(chan);

  test_transport_op(chan);

  GPR_ASSERT(GRPC_CHANNEL_FATAL_FAILURE ==
             grpc_channel_check_connectivity_state(chan, 0));

  cq = grpc_completion_queue_create(NULL);

  call = grpc_channel_create_call(chan, NULL, GRPC_PROPAGATE_DEFAULTS, cq,
                                  "/Foo", "anywhere",
                                  GRPC_TIMEOUT_SECONDS_TO_DEADLINE(100), NULL);
  GPR_ASSERT(call);
  cqv = cq_verifier_create(cq);

  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(call, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  /* the call should immediately fail */
  cq_expect_completion(cqv, tag(1), 0);
  cq_verify(cqv);

  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->data.recv_status_on_client.status_details_capacity = &details_capacity;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(call, ops, (size_t)(op - ops), tag(2), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  /* the call should immediately fail */
  cq_expect_completion(cqv, tag(2), 1);
  cq_verify(cqv);

  peer = grpc_call_get_peer(call);
  GPR_ASSERT(strcmp(peer, "lampoon:national") == 0);
  gpr_free(peer);

  grpc_call_destroy(call);
  grpc_channel_destroy(chan);
  cq_verifier_destroy(cqv);
  grpc_completion_queue_destroy(cq);

  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  gpr_free(details);

  grpc_shutdown();

  return 0;
}
