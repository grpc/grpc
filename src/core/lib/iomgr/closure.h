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

#ifndef GRPC_CORE_LIB_IOMGR_CLOSURE_H
#define GRPC_CORE_LIB_IOMGR_CLOSURE_H

#include <grpc/support/port_platform.h>

#include <assert.h>
#include <grpc/impl/codegen/exec_ctx_fwd.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <stdbool.h>
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/support/mpscq.h"

struct grpc_closure;
typedef struct grpc_closure grpc_closure;

extern grpc_core::DebugOnlyTraceFlag grpc_trace_closure;

typedef struct grpc_closure_list {
  grpc_closure* head;
  grpc_closure* tail;
} grpc_closure_list;

/** gRPC Callback definition.
 *
 * \param arg Arbitrary input.
 * \param error GRPC_ERROR_NONE if no error occurred, otherwise some grpc_error
 *              describing what went wrong.
 *              Error contract: it is not the cb's job to unref this error;
 *              the closure scheduler will do that after the cb returns */
typedef void (*grpc_iomgr_cb_func)(grpc_exec_ctx* exec_ctx, void* arg,
                                   grpc_error* error);

typedef struct grpc_closure_scheduler grpc_closure_scheduler;

typedef struct grpc_closure_scheduler_vtable {
  /* NOTE: for all these functions, closure->scheduler == the scheduler that was
           used to find this vtable */
  void (*run)(grpc_exec_ctx* exec_ctx, grpc_closure* closure,
              grpc_error* error);
  void (*sched)(grpc_exec_ctx* exec_ctx, grpc_closure* closure,
                grpc_error* error);
  const char* name;
} grpc_closure_scheduler_vtable;

/** Abstract type that can schedule closures for execution */
struct grpc_closure_scheduler {
  const grpc_closure_scheduler_vtable* vtable;
};

/** A closure over a grpc_iomgr_cb_func. */
struct grpc_closure {
  /** Once queued, next indicates the next queued closure; before then, scratch
   *  space */
  union {
    grpc_closure* next;
    gpr_mpscq_node atm_next;
    uintptr_t scratch;
  } next_data;

  /** Bound callback. */
  grpc_iomgr_cb_func cb;

  /** Arguments to be passed to "cb". */
  void* cb_arg;

  /** Scheduler to schedule against: nullptr to schedule against current
     execution context */
  grpc_closure_scheduler* scheduler;

  /** Once queued, the result of the closure. Before then: scratch space */
  union {
    grpc_error* error;
    uintptr_t scratch;
  } error_data;

// extra tracing and debugging for grpc_closure. This incurs a decent amount of
// overhead per closure, so it must be enabled at compile time.
#ifndef NDEBUG
  bool scheduled;
  bool run;  // true = run, false = scheduled
  const char* file_created;
  int line_created;
  const char* file_initiated;
  int line_initiated;
#endif
};

#ifndef NDEBUG
inline grpc_closure* grpc_closure_init(const char* file, int line,
                                       grpc_closure* closure,
                                       grpc_iomgr_cb_func cb, void* cb_arg,
                                       grpc_closure_scheduler* scheduler) {
#else
inline grpc_closure* grpc_closure_init(grpc_closure* closure,
                                       grpc_iomgr_cb_func cb, void* cb_arg,
                                       grpc_closure_scheduler* scheduler) {
#endif
  closure->cb = cb;
  closure->cb_arg = cb_arg;
  closure->scheduler = scheduler;
#ifndef NDEBUG
  closure->scheduled = false;
  closure->file_initiated = nullptr;
  closure->line_initiated = 0;
  closure->run = false;
  closure->file_created = file;
  closure->line_created = line;
#endif
  return closure;
}

/** Initializes \a closure with \a cb and \a cb_arg. Returns \a closure. */
#ifndef NDEBUG
#define GRPC_CLOSURE_INIT(closure, cb, cb_arg, scheduler) \
  grpc_closure_init(__FILE__, __LINE__, closure, cb, cb_arg, scheduler)
#else
#define GRPC_CLOSURE_INIT(closure, cb, cb_arg, scheduler) \
  grpc_closure_init(closure, cb, cb_arg, scheduler)
#endif

namespace closure_impl {

typedef struct {
  grpc_iomgr_cb_func cb;
  void* cb_arg;
  grpc_closure wrapper;
} wrapped_closure;

inline void closure_wrapper(grpc_exec_ctx* exec_ctx, void* arg,
                            grpc_error* error) {
  wrapped_closure* wc = (wrapped_closure*)arg;
  grpc_iomgr_cb_func cb = wc->cb;
  void* cb_arg = wc->cb_arg;
  gpr_free(wc);
  cb(exec_ctx, cb_arg, error);
}

}  // namespace closure_impl

#ifndef NDEBUG
inline grpc_closure* grpc_closure_create(const char* file, int line,
                                         grpc_iomgr_cb_func cb, void* cb_arg,
                                         grpc_closure_scheduler* scheduler) {
#else
inline grpc_closure* grpc_closure_create(grpc_iomgr_cb_func cb, void* cb_arg,
                                         grpc_closure_scheduler* scheduler) {
#endif
  closure_impl::wrapped_closure* wc =
      (closure_impl::wrapped_closure*)gpr_malloc(sizeof(*wc));
  wc->cb = cb;
  wc->cb_arg = cb_arg;
#ifndef NDEBUG
  grpc_closure_init(file, line, &wc->wrapper, closure_impl::closure_wrapper, wc,
                    scheduler);
#else
  grpc_closure_init(&wc->wrapper, closure_impl::closure_wrapper, wc, scheduler);
#endif
  return &wc->wrapper;
}

/* Create a heap allocated closure: try to avoid except for very rare events */
#ifndef NDEBUG
#define GRPC_CLOSURE_CREATE(cb, cb_arg, scheduler) \
  grpc_closure_create(__FILE__, __LINE__, cb, cb_arg, scheduler)
#else
#define GRPC_CLOSURE_CREATE(cb, cb_arg, scheduler) \
  grpc_closure_create(cb, cb_arg, scheduler)
#endif

#define GRPC_CLOSURE_LIST_INIT \
  { nullptr, nullptr }

inline void grpc_closure_list_init(grpc_closure_list* closure_list) {
  closure_list->head = closure_list->tail = nullptr;
}

/** add \a closure to the end of \a list
    and set \a closure's result to \a error
    Returns true if \a list becomes non-empty */
inline bool grpc_closure_list_append(grpc_closure_list* closure_list,
                                     grpc_closure* closure, grpc_error* error) {
  if (closure == nullptr) {
    GRPC_ERROR_UNREF(error);
    return false;
  }
  closure->error_data.error = error;
  closure->next_data.next = nullptr;
  bool was_empty = (closure_list->head == nullptr);
  if (was_empty) {
    closure_list->head = closure;
  } else {
    closure_list->tail->next_data.next = closure;
  }
  closure_list->tail = closure;
  return was_empty;
}

/** force all success bits in \a list to false */
inline void grpc_closure_list_fail_all(grpc_closure_list* list,
                                       grpc_error* forced_failure) {
  for (grpc_closure* c = list->head; c != nullptr; c = c->next_data.next) {
    if (c->error_data.error == GRPC_ERROR_NONE) {
      c->error_data.error = GRPC_ERROR_REF(forced_failure);
    }
  }
  GRPC_ERROR_UNREF(forced_failure);
}

/** append all closures from \a src to \a dst and empty \a src. */
inline void grpc_closure_list_move(grpc_closure_list* src,
                                   grpc_closure_list* dst) {
  if (src->head == nullptr) {
    return;
  }
  if (dst->head == nullptr) {
    *dst = *src;
  } else {
    dst->tail->next_data.next = src->head;
    dst->tail = src->tail;
  }
  src->head = src->tail = nullptr;
}

/** return whether \a list is empty. */
inline bool grpc_closure_list_empty(grpc_closure_list closure_list) {
  return closure_list.head == nullptr;
}

#ifndef NDEBUG
inline void grpc_closure_run(const char* file, int line,
                             grpc_exec_ctx* exec_ctx, grpc_closure* c,
                             grpc_error* error) {
#else
inline void grpc_closure_run(grpc_exec_ctx* exec_ctx, grpc_closure* c,
                             grpc_error* error) {
#endif
  GPR_TIMER_BEGIN("grpc_closure_run", 0);
  if (c != nullptr) {
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

/** Run a closure directly. Caller ensures that no locks are being held above.
 *  Note that calling this at the end of a closure callback function itself is
 *  by definition safe. */
#ifndef NDEBUG
#define GRPC_CLOSURE_RUN(exec_ctx, closure, error) \
  grpc_closure_run(__FILE__, __LINE__, exec_ctx, closure, error)
#else
#define GRPC_CLOSURE_RUN(exec_ctx, closure, error) \
  grpc_closure_run(exec_ctx, closure, error)
#endif

#ifndef NDEBUG
inline void grpc_closure_sched(const char* file, int line,
                               grpc_exec_ctx* exec_ctx, grpc_closure* c,
                               grpc_error* error) {
#else
inline void grpc_closure_sched(grpc_exec_ctx* exec_ctx, grpc_closure* c,
                               grpc_error* error) {
#endif
  GPR_TIMER_BEGIN("grpc_closure_sched", 0);
  if (c != nullptr) {
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

/** Schedule a closure to be run. Does not need to be run from a safe point. */
#ifndef NDEBUG
#define GRPC_CLOSURE_SCHED(exec_ctx, closure, error) \
  grpc_closure_sched(__FILE__, __LINE__, exec_ctx, closure, error)
#else
#define GRPC_CLOSURE_SCHED(exec_ctx, closure, error) \
  grpc_closure_sched(exec_ctx, closure, error)
#endif

#ifndef NDEBUG
inline void grpc_closure_list_sched(const char* file, int line,
                                    grpc_exec_ctx* exec_ctx,
                                    grpc_closure_list* list) {
#else
inline void grpc_closure_list_sched(grpc_exec_ctx* exec_ctx,
                                    grpc_closure_list* list) {
#endif
  grpc_closure* c = list->head;
  while (c != nullptr) {
    grpc_closure* next = c->next_data.next;
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
  list->head = list->tail = nullptr;
}

/** Schedule all closures in a list to be run. Does not need to be run from a
 * safe point. */
#ifndef NDEBUG
#define GRPC_CLOSURE_LIST_SCHED(exec_ctx, closure_list) \
  grpc_closure_list_sched(__FILE__, __LINE__, exec_ctx, closure_list)
#else
#define GRPC_CLOSURE_LIST_SCHED(exec_ctx, closure_list) \
  grpc_closure_list_sched(exec_ctx, closure_list)
#endif

#endif /* GRPC_CORE_LIB_IOMGR_CLOSURE_H */
