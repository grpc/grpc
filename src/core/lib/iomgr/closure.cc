/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/lib/iomgr/closure.h"

#include <assert.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/profiling/timers.h"

#ifndef NDEBUG
grpc_tracer_flag grpc_trace_closure = GRPC_TRACER_INITIALIZER(false, "closure");
#endif

#ifndef NDEBUG
grpc_closure *grpc_closure_init(const char *file, int line,
                                grpc_closure *closure, grpc_iomgr_cb_func cb,
                                void *cb_arg,
                                grpc_closure_scheduler *scheduler) {
#else
grpc_closure *grpc_closure_init(grpc_closure *closure, grpc_iomgr_cb_func cb,
                                void *cb_arg,
                                grpc_closure_scheduler *scheduler) {
#endif
  closure->cb = cb;
  closure->cb_arg = cb_arg;
  closure->scheduler = scheduler;
#ifndef NDEBUG
  closure->scheduled = false;
  closure->file_initiated = NULL;
  closure->line_initiated = 0;
  closure->run = false;
  closure->file_created = file;
  closure->line_created = line;
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
  wrapped_closure *wc = (wrapped_closure *)arg;
  grpc_iomgr_cb_func cb = wc->cb;
  void *cb_arg = wc->cb_arg;
  gpr_free(wc);
  cb(exec_ctx, cb_arg, error);
}

#ifndef NDEBUG
grpc_closure *grpc_closure_create(const char *file, int line,
                                  grpc_iomgr_cb_func cb, void *cb_arg,
                                  grpc_closure_scheduler *scheduler) {
#else
grpc_closure *grpc_closure_create(grpc_iomgr_cb_func cb, void *cb_arg,
                                  grpc_closure_scheduler *scheduler) {
#endif
  wrapped_closure *wc = (wrapped_closure *)gpr_malloc(sizeof(*wc));
  wc->cb = cb;
  wc->cb_arg = cb_arg;
#ifndef NDEBUG
  grpc_closure_init(file, line, &wc->wrapper, closure_wrapper, wc, scheduler);
#else
  grpc_closure_init(&wc->wrapper, closure_wrapper, wc, scheduler);
#endif
  return &wc->wrapper;
}

#ifndef NDEBUG
void grpc_closure_run(const char *file, int line, grpc_exec_ctx *exec_ctx,
                      grpc_closure *c, grpc_error *error) {
#else
void grpc_closure_run(grpc_exec_ctx *exec_ctx, grpc_closure *c,
                      grpc_error *error) {
#endif
  GPR_TIMER_BEGIN("grpc_closure_run", 0);
  if (c != NULL) {
#ifndef NDEBUG
    c->file_initiated = file;
    c->line_initiated = line;
    c->run = true;
#endif
    assert(c->cb);
    c->scheduler->vtable->run(exec_ctx, c, error);
  } else {
    GRPC_ERROR_UNREF(error);
  }
  GPR_TIMER_END("grpc_closure_run", 0);
}

#ifndef NDEBUG
void grpc_closure_sched(const char *file, int line, grpc_exec_ctx *exec_ctx,
                        grpc_closure *c, grpc_error *error) {
#else
void grpc_closure_sched(grpc_exec_ctx *exec_ctx, grpc_closure *c,
                        grpc_error *error) {
#endif
  GPR_TIMER_BEGIN("grpc_closure_sched", 0);
  if (c != NULL) {
#ifndef NDEBUG
    if (c->scheduled) {
      gpr_log(GPR_ERROR,
              "Closure already scheduled. (closure: %p, created: [%s:%d], "
              "previously scheduled at: [%s: %d] run?: %s",
              c, c->file_created, c->line_created, c->file_initiated,
              c->line_initiated, c->run ? "true" : "false");
      abort();
    }
    c->scheduled = true;
    c->file_initiated = file;
    c->line_initiated = line;
    c->run = false;
#endif
    assert(c->cb);
    c->scheduler->vtable->sched(exec_ctx, c, error);
  } else {
    GRPC_ERROR_UNREF(error);
  }
  GPR_TIMER_END("grpc_closure_sched", 0);
}

#ifndef NDEBUG
void grpc_closure_list_sched(const char *file, int line,
                             grpc_exec_ctx *exec_ctx, grpc_closure_list *list) {
#else
void grpc_closure_list_sched(grpc_exec_ctx *exec_ctx, grpc_closure_list *list) {
#endif
  grpc_closure *c = list->head;
  while (c != NULL) {
    grpc_closure *next = c->next_data.next;
#ifndef NDEBUG
    if (c->scheduled) {
      gpr_log(GPR_ERROR,
              "Closure already scheduled. (closure: %p, created: [%s:%d], "
              "previously scheduled at: [%s: %d] run?: %s",
              c, c->file_created, c->line_created, c->file_initiated,
              c->line_initiated, c->run ? "true" : "false");
      abort();
    }
    c->scheduled = true;
    c->file_initiated = file;
    c->line_initiated = line;
    c->run = false;
#endif
    assert(c->cb);
    c->scheduler->vtable->sched(exec_ctx, c, c->error_data.error);
    c = next;
  }
  list->head = list->tail = NULL;
}
