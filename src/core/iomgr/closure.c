/*
 *
 * Copyright 2015-2016, Google Inc.
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

#include "src/core/iomgr/closure.h"

#include <grpc/support/alloc.h>

void grpc_closure_init(grpc_closure *closure, grpc_iomgr_cb_func cb,
                       void *cb_arg) {
  closure->cb = cb;
  closure->cb_arg = cb_arg;
  closure->final_data = 0;
}

void grpc_closure_list_add(grpc_closure_list *closure_list,
                           grpc_closure *closure, bool success) {
  if (closure == NULL) return;
  closure->final_data = (success != 0);
  if (closure_list->head == NULL) {
    closure_list->head = closure;
  } else {
    closure_list->tail->final_data |= (uintptr_t)closure;
  }
  closure_list->tail = closure;
}

bool grpc_closure_list_empty(grpc_closure_list closure_list) {
  return closure_list.head == NULL;
}

void grpc_closure_list_move(grpc_closure_list *src, grpc_closure_list *dst) {
  if (src->head == NULL) {
    return;
  }
  if (dst->head == NULL) {
    *dst = *src;
  } else {
    dst->tail->final_data |= (uintptr_t)src->head;
    dst->tail = src->tail;
  }
  src->head = src->tail = NULL;
}

typedef struct {
  grpc_iomgr_cb_func cb;
  void *cb_arg;
  grpc_closure wrapper;
} wrapped_closure;

static void closure_wrapper(grpc_exec_ctx *exec_ctx, void *arg, bool success) {
  wrapped_closure *wc = arg;
  grpc_iomgr_cb_func cb = wc->cb;
  void *cb_arg = wc->cb_arg;
  gpr_free(wc);
  cb(exec_ctx, cb_arg, success);
}

grpc_closure *grpc_closure_create(grpc_iomgr_cb_func cb, void *cb_arg) {
  wrapped_closure *wc = gpr_malloc(sizeof(*wc));
  wc->cb = cb;
  wc->cb_arg = cb_arg;
  grpc_closure_init(&wc->wrapper, closure_wrapper, wc);
  return &wc->wrapper;
}

grpc_closure *grpc_closure_next(grpc_closure *closure) {
  return (grpc_closure *)(closure->final_data & ~(uintptr_t)1);
}
