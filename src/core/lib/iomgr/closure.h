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

#ifndef GRPC_CORE_LIB_IOMGR_CLOSURE_H
#define GRPC_CORE_LIB_IOMGR_CLOSURE_H

#include <grpc/support/port_platform.h>

#include <grpc/impl/codegen/exec_ctx_fwd.h>
#include <stdbool.h>
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/support/mpscq.h"

struct grpc_closure;
typedef struct grpc_closure grpc_closure;

typedef struct grpc_closure_list {
  grpc_closure *head;
  grpc_closure *tail;
} grpc_closure_list;

/** gRPC Callback definition.
 *
 * \param arg Arbitrary input.
 * \param error GRPC_ERROR_NONE if no error occurred, otherwise some grpc_error
 *              describing what went wrong */
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

#ifndef NDEBUG
  bool scheduled;
#endif
};

/** Initializes \a closure with \a cb and \a cb_arg. Returns \a closure. */
grpc_closure *grpc_closure_init(grpc_closure *closure, grpc_iomgr_cb_func cb,
                                void *cb_arg,
                                grpc_closure_scheduler *scheduler);

/* Create a heap allocated closure: try to avoid except for very rare events */
grpc_closure *grpc_closure_create(grpc_iomgr_cb_func cb, void *cb_arg,
                                  grpc_closure_scheduler *scheduler);

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
void grpc_closure_run(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                      grpc_error *error);

/** Schedule a closure to be run. Does not need to be run from a safe point. */
void grpc_closure_sched(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                        grpc_error *error);

/** Schedule all closures in a list to be run. Does not need to be run from a
 * safe point. */
void grpc_closure_list_sched(grpc_exec_ctx *exec_ctx,
                             grpc_closure_list *closure_list);

#endif /* GRPC_CORE_LIB_IOMGR_CLOSURE_H */
