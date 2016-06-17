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

#include "src/core/lib/surface/channel.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/completion_queue.h"

typedef struct {
  grpc_closure closure;
  void *tag;
  grpc_completion_queue *cq;
  grpc_cq_completion completion_storage;
} ping_result;

static void ping_destroy(grpc_exec_ctx *exec_ctx, void *arg,
                         grpc_cq_completion *storage) {
  gpr_free(arg);
}

static void ping_done(grpc_exec_ctx *exec_ctx, void *arg, bool success) {
  ping_result *pr = arg;
  grpc_cq_end_op(exec_ctx, pr->cq, pr->tag, success, ping_destroy, pr,
                 &pr->completion_storage);
}

void grpc_channel_ping(grpc_channel *channel, grpc_completion_queue *cq,
                       void *tag, void *reserved) {
  grpc_transport_op op;
  ping_result *pr = gpr_malloc(sizeof(*pr));
  grpc_channel_element *top_elem =
      grpc_channel_stack_element(grpc_channel_get_channel_stack(channel), 0);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  GPR_ASSERT(reserved == NULL);
  memset(&op, 0, sizeof(op));
  pr->tag = tag;
  pr->cq = cq;
  grpc_closure_init(&pr->closure, ping_done, pr);
  op.send_ping = &pr->closure;
  op.bind_pollset = grpc_cq_pollset(cq);
  grpc_cq_begin_op(cq, tag);
  top_elem->filter->start_transport_op(&exec_ctx, top_elem, &op);
  grpc_exec_ctx_finish(&exec_ctx);
}
