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
  void *unused;
} call_data;

typedef struct {
  grpc_mdelem *status;
  grpc_mdelem *message;
} channel_data;

static void call_op(grpc_call_element *elem, grpc_call_element *from_elem,
                    grpc_call_op *op) {
  channel_data *channeld = elem->channel_data;
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);

  switch (op->type) {
    case GRPC_SEND_START:
      grpc_call_recv_metadata(elem, grpc_mdelem_ref(channeld->status));
      grpc_call_recv_metadata(elem, grpc_mdelem_ref(channeld->message));
      grpc_call_stream_closed(elem);
      break;
    case GRPC_SEND_METADATA:
      grpc_mdelem_unref(op->data.metadata);
      break;
    default:
      break;
  }

  op->done_cb(op->user_data, GRPC_OP_ERROR);
}

static void channel_op(grpc_channel_element *elem,
                       grpc_channel_element *from_elem, grpc_channel_op *op) {
  switch (op->type) {
    case GRPC_CHANNEL_GOAWAY:
      gpr_slice_unref(op->data.goaway.message);
      break;
    case GRPC_CHANNEL_DISCONNECT:
      grpc_client_channel_closed(elem);
      break;
    default:
      break;
  }
}

static void init_call_elem(grpc_call_element *elem,
                           const void *transport_server_data) {}

static void destroy_call_elem(grpc_call_element *elem) {}

static void init_channel_elem(grpc_channel_element *elem,
                              const grpc_channel_args *args, grpc_mdctx *mdctx,
                              int is_first, int is_last) {
  channel_data *channeld = elem->channel_data;
  char status[12];

  GPR_ASSERT(is_first);
  GPR_ASSERT(is_last);

  channeld->message = grpc_mdelem_from_strings(mdctx, "grpc-message",
                                               "Rpc sent on a lame channel.");
  gpr_ltoa(GRPC_STATUS_UNKNOWN, status);
  channeld->status = grpc_mdelem_from_strings(mdctx, "grpc-status", status);
}

static void destroy_channel_elem(grpc_channel_element *elem) {
  channel_data *channeld = elem->channel_data;

  grpc_mdelem_unref(channeld->message);
  grpc_mdelem_unref(channeld->status);
}

static const grpc_channel_filter lame_filter = {
    call_op,           channel_op,           sizeof(call_data),
    init_call_elem,    destroy_call_elem,    sizeof(channel_data),
    init_channel_elem, destroy_channel_elem, "lame-client", };

grpc_channel *grpc_lame_client_channel_create(void) {
  static const grpc_channel_filter *filters[] = {&lame_filter};
  return grpc_channel_create_from_filters(filters, 1, NULL, grpc_mdctx_create(),
                                          1);
}
