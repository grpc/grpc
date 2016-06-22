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

#include "unary_blocking_call.h"
#include "../grpc_c_public.h"
#include "context.h"
#include "call_ops.h"
#include "tag.h"
#include "completion_queue.h"
#include <stdio.h>
#include <grpc/support/log.h>

GRPC_status GRPC_unary_blocking_call(GRPC_channel *channel, const GRPC_method *const rpc_method,
                                     GRPC_context *const context, const GRPC_message message, GRPC_message *response) {
  grpc_completion_queue *cq = GRPC_completion_queue_create();
  grpc_call *call = grpc_channel_create_call(channel,
                                             NULL,
                                             GRPC_PROPAGATE_DEFAULTS,
                                             cq,
                                             rpc_method->name,
                                             "",
                                             context->deadline,
                                             NULL);
  context->call = call;
  grpc_call_op_set set = {
    {
      grpc_op_send_metadata,
      grpc_op_recv_metadata,
      grpc_op_send_object,
      grpc_op_recv_object,
      grpc_op_send_close,
      grpc_op_recv_status
    },
    context,
    .user_tag = TAG(&set)
  };

  size_t nops;
  grpc_op ops[GRPC_MAX_OP_COUNT];
  grpc_fill_op_from_call_set(set, rpc_method, context, message, response, ops, &nops);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call, ops, nops, TAG(&set), NULL));
  for (;;) {
    void *tag;
    bool ok;
    GRPC_completion_queue_next_status status = GRPC_commit_call_and_wait_deadline(cq, context->deadline, &tag, &ok);
    GPR_ASSERT(status == GRPC_COMPLETION_QUEUE_GOT_EVENT);
    GPR_ASSERT(ok);
    if (tag == TAG(&set)) {
      break;
    }
  }

  grpc_finish_op_from_call_set(set, context);
  GPR_ASSERT(context->status.code == GRPC_STATUS_OK);

  GRPC_completion_queue_shutdown_and_destroy(cq);
  grpc_call_destroy(call);
  context->call = NULL;

  return context->status;
}
