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

#include "src/core/surface/client.h"

#include "src/core/surface/call.h"
#include "src/core/surface/channel.h"
#include "src/core/support/string.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

typedef struct {
  void *unused;
} call_data;

typedef struct {
  void *unused;
} channel_data;

static void call_op(grpc_call_element *elem, grpc_call_element *from_elem,
                    grpc_call_op *op) {
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);

  switch (op->type) {
    case GRPC_SEND_DEADLINE:
      grpc_call_set_deadline(elem, op->data.deadline);
      grpc_call_next_op(elem, op);
      break;
    case GRPC_RECV_METADATA:
      grpc_call_recv_metadata(elem, op->data.metadata);
      break;
    case GRPC_RECV_DEADLINE:
      gpr_log(GPR_ERROR, "Deadline received by client (ignored)");
      break;
    case GRPC_RECV_MESSAGE:
      grpc_call_recv_message(elem, op->data.message);
      op->done_cb(op->user_data, GRPC_OP_OK);
      break;
    case GRPC_RECV_HALF_CLOSE:
      grpc_call_read_closed(elem);
      break;
    case GRPC_RECV_FINISH:
      grpc_call_stream_closed(elem);
      break;
    case GRPC_RECV_END_OF_INITIAL_METADATA:
      grpc_call_initial_metadata_complete(elem);
      break;
    default:
      GPR_ASSERT(op->dir == GRPC_CALL_DOWN);
      grpc_call_next_op(elem, op);
  }
}

static void channel_op(grpc_channel_element *elem,
                       grpc_channel_element *from_elem, grpc_channel_op *op) {
  switch (op->type) {
    case GRPC_ACCEPT_CALL:
      gpr_log(GPR_ERROR, "Client cannot accept new calls");
      break;
    case GRPC_TRANSPORT_CLOSED:
      grpc_client_channel_closed(elem);
      break;
    case GRPC_TRANSPORT_GOAWAY:
      gpr_slice_unref(op->data.goaway.message);
      break;
    default:
      GPR_ASSERT(op->dir == GRPC_CALL_DOWN);
      grpc_channel_next_op(elem, op);
  }
}

static void init_call_elem(grpc_call_element *elem,
                           const void *transport_server_data) {}

static void destroy_call_elem(grpc_call_element *elem) {}

static void init_channel_elem(grpc_channel_element *elem,
                              const grpc_channel_args *args, grpc_mdctx *mdctx,
                              int is_first, int is_last) {
  GPR_ASSERT(is_first);
  GPR_ASSERT(!is_last);
}

static void destroy_channel_elem(grpc_channel_element *elem) {}

const grpc_channel_filter grpc_client_surface_filter = {
    call_op,           channel_op,           sizeof(call_data),
    init_call_elem,    destroy_call_elem,    sizeof(channel_data),
    init_channel_elem, destroy_channel_elem, "client", };
