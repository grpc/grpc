/*
 *
 * Copyright 2016, Google Inc.
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

#include "src/core/lib/iomgr/combiner.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/workqueue.h"
#include "src/core/lib/profiling/timers.h"

int grpc_combiner_trace = 0;

#define GRPC_COMBINER_TRACE(fn) \
  do {                          \
    if (grpc_combiner_trace) {  \
      fn;                       \
    }                           \
  } while (0)

#define STATE_UNORPHANED 1
#define STATE_ELEM_COUNT_LOW_BIT 2

struct grpc_combiner {
  grpc_combiner *next_combiner_on_this_exec_ctx;
  grpc_workqueue *optional_workqueue;
  grpc_closure_scheduler uncovered_scheduler;
  grpc_closure_scheduler covered_scheduler;
  grpc_closure_scheduler uncovered_finally_scheduler;
  grpc_closure_scheduler covered_finally_scheduler;
  gpr_mpscq queue;
  // state is:
  // lower bit - zero if orphaned (STATE_UNORPHANED)
  // other bits - number of items queued on the lock (STATE_ELEM_COUNT_LOW_BIT)
  gpr_atm state;
  // number of elements in the list that are covered by a poller: if >0, we can
  // offload safely
  gpr_atm elements_covered_by_poller;
  bool time_to_execute_final_list;
  bool final_list_covered_by_poller;
  grpc_closure_list final_list;
  grpc_closure offload;
};

static void combiner_exec_uncovered(grpc_exec_ctx *exec_ctx,
                                    grpc_closure *closure, grpc_error *error);
static void combiner_exec_covered(grpc_exec_ctx *exec_ctx,
                                  grpc_closure *closure, grpc_error *error);
static void combiner_finally_exec_uncovered(grpc_exec_ctx *exec_ctx,
                                            grpc_closure *closure,
                                            grpc_error *error);
static void combiner_finally_exec_covered(grpc_exec_ctx *exec_ctx,
                                          grpc_closure *closure,
                                          grpc_error *error);

static const grpc_closure_scheduler_vtable scheduler_uncovered = {
    combiner_exec_uncovered, combiner_exec_uncovered};
static const grpc_closure_scheduler_vtable scheduler_covered = {
    combiner_exec_covered, combiner_exec_covered};
static const grpc_closure_scheduler_vtable finally_scheduler_uncovered = {
    combiner_finally_exec_uncovered, combiner_finally_exec_uncovered};
static const grpc_closure_scheduler_vtable finally_scheduler_covered = {
    combiner_finally_exec_covered, combiner_finally_exec_covered};

static void offload(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error);

typedef struct {
  grpc_error *error;
  bool covered_by_poller;
} error_data;

static uintptr_t pack_error_data(error_data d) {
  return ((uintptr_t)d.error) | (d.covered_by_poller ? 1 : 0);
}

static error_data unpack_error_data(uintptr_t p) {
  return (error_data){(grpc_error *)(p & ~(uintptr_t)1), p & 1};
}

static bool is_covered_by_poller(grpc_combiner *lock) {
  return lock->final_list_covered_by_poller ||
         gpr_atm_acq_load(&lock->elements_covered_by_poller) > 0;
}

#define IS_COVERED_BY_POLLER_FMT "(final=%d elems=%" PRIdPTR ")->%d"
#define IS_COVERED_BY_POLLER_ARGS(lock)                      \
  (lock)->final_list_covered_by_poller,                      \
      gpr_atm_acq_load(&(lock)->elements_covered_by_poller), \
      is_covered_by_poller((lock))

grpc_combiner *grpc_combiner_create(grpc_workqueue *optional_workqueue) {
  grpc_combiner *lock = gpr_malloc(sizeof(*lock));
  lock->next_combiner_on_this_exec_ctx = NULL;
  lock->time_to_execute_final_list = false;
  lock->optional_workqueue = optional_workqueue;
  lock->final_list_covered_by_poller = false;
  lock->uncovered_scheduler.vtable = &scheduler_uncovered;
  lock->covered_scheduler.vtable = &scheduler_covered;
  lock->uncovered_finally_scheduler.vtable = &finally_scheduler_uncovered;
  lock->covered_finally_scheduler.vtable = &finally_scheduler_covered;
  gpr_atm_no_barrier_store(&lock->state, STATE_UNORPHANED);
  gpr_atm_no_barrier_store(&lock->elements_covered_by_poller, 0);
  gpr_mpscq_init(&lock->queue);
  grpc_closure_list_init(&lock->final_list);
  grpc_closure_init(&lock->offload, offload, lock,
                    grpc_workqueue_scheduler(lock->optional_workqueue));
  GRPC_COMBINER_TRACE(gpr_log(GPR_DEBUG, "C:%p create", lock));
  return lock;
}

static void really_destroy(grpc_exec_ctx *exec_ctx, grpc_combiner *lock) {
  GRPC_COMBINER_TRACE(gpr_log(GPR_DEBUG, "C:%p really_destroy", lock));
  GPR_ASSERT(gpr_atm_no_barrier_load(&lock->state) == 0);
  gpr_mpscq_destroy(&lock->queue);
  GRPC_WORKQUEUE_UNREF(exec_ctx, lock->optional_workqueue, "combiner");
  gpr_free(lock);
}

void grpc_combiner_destroy(grpc_exec_ctx *exec_ctx, grpc_combiner *lock) {
  gpr_atm old_state = gpr_atm_full_fetch_add(&lock->state, -STATE_UNORPHANED);
  GRPC_COMBINER_TRACE(gpr_log(
      GPR_DEBUG, "C:%p really_destroy old_state=%" PRIdPTR, lock, old_state));
  if (old_state == 1) {
    really_destroy(exec_ctx, lock);
  }
}

static void push_last_on_exec_ctx(grpc_exec_ctx *exec_ctx,
                                  grpc_combiner *lock) {
  lock->next_combiner_on_this_exec_ctx = NULL;
  if (exec_ctx->active_combiner == NULL) {
    exec_ctx->active_combiner = exec_ctx->last_combiner = lock;
  } else {
    exec_ctx->last_combiner->next_combiner_on_this_exec_ctx = lock;
    exec_ctx->last_combiner = lock;
  }
}

static void push_first_on_exec_ctx(grpc_exec_ctx *exec_ctx,
                                   grpc_combiner *lock) {
  lock->next_combiner_on_this_exec_ctx = exec_ctx->active_combiner;
  exec_ctx->active_combiner = lock;
  if (lock->next_combiner_on_this_exec_ctx == NULL) {
    exec_ctx->last_combiner = lock;
  }
}

static void combiner_exec(grpc_exec_ctx *exec_ctx, grpc_combiner *lock,
                          grpc_closure *cl, grpc_error *error,
                          bool covered_by_poller) {
  GPR_TIMER_BEGIN("combiner.execute", 0);
  gpr_atm last = gpr_atm_full_fetch_add(&lock->state, STATE_ELEM_COUNT_LOW_BIT);
  GRPC_COMBINER_TRACE(gpr_log(
      GPR_DEBUG, "C:%p grpc_combiner_execute c=%p cov=%d last=%" PRIdPTR, lock,
      cl, covered_by_poller, last));
  GPR_ASSERT(last & STATE_UNORPHANED);  // ensure lock has not been destroyed
  cl->error_data.scratch =
      pack_error_data((error_data){error, covered_by_poller});
  if (covered_by_poller) {
    gpr_atm_no_barrier_fetch_add(&lock->elements_covered_by_poller, 1);
  }
  gpr_mpscq_push(&lock->queue, &cl->next_data.atm_next);
  if (last == 1) {
    // first element on this list: add it to the list of combiner locks
    // executing within this exec_ctx
    push_last_on_exec_ctx(exec_ctx, lock);
  }
  GPR_TIMER_END("combiner.execute", 0);
}

#define COMBINER_FROM_CLOSURE_SCHEDULER(closure, scheduler_name) \
  ((grpc_combiner *)(((char *)((closure)->scheduler)) -          \
                     offsetof(grpc_combiner, scheduler_name)))

static void combiner_exec_uncovered(grpc_exec_ctx *exec_ctx, grpc_closure *cl,
                                    grpc_error *error) {
  combiner_exec(exec_ctx,
                COMBINER_FROM_CLOSURE_SCHEDULER(cl, uncovered_scheduler), cl,
                error, false);
}

static void combiner_exec_covered(grpc_exec_ctx *exec_ctx, grpc_closure *cl,
                                  grpc_error *error) {
  combiner_exec(exec_ctx,
                COMBINER_FROM_CLOSURE_SCHEDULER(cl, covered_scheduler), cl,
                error, true);
}

static void move_next(grpc_exec_ctx *exec_ctx) {
  exec_ctx->active_combiner =
      exec_ctx->active_combiner->next_combiner_on_this_exec_ctx;
  if (exec_ctx->active_combiner == NULL) {
    exec_ctx->last_combiner = NULL;
  }
}

static void offload(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  grpc_combiner *lock = arg;
  push_last_on_exec_ctx(exec_ctx, lock);
}

static void queue_offload(grpc_exec_ctx *exec_ctx, grpc_combiner *lock) {
  move_next(exec_ctx);
  GRPC_COMBINER_TRACE(gpr_log(GPR_DEBUG, "C:%p queue_offload --> %p", lock,
                              lock->optional_workqueue));
  grpc_closure_sched(exec_ctx, &lock->offload, GRPC_ERROR_NONE);
}

bool grpc_combiner_continue_exec_ctx(grpc_exec_ctx *exec_ctx) {
  GPR_TIMER_BEGIN("combiner.continue_exec_ctx", 0);
  grpc_combiner *lock = exec_ctx->active_combiner;
  if (lock == NULL) {
    GPR_TIMER_END("combiner.continue_exec_ctx", 0);
    return false;
  }

  GRPC_COMBINER_TRACE(
      gpr_log(GPR_DEBUG,
              "C:%p grpc_combiner_continue_exec_ctx workqueue=%p "
              "is_covered_by_poller=" IS_COVERED_BY_POLLER_FMT
              " exec_ctx_ready_to_finish=%d "
              "time_to_execute_final_list=%d",
              lock, lock->optional_workqueue, IS_COVERED_BY_POLLER_ARGS(lock),
              grpc_exec_ctx_ready_to_finish(exec_ctx),
              lock->time_to_execute_final_list));

  if (lock->optional_workqueue != NULL && is_covered_by_poller(lock) &&
      grpc_exec_ctx_ready_to_finish(exec_ctx)) {
    GPR_TIMER_MARK("offload_from_finished_exec_ctx", 0);
    // this execution context wants to move on, and we have a workqueue (and
    // so can help the execution context out): schedule remaining work to be
    // picked up on the workqueue
    queue_offload(exec_ctx, lock);
    GPR_TIMER_END("combiner.continue_exec_ctx", 0);
    return true;
  }

  if (!lock->time_to_execute_final_list ||
      // peek to see if something new has shown up, and execute that with
      // priority
      (gpr_atm_acq_load(&lock->state) >> 1) > 1) {
    gpr_mpscq_node *n = gpr_mpscq_pop(&lock->queue);
    GRPC_COMBINER_TRACE(
        gpr_log(GPR_DEBUG, "C:%p maybe_finish_one n=%p", lock, n));
    if (n == NULL) {
      // queue is in an inconsistent state: use this as a cue that we should
      // go off and do something else for a while (and come back later)
      GPR_TIMER_MARK("delay_busy", 0);
      if (lock->optional_workqueue != NULL && is_covered_by_poller(lock)) {
        queue_offload(exec_ctx, lock);
      }
      GPR_TIMER_END("combiner.continue_exec_ctx", 0);
      return true;
    }
    GPR_TIMER_BEGIN("combiner.exec1", 0);
    grpc_closure *cl = (grpc_closure *)n;
    error_data err = unpack_error_data(cl->error_data.scratch);
    cl->cb(exec_ctx, cl->cb_arg, err.error);
    if (err.covered_by_poller) {
      gpr_atm_no_barrier_fetch_add(&lock->elements_covered_by_poller, -1);
    }
    GRPC_ERROR_UNREF(err.error);
    GPR_TIMER_END("combiner.exec1", 0);
  } else {
    grpc_closure *c = lock->final_list.head;
    GPR_ASSERT(c != NULL);
    grpc_closure_list_init(&lock->final_list);
    lock->final_list_covered_by_poller = false;
    int loops = 0;
    while (c != NULL) {
      GPR_TIMER_BEGIN("combiner.exec_1final", 0);
      GRPC_COMBINER_TRACE(
          gpr_log(GPR_DEBUG, "C:%p execute_final[%d] c=%p", lock, loops, c));
      grpc_closure *next = c->next_data.next;
      grpc_error *error = c->error_data.error;
      c->cb(exec_ctx, c->cb_arg, error);
      GRPC_ERROR_UNREF(error);
      c = next;
      GPR_TIMER_END("combiner.exec_1final", 0);
    }
  }

  GPR_TIMER_MARK("unref", 0);
  move_next(exec_ctx);
  lock->time_to_execute_final_list = false;
  gpr_atm old_state =
      gpr_atm_full_fetch_add(&lock->state, -STATE_ELEM_COUNT_LOW_BIT);
  GRPC_COMBINER_TRACE(
      gpr_log(GPR_DEBUG, "C:%p finish old_state=%" PRIdPTR, lock, old_state));
// Define a macro to ease readability of the following switch statement.
#define OLD_STATE_WAS(orphaned, elem_count) \
  (((orphaned) ? 0 : STATE_UNORPHANED) |    \
   ((elem_count)*STATE_ELEM_COUNT_LOW_BIT))
  // Depending on what the previous state was, we need to perform different
  // actions.
  switch (old_state) {
    default:
      // we have multiple queued work items: just continue executing them
      break;
    case OLD_STATE_WAS(false, 2):
    case OLD_STATE_WAS(true, 2):
      // we're down to one queued item: if it's the final list we should do that
      if (!grpc_closure_list_empty(lock->final_list)) {
        lock->time_to_execute_final_list = true;
      }
      break;
    case OLD_STATE_WAS(false, 1):
      // had one count, one unorphaned --> unlocked unorphaned
      GPR_TIMER_END("combiner.continue_exec_ctx", 0);
      return true;
    case OLD_STATE_WAS(true, 1):
      // and one count, one orphaned --> unlocked and orphaned
      really_destroy(exec_ctx, lock);
      GPR_TIMER_END("combiner.continue_exec_ctx", 0);
      return true;
    case OLD_STATE_WAS(false, 0):
    case OLD_STATE_WAS(true, 0):
      // these values are illegal - representing an already unlocked or
      // deleted lock
      GPR_TIMER_END("combiner.continue_exec_ctx", 0);
      GPR_UNREACHABLE_CODE(return true);
  }
  push_first_on_exec_ctx(exec_ctx, lock);
  GPR_TIMER_END("combiner.continue_exec_ctx", 0);
  return true;
}

static void enqueue_finally(grpc_exec_ctx *exec_ctx, void *closure,
                            grpc_error *error);

static void combiner_execute_finally(grpc_exec_ctx *exec_ctx,
                                     grpc_combiner *lock, grpc_closure *closure,
                                     grpc_error *error,
                                     bool covered_by_poller) {
  GRPC_COMBINER_TRACE(gpr_log(
      GPR_DEBUG, "C:%p grpc_combiner_execute_finally c=%p; ac=%p; cov=%d", lock,
      closure, exec_ctx->active_combiner, covered_by_poller));
  GPR_TIMER_BEGIN("combiner.execute_finally", 0);
  if (exec_ctx->active_combiner != lock) {
    GPR_TIMER_MARK("slowpath", 0);
    grpc_closure_sched(
        exec_ctx, grpc_closure_create(enqueue_finally, closure,
                                      grpc_combiner_scheduler(lock, false)),
        error);
    GPR_TIMER_END("combiner.execute_finally", 0);
    return;
  }

  if (grpc_closure_list_empty(lock->final_list)) {
    gpr_atm_full_fetch_add(&lock->state, STATE_ELEM_COUNT_LOW_BIT);
  }
  if (covered_by_poller) {
    lock->final_list_covered_by_poller = true;
  }
  grpc_closure_list_append(&lock->final_list, closure, error);
  GPR_TIMER_END("combiner.execute_finally", 0);
}

static void enqueue_finally(grpc_exec_ctx *exec_ctx, void *closure,
                            grpc_error *error) {
  combiner_execute_finally(exec_ctx, exec_ctx->active_combiner, closure,
                           GRPC_ERROR_REF(error), false);
}

static void combiner_finally_exec_uncovered(grpc_exec_ctx *exec_ctx,
                                            grpc_closure *cl,
                                            grpc_error *error) {
  combiner_execute_finally(exec_ctx, COMBINER_FROM_CLOSURE_SCHEDULER(
                                         cl, uncovered_finally_scheduler),
                           cl, error, false);
}

static void combiner_finally_exec_covered(grpc_exec_ctx *exec_ctx,
                                          grpc_closure *cl, grpc_error *error) {
  combiner_execute_finally(
      exec_ctx, COMBINER_FROM_CLOSURE_SCHEDULER(cl, covered_finally_scheduler),
      cl, error, true);
}

grpc_closure_scheduler *grpc_combiner_scheduler(grpc_combiner *combiner,
                                                bool covered_by_poller) {
  return covered_by_poller ? &combiner->covered_scheduler
                           : &combiner->uncovered_scheduler;
}

grpc_closure_scheduler *grpc_combiner_finally_scheduler(
    grpc_combiner *combiner, bool covered_by_poller) {
  return covered_by_poller ? &combiner->covered_finally_scheduler
                           : &combiner->uncovered_finally_scheduler;
}
