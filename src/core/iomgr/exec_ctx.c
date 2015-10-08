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

#include "src/core/iomgr/exec_ctx.h"

#include <grpc/support/log.h>

#include "src/core/profiling/timers.h"

int grpc_exec_ctx_flush(grpc_exec_ctx *exec_ctx) {
  int did_something = 0;
  GRPC_TIMER_BEGIN(GRPC_PTAG_EXEC_CTX_FLUSH, 0);
  while (!grpc_closure_list_empty(exec_ctx->closure_list)) {
    grpc_closure *c = exec_ctx->closure_list.head;
    exec_ctx->closure_list.head = exec_ctx->closure_list.tail = NULL;
    while (c != NULL) {
      grpc_closure *next = c->next;
      did_something++;
      GRPC_TIMER_BEGIN(GRPC_PTAG_EXEC_CTX_STEP, 0);
      c->cb(exec_ctx, c->cb_arg, c->success);
      GRPC_TIMER_END(GRPC_PTAG_EXEC_CTX_STEP, 0);
      c = next;
    }
  }
  GRPC_TIMER_END(GRPC_PTAG_EXEC_CTX_FLUSH, 0);
  return did_something;
}

void grpc_exec_ctx_finish(grpc_exec_ctx *exec_ctx) {
  grpc_exec_ctx_flush(exec_ctx);
}

void grpc_exec_ctx_enqueue(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                           int success) {
  grpc_closure_list_add(&exec_ctx->closure_list, closure, success);
}

void grpc_exec_ctx_enqueue_list(grpc_exec_ctx *exec_ctx,
                                grpc_closure_list *list) {
  grpc_closure_list_move(list, &exec_ctx->closure_list);
}
