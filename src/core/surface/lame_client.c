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

#include <string.h>

#include "src/core/channel/channel_stack.h"
#include "src/core/support/string.h"
#include "src/core/surface/channel.h"
#include "src/core/surface/call.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

typedef struct {
  grpc_linked_mdelem status;
  grpc_linked_mdelem details;
} call_data;

typedef struct { grpc_mdctx *mdctx; } channel_data;

static void lame_start_transport_stream_op(grpc_call_element *elem,
                                           grpc_transport_stream_op *op) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);
  if (op->send_ops != NULL) {
    grpc_stream_ops_unref_owned_objects(op->send_ops->ops, op->send_ops->nops);
    op->on_done_send->cb(op->on_done_send->cb_arg, 0);
  }
  if (op->recv_ops != NULL) {
    char tmp[GPR_LTOA_MIN_BUFSIZE];
    grpc_metadata_batch mdb;
    gpr_ltoa(GRPC_STATUS_UNKNOWN, tmp);
    calld->status.md =
        grpc_mdelem_from_strings(chand->mdctx, "grpc-status", tmp);
    calld->details.md = grpc_mdelem_from_strings(chand->mdctx, "grpc-message",
                                                 "Rpc sent on a lame channel.");
    calld->status.prev = calld->details.next = NULL;
    calld->status.next = &calld->details;
    calld->details.prev = &calld->status;
    mdb.list.head = &calld->status;
    mdb.list.tail = &calld->details;
    mdb.garbage.head = mdb.garbage.tail = NULL;
    mdb.deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
    grpc_sopb_add_metadata(op->recv_ops, mdb);
    *op->recv_state = GRPC_STREAM_CLOSED;
    op->on_done_recv->cb(op->on_done_recv->cb_arg, 1);
  }
  if (op->on_consumed != NULL) {
    op->on_consumed->cb(op->on_consumed->cb_arg, 0);
  }
}

static void lame_start_transport_op(grpc_channel_element *elem,
                                    grpc_transport_op *op) {
  if (op->on_connectivity_state_change) {
    GPR_ASSERT(*op->connectivity_state != GRPC_CHANNEL_FATAL_FAILURE);
    *op->connectivity_state = GRPC_CHANNEL_FATAL_FAILURE;
    op->on_connectivity_state_change->cb(
        op->on_connectivity_state_change->cb_arg, 1);
  }
  if (op->on_consumed != NULL) {
    op->on_consumed->cb(op->on_consumed->cb_arg, 1);
  }
}

static void init_call_elem(grpc_call_element *elem,
                           const void *transport_server_data,
                           grpc_transport_stream_op *initial_op) {
  if (initial_op) {
    grpc_transport_stream_op_finish_with_failure(initial_op);
  }
}

static void destroy_call_elem(grpc_call_element *elem) {}

static void init_channel_elem(grpc_channel_element *elem, grpc_channel *master,
                              const grpc_channel_args *args, grpc_mdctx *mdctx,
                              int is_first, int is_last) {
  channel_data *chand = elem->channel_data;
  GPR_ASSERT(is_first);
  GPR_ASSERT(is_last);
  chand->mdctx = mdctx;
}

static void destroy_channel_elem(grpc_channel_element *elem) {}

static const grpc_channel_filter lame_filter = {
    lame_start_transport_stream_op,
    lame_start_transport_op,
    sizeof(call_data),
    init_call_elem,
    destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    "lame-client",
};

grpc_channel *grpc_lame_client_channel_create(void) {
  static const grpc_channel_filter *filters[] = {&lame_filter};
  return grpc_channel_create_from_filters(filters, 1, NULL, grpc_mdctx_create(),
                                          1);
}
