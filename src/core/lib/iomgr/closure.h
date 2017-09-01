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

#include <grpc/impl/codegen/exec_ctx_fwd.h>
#include <stdbool.h>
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/support/mpscq.h"

#ifdef __cplusplus
extern "C" {
#endif

struct grpc_closure;
typedef struct grpc_closure grpc_closure;

#ifndef NDEBUG
extern grpc_tracer_flag grpc_trace_closure;
#endif

typedef struct grpc_closure_list {
  grpc_closure *head;
  grpc_closure *tail;
} grpc_closure_list;

/** gRPC Callback definition.
 *
 * \param arg Arbitrary input.
 * \param error GRPC_ERROR_NONE if no error occurred, otherwise some grpc_error
 *              describing what went wrong.
 *              Error contract: it is not the cb's job to unref this error;
 *              the closure scheduler will do that after the cb returns */
typedef void (*grpc_iomgr_cb_func)(grpc_exec_ctx *exec_ctx, void *arg,
                                   grpc_error *error);

typedef struct grpc_closure_scheduler grpc_closure_scheduler;

typedef struct grpc_closure_scheduler_vtable {
  /* NOTE: for all these functions, closure->scheduler == the scheduler that was
           used to find this vtable */
  void (*run)(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
              grpc_error *error);
  void (*sched)(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                grpc_error *error);
  const char *name;
} grpc_closure_scheduler_vtable;

/** Abstract type that can schedule closures for execution */
struct grpc_closure_scheduler {
  const grpc_closure_scheduler_vtable *vtable;
};

/** A closure over a grpc_iomgr_cb_func. */
struct grpc_closure {
  /** Once queued, next indicates the next queued closure; before then, scratch
   *  space */
  union {
    grpc_closure *next;
    gpr_mpscq_node atm_next;
    uintptr_t scratch;
  } next_data;

  /** Bound callback. */
  grpc_iomgr_cb_func cb;

  /** Arguments to be passed to "cb". */
  void *cb_arg;

  /** Scheduler to schedule against: NULL to schedule against current execution
      context */
  grpc_closure_scheduler *scheduler;

  /** Once queued, the result of the closure. Before then: scratch space */
  union {
    grpc_error *error;
    uintptr_t scratch;
  } error_data;

// extra tracing and debugging for grpc_closure. This incurs a decent amount of
// overhead per closure, so it must be enabled at compile time.
#ifndef NDEBUG
  bool scheduled;
  bool run;  // true = run, false = scheduled
  const char *file_created;
  int line_created;
  const char *file_initiated;
  int line_initiated;
#endif
};

/** Initializes \a closure with \a cb and \a cb_arg. Returns \a closure. */
#ifndef NDEBUG
grpc_closure *grpc_closure_init(const char *file, int line,
                                grpc_closure *closure, grpc_iomgr_cb_func cb,
                                void *cb_arg,
                                grpc_closure_scheduler *scheduler);
#define GRPC_CLOSURE_INIT(closure, cb, cb_arg, scheduler) \
  grpc_closure_init(__FILE__, __LINE__, closure, cb, cb_arg, scheduler)
#else
grpc_closure *grpc_closure_init(grpc_closure *closure, grpc_iomgr_cb_func cb,
                                void *cb_arg,
                                grpc_closure_scheduler *scheduler);
#define GRPC_CLOSURE_INIT(closure, cb, cb_arg, scheduler) \
  grpc_closure_init(closure, cb, cb_arg, scheduler)
#endif

/* Create a heap allocated closure: try to avoid except for very rare events */
#ifndef NDEBUG
grpc_closure *grpc_closure_create(const char *file, int line,
                                  grpc_iomgr_cb_func cb, void *cb_arg,
                                  grpc_closure_scheduler *scheduler);
#define GRPC_CLOSURE_CREATE(cb, cb_arg, scheduler) \
  grpc_closure_create(__FILE__, __LINE__, cb, cb_arg, scheduler)
#else
grpc_closure *grpc_closure_create(grpc_iomgr_cb_func cb, void *cb_arg,
                                  grpc_closure_scheduler *scheduler);
#define GRPC_CLOSURE_CREATE(cb, cb_arg, scheduler) \
  grpc_closure_create(cb, cb_arg, scheduler)
#endif

#define GRPC_CLOSURE_LIST_INIT \
  { NULL, NULL }

void grpc_closure_list_init(grpc_closure_list *list);

/** add \a closure to the end of \a list
    and set \a closure's result to \a error
    Returns true if \a list becomes non-empty */
bool grpc_closure_list_append(grpc_closure_list *list, grpc_closure *closure,
                              grpc_error *error);

/** force all success bits in \a list to false */
void grpc_closure_list_fail_all(grpc_closure_list *list,
                                grpc_error *forced_failure);

/** append all closures from \a src to \a dst and empty \a src. */
void grpc_closure_list_move(grpc_closure_list *src, grpc_closure_list *dst);

/** return whether \a list is empty. */
bool grpc_closure_list_empty(grpc_closure_list list);

/** Run a closure directly. Caller ensures that no locks are being held above.
 *  Note that calling this at the end of a closure callback function itself is
 *  by definition safe. */
#ifndef NDEBUG
void grpc_closure_run(const char *file, int line, grpc_exec_ctx *exec_ctx,
                      grpc_closure *closure, grpc_error *error);
#define GRPC_CLOSURE_RUN(exec_ctx, closure, error) \
  grpc_closure_run(__FILE__, __LINE__, exec_ctx, closure, error)
#else
void grpc_closure_run(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                      grpc_error *error);
#define GRPC_CLOSURE_RUN(exec_ctx, closure, error) \
  grpc_closure_run(exec_ctx, closure, error)
#endif

/** Schedule a closure to be run. Does not need to be run from a safe point. */
#ifndef NDEBUG
void grpc_closure_sched(const char *file, int line, grpc_exec_ctx *exec_ctx,
                        grpc_closure *closure, grpc_error *error);
#define GRPC_CLOSURE_SCHED(exec_ctx, closure, error) \
  grpc_closure_sched(__FILE__, __LINE__, exec_ctx, closure, error)
#else
void grpc_closure_sched(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                        grpc_error *error);
#define GRPC_CLOSURE_SCHED(exec_ctx, closure, error) \
  grpc_closure_sched(exec_ctx, closure, error)
#endif

/** Schedule all closures in a list to be run. Does not need to be run from a
 * safe point. */
#ifndef NDEBUG
void grpc_closure_list_sched(const char *file, int line,
                             grpc_exec_ctx *exec_ctx,
                             grpc_closure_list *closure_list);
#define GRPC_CLOSURE_LIST_SCHED(exec_ctx, closure_list) \
  grpc_closure_list_sched(__FILE__, __LINE__, exec_ctx, closure_list)
#else
void grpc_closure_list_sched(grpc_exec_ctx *exec_ctx,
                             grpc_closure_list *closure_list);
#define GRPC_CLOSURE_LIST_SCHED(exec_ctx, closure_list) \
  grpc_closure_list_sched(exec_ctx, closure_list)
#endif

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_IOMGR_CLOSURE_H */
