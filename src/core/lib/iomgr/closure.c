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

#include "src/core/lib/iomgr/closure.h"

#include <assert.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/profiling/timers.h"

grpc_closure *grpc_closure_init(grpc_closure *closure, grpc_iomgr_cb_func cb,
                                void *cb_arg,
                                grpc_closure_scheduler *scheduler) {
  closure->cb = cb;
  closure->cb_arg = cb_arg;
  closure->scheduler = scheduler;
#ifndef NDEBUG
  closure->scheduled = false;
#endif
  return closure;
}

void grpc_closure_list_init(grpc_closure_list *closure_list) {
  closure_list->head = closure_list->tail = NULL;
}

bool grpc_closure_list_append(grpc_closure_list *closure_list,
                              grpc_closure *closure, grpc_error *error) {
  if (closure == NULL) {
    GRPC_ERROR_UNREF(error);
    return false;
  }
  closure->error_data.error = error;
  closure->next_data.next = NULL;
  bool was_empty = (closure_list->head == NULL);
  if (was_empty) {
    closure_list->head = closure;
  } else {
    closure_list->tail->next_data.next = closure;
  }
  closure_list->tail = closure;
  return was_empty;
}

void grpc_closure_list_fail_all(grpc_closure_list *list,
                                grpc_error *forced_failure) {
  for (grpc_closure *c = list->head; c != NULL; c = c->next_data.next) {
    if (c->error_data.error == GRPC_ERROR_NONE) {
      c->error_data.error = GRPC_ERROR_REF(forced_failure);
    }
  }
  GRPC_ERROR_UNREF(forced_failure);
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
    dst->tail->next_data.next = src->head;
    dst->tail = src->tail;
  }
  src->head = src->tail = NULL;
}

typedef struct {
  grpc_iomgr_cb_func cb;
  void *cb_arg;
  grpc_closure wrapper;
} wrapped_closure;

static void closure_wrapper(grpc_exec_ctx *exec_ctx, void *arg,
                            grpc_error *error) {
  wrapped_closure *wc = arg;
  grpc_iomgr_cb_func cb = wc->cb;
  void *cb_arg = wc->cb_arg;
  gpr_free(wc);
  cb(exec_ctx, cb_arg, error);
}

grpc_closure *grpc_closure_create(grpc_iomgr_cb_func cb, void *cb_arg,
                                  grpc_closure_scheduler *scheduler) {
  wrapped_closure *wc = gpr_malloc(sizeof(*wc));
  wc->cb = cb;
  wc->cb_arg = cb_arg;
  grpc_closure_init(&wc->wrapper, closure_wrapper, wc, scheduler);
  return &wc->wrapper;
}

void grpc_closure_run(grpc_exec_ctx *exec_ctx, grpc_closure *c,
                      grpc_error *error) {
  GPR_TIMER_BEGIN("grpc_closure_run", 0);
  if (c != NULL) {
    assert(c->cb);
    c->scheduler->vtable->run(exec_ctx, c, error);
  } else {
    GRPC_ERROR_UNREF(error);
  }
  GPR_TIMER_END("grpc_closure_run", 0);
}

void grpc_closure_sched(grpc_exec_ctx *exec_ctx, grpc_closure *c,
                        grpc_error *error) {
  GPR_TIMER_BEGIN("grpc_closure_sched", 0);
  if (c != NULL) {
#ifndef NDEBUG
    GPR_ASSERT(!c->scheduled);
    c->scheduled = true;
#endif
    assert(c->cb);
    c->scheduler->vtable->sched(exec_ctx, c, error);
  } else {
    GRPC_ERROR_UNREF(error);
  }
  GPR_TIMER_END("grpc_closure_sched", 0);
}

void grpc_closure_list_sched(grpc_exec_ctx *exec_ctx, grpc_closure_list *list) {
  grpc_closure *c = list->head;
  while (c != NULL) {
    grpc_closure *next = c->next_data.next;
#ifndef NDEBUG
    GPR_ASSERT(!c->scheduled);
    c->scheduled = true;
#endif
    assert(c->cb);
    c->scheduler->vtable->sched(exec_ctx, c, c->error_data.error);
    c = next;
  }
  list->head = list->tail = NULL;
}
