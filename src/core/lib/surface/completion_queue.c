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

#include "src/core/lib/surface/completion_queue.h"

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/event_string.h"

int grpc_trace_operation_failures;
#ifndef NDEBUG
int grpc_trace_pending_tags;
#endif

typedef struct {
  grpc_pollset_worker **worker;
  void *tag;
} plucker;

/* Completion queue structure */
struct grpc_completion_queue {
  /** owned by pollset */
  gpr_mu *mu;
  /** completed events */
  grpc_cq_completion completed_head;
  grpc_cq_completion *completed_tail;
  /** Number of pending events (+1 if we're not shutdown) */
  gpr_refcount pending_events;
  /** Once owning_refs drops to zero, we will destroy the cq */
  gpr_refcount owning_refs;
  /** counter of how many things have ever been queued on this completion queue
      useful for avoiding locks to check the queue */
  gpr_atm things_queued_ever;
  /** 0 initially, 1 once we've begun shutting down */
  int shutdown;
  int shutdown_called;
  int is_server_cq;
  /** Can the server cq accept incoming channels */
  int is_non_listening_server_cq;
  int num_pluckers;
  plucker pluckers[GRPC_MAX_COMPLETION_QUEUE_PLUCKERS];
  grpc_closure pollset_shutdown_done;

#ifndef NDEBUG
  void **outstanding_tags;
  size_t outstanding_tag_count;
  size_t outstanding_tag_capacity;
#endif

  grpc_completion_queue *next_free;
};

#define POLLSET_FROM_CQ(cq) ((grpc_pollset *)(cq + 1))
#define CQ_FROM_POLLSET(ps) (((grpc_completion_queue *)ps) - 1)

int grpc_cq_pluck_trace;
int grpc_cq_event_timeout_trace;

#define GRPC_SURFACE_TRACE_RETURNED_EVENT(cq, event)                  \
  if (grpc_api_trace &&                                               \
      (grpc_cq_pluck_trace || (event)->type != GRPC_QUEUE_TIMEOUT)) { \
    char *_ev = grpc_event_string(event);                             \
    gpr_log(GPR_INFO, "RETURN_EVENT[%p]: %s", cq, _ev);               \
    gpr_free(_ev);                                                    \
  }

static void on_pollset_shutdown_done(grpc_exec_ctx *exec_ctx, void *cc,
                                     grpc_error *error);

grpc_completion_queue *grpc_completion_queue_create(void *reserved) {
  grpc_completion_queue *cc;
  GPR_ASSERT(!reserved);

  GPR_TIMER_BEGIN("grpc_completion_queue_create", 0);

  GRPC_API_TRACE("grpc_completion_queue_create(reserved=%p)", 1, (reserved));

  cc = gpr_zalloc(sizeof(grpc_completion_queue) + grpc_pollset_size());
  grpc_pollset_init(POLLSET_FROM_CQ(cc), &cc->mu);
#ifndef NDEBUG
  cc->outstanding_tags = NULL;
  cc->outstanding_tag_capacity = 0;
#endif

  /* Initial ref is dropped by grpc_completion_queue_shutdown */
  gpr_ref_init(&cc->pending_events, 1);
  /* One for destroy(), one for pollset_shutdown */
  gpr_ref_init(&cc->owning_refs, 2);
  cc->completed_tail = &cc->completed_head;
  cc->completed_head.next = (uintptr_t)cc->completed_tail;
  cc->shutdown = 0;
  cc->shutdown_called = 0;
  cc->is_server_cq = 0;
  cc->is_non_listening_server_cq = 0;
  cc->num_pluckers = 0;
  gpr_atm_no_barrier_store(&cc->things_queued_ever, 0);
#ifndef NDEBUG
  cc->outstanding_tag_count = 0;
#endif
  grpc_closure_init(&cc->pollset_shutdown_done, on_pollset_shutdown_done, cc,
                    grpc_schedule_on_exec_ctx);

  GPR_TIMER_END("grpc_completion_queue_create", 0);

  return cc;
}

#ifdef GRPC_CQ_REF_COUNT_DEBUG
void grpc_cq_internal_ref(grpc_completion_queue *cc, const char *reason,
                          const char *file, int line) {
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "CQ:%p   ref %d -> %d %s", cc,
          (int)cc->owning_refs.count, (int)cc->owning_refs.count + 1, reason);
#else
void grpc_cq_internal_ref(grpc_completion_queue *cc) {
#endif
  gpr_ref(&cc->owning_refs);
}

static void on_pollset_shutdown_done(grpc_exec_ctx *exec_ctx, void *arg,
                                     grpc_error *error) {
  grpc_completion_queue *cc = arg;
  GRPC_CQ_INTERNAL_UNREF(cc, "pollset_destroy");
}

#ifdef GRPC_CQ_REF_COUNT_DEBUG
void grpc_cq_internal_unref(grpc_completion_queue *cc, const char *reason,
                            const char *file, int line) {
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "CQ:%p unref %d -> %d %s", cc,
          (int)cc->owning_refs.count, (int)cc->owning_refs.count - 1, reason);
#else
void grpc_cq_internal_unref(grpc_completion_queue *cc) {
#endif
  if (gpr_unref(&cc->owning_refs)) {
    GPR_ASSERT(cc->completed_head.next == (uintptr_t)&cc->completed_head);
    grpc_pollset_destroy(POLLSET_FROM_CQ(cc));
#ifndef NDEBUG
    gpr_free(cc->outstanding_tags);
#endif
    gpr_free(cc);
  }
}

void grpc_cq_begin_op(grpc_completion_queue *cc, void *tag) {
#ifndef NDEBUG
  gpr_mu_lock(cc->mu);
  GPR_ASSERT(!cc->shutdown_called);
  if (cc->outstanding_tag_count == cc->outstanding_tag_capacity) {
    cc->outstanding_tag_capacity = GPR_MAX(4, 2 * cc->outstanding_tag_capacity);
    cc->outstanding_tags =
        gpr_realloc(cc->outstanding_tags, sizeof(*cc->outstanding_tags) *
                                              cc->outstanding_tag_capacity);
  }
  cc->outstanding_tags[cc->outstanding_tag_count++] = tag;
  gpr_mu_unlock(cc->mu);
#endif
  gpr_ref(&cc->pending_events);
}

/* Signal the end of an operation - if this is the last waiting-to-be-queued
   event, then enter shutdown mode */
/* Queue a GRPC_OP_COMPLETED operation */
void grpc_cq_end_op(grpc_exec_ctx *exec_ctx, grpc_completion_queue *cc,
                    void *tag, grpc_error *error,
                    void (*done)(grpc_exec_ctx *exec_ctx, void *done_arg,
                                 grpc_cq_completion *storage),
                    void *done_arg, grpc_cq_completion *storage) {
  int shutdown;
  int i;
  grpc_pollset_worker *pluck_worker;
#ifndef NDEBUG
  int found = 0;
#endif

  GPR_TIMER_BEGIN("grpc_cq_end_op", 0);
  if (grpc_api_trace ||
      (grpc_trace_operation_failures && error != GRPC_ERROR_NONE)) {
    const char *errmsg = grpc_error_string(error);
    GRPC_API_TRACE(
        "grpc_cq_end_op(exec_ctx=%p, cc=%p, tag=%p, error=%s, done=%p, "
        "done_arg=%p, storage=%p)",
        7, (exec_ctx, cc, tag, errmsg, done, done_arg, storage));
    if (grpc_trace_operation_failures && error != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR, "Operation failed: tag=%p, error=%s", tag, errmsg);
    }
  }

  storage->tag = tag;
  storage->done = done;
  storage->done_arg = done_arg;
  storage->next = ((uintptr_t)&cc->completed_head) |
                  ((uintptr_t)(error == GRPC_ERROR_NONE));

  gpr_mu_lock(cc->mu);
#ifndef NDEBUG
  for (i = 0; i < (int)cc->outstanding_tag_count; i++) {
    if (cc->outstanding_tags[i] == tag) {
      cc->outstanding_tag_count--;
      GPR_SWAP(void *, cc->outstanding_tags[i],
               cc->outstanding_tags[cc->outstanding_tag_count]);
      found = 1;
      break;
    }
  }
  GPR_ASSERT(found);
#endif
  shutdown = gpr_unref(&cc->pending_events);
  gpr_atm_no_barrier_fetch_add(&cc->things_queued_ever, 1);
  if (!shutdown) {
    cc->completed_tail->next =
        ((uintptr_t)storage) | (1u & (uintptr_t)cc->completed_tail->next);
    cc->completed_tail = storage;
    pluck_worker = NULL;
    for (i = 0; i < cc->num_pluckers; i++) {
      if (cc->pluckers[i].tag == tag) {
        pluck_worker = *cc->pluckers[i].worker;
        break;
      }
    }
    grpc_error *kick_error =
        grpc_pollset_kick(POLLSET_FROM_CQ(cc), pluck_worker);
    gpr_mu_unlock(cc->mu);
    if (kick_error != GRPC_ERROR_NONE) {
      const char *msg = grpc_error_string(kick_error);
      gpr_log(GPR_ERROR, "Kick failed: %s", msg);

      GRPC_ERROR_UNREF(kick_error);
    }
  } else {
    cc->completed_tail->next =
        ((uintptr_t)storage) | (1u & (uintptr_t)cc->completed_tail->next);
    cc->completed_tail = storage;
    GPR_ASSERT(!cc->shutdown);
    GPR_ASSERT(cc->shutdown_called);
    cc->shutdown = 1;
    grpc_pollset_shutdown(exec_ctx, POLLSET_FROM_CQ(cc),
                          &cc->pollset_shutdown_done);
    gpr_mu_unlock(cc->mu);
  }

  GPR_TIMER_END("grpc_cq_end_op", 0);

  GRPC_ERROR_UNREF(error);
}

typedef struct {
  gpr_atm last_seen_things_queued_ever;
  grpc_completion_queue *cq;
  gpr_timespec deadline;
  grpc_cq_completion *stolen_completion;
  void *tag; /* for pluck */
  bool first_loop;
} cq_is_finished_arg;

static bool cq_is_next_finished(grpc_exec_ctx *exec_ctx, void *arg) {
  cq_is_finished_arg *a = arg;
  grpc_completion_queue *cq = a->cq;
  GPR_ASSERT(a->stolen_completion == NULL);
  gpr_atm current_last_seen_things_queued_ever =
      gpr_atm_no_barrier_load(&cq->things_queued_ever);
  if (current_last_seen_things_queued_ever != a->last_seen_things_queued_ever) {
    gpr_mu_lock(cq->mu);
    a->last_seen_things_queued_ever =
        gpr_atm_no_barrier_load(&cq->things_queued_ever);
    if (cq->completed_tail != &cq->completed_head) {
      a->stolen_completion = (grpc_cq_completion *)cq->completed_head.next;
      cq->completed_head.next = a->stolen_completion->next & ~(uintptr_t)1;
      if (a->stolen_completion == cq->completed_tail) {
        cq->completed_tail = &cq->completed_head;
      }
      gpr_mu_unlock(cq->mu);
      return true;
    }
    gpr_mu_unlock(cq->mu);
  }
  return !a->first_loop &&
         gpr_time_cmp(a->deadline, gpr_now(a->deadline.clock_type)) < 0;
}

#ifndef NDEBUG
static void dump_pending_tags(grpc_completion_queue *cc) {
  if (!grpc_trace_pending_tags) return;

  gpr_strvec v;
  gpr_strvec_init(&v);
  gpr_strvec_add(&v, gpr_strdup("PENDING TAGS:"));
  gpr_mu_lock(cc->mu);
  for (size_t i = 0; i < cc->outstanding_tag_count; i++) {
    char *s;
    gpr_asprintf(&s, " %p", cc->outstanding_tags[i]);
    gpr_strvec_add(&v, s);
  }
  gpr_mu_unlock(cc->mu);
  char *out = gpr_strvec_flatten(&v, NULL);
  gpr_strvec_destroy(&v);
  gpr_log(GPR_DEBUG, "%s", out);
  gpr_free(out);
}
#else
static void dump_pending_tags(grpc_completion_queue *cc) {}
#endif

grpc_event grpc_completion_queue_next(grpc_completion_queue *cc,
                                      gpr_timespec deadline, void *reserved) {
  grpc_event ret;
  grpc_pollset_worker *worker = NULL;
  gpr_timespec now;

  GPR_TIMER_BEGIN("grpc_completion_queue_next", 0);

  GRPC_API_TRACE(
      "grpc_completion_queue_next("
      "cc=%p, "
      "deadline=gpr_timespec { tv_sec: %" PRId64
      ", tv_nsec: %d, clock_type: %d }, "
      "reserved=%p)",
      5, (cc, deadline.tv_sec, deadline.tv_nsec, (int)deadline.clock_type,
          reserved));
  GPR_ASSERT(!reserved);

  dump_pending_tags(cc);

  deadline = gpr_convert_clock_type(deadline, GPR_CLOCK_MONOTONIC);

  GRPC_CQ_INTERNAL_REF(cc, "next");
  gpr_mu_lock(cc->mu);
  cq_is_finished_arg is_finished_arg = {
      .last_seen_things_queued_ever =
          gpr_atm_no_barrier_load(&cc->things_queued_ever),
      .cq = cc,
      .deadline = deadline,
      .stolen_completion = NULL,
      .tag = NULL,
      .first_loop = true};
  grpc_exec_ctx exec_ctx =
      GRPC_EXEC_CTX_INITIALIZER(0, cq_is_next_finished, &is_finished_arg);
  for (;;) {
    if (is_finished_arg.stolen_completion != NULL) {
      gpr_mu_unlock(cc->mu);
      grpc_cq_completion *c = is_finished_arg.stolen_completion;
      is_finished_arg.stolen_completion = NULL;
      ret.type = GRPC_OP_COMPLETE;
      ret.success = c->next & 1u;
      ret.tag = c->tag;
      c->done(&exec_ctx, c->done_arg, c);
      break;
    }
    if (cc->completed_tail != &cc->completed_head) {
      grpc_cq_completion *c = (grpc_cq_completion *)cc->completed_head.next;
      cc->completed_head.next = c->next & ~(uintptr_t)1;
      if (c == cc->completed_tail) {
        cc->completed_tail = &cc->completed_head;
      }
      gpr_mu_unlock(cc->mu);
      ret.type = GRPC_OP_COMPLETE;
      ret.success = c->next & 1u;
      ret.tag = c->tag;
      c->done(&exec_ctx, c->done_arg, c);
      break;
    }
    if (cc->shutdown) {
      gpr_mu_unlock(cc->mu);
      memset(&ret, 0, sizeof(ret));
      ret.type = GRPC_QUEUE_SHUTDOWN;
      break;
    }
    now = gpr_now(GPR_CLOCK_MONOTONIC);
    if (!is_finished_arg.first_loop && gpr_time_cmp(now, deadline) >= 0) {
      gpr_mu_unlock(cc->mu);
      memset(&ret, 0, sizeof(ret));
      ret.type = GRPC_QUEUE_TIMEOUT;
      dump_pending_tags(cc);
      break;
    }
    /* Check alarms - these are a global resource so we just ping
       each time through on every pollset.
       May update deadline to ensure timely wakeups.
       TODO(ctiller): can this work be localized? */
    gpr_timespec iteration_deadline = deadline;
    if (grpc_timer_check(&exec_ctx, now, &iteration_deadline)) {
      GPR_TIMER_MARK("alarm_triggered", 0);
      gpr_mu_unlock(cc->mu);
      grpc_exec_ctx_flush(&exec_ctx);
      gpr_mu_lock(cc->mu);
      continue;
    } else {
      grpc_error *err = grpc_pollset_work(&exec_ctx, POLLSET_FROM_CQ(cc),
                                          &worker, now, iteration_deadline);
      if (err != GRPC_ERROR_NONE) {
        gpr_mu_unlock(cc->mu);
        const char *msg = grpc_error_string(err);
        gpr_log(GPR_ERROR, "Completion queue next failed: %s", msg);

        GRPC_ERROR_UNREF(err);
        memset(&ret, 0, sizeof(ret));
        ret.type = GRPC_QUEUE_TIMEOUT;
        dump_pending_tags(cc);
        break;
      }
    }
    is_finished_arg.first_loop = false;
  }
  GRPC_SURFACE_TRACE_RETURNED_EVENT(cc, &ret);
  GRPC_CQ_INTERNAL_UNREF(cc, "next");
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_ASSERT(is_finished_arg.stolen_completion == NULL);

  GPR_TIMER_END("grpc_completion_queue_next", 0);

  return ret;
}

static int add_plucker(grpc_completion_queue *cc, void *tag,
                       grpc_pollset_worker **worker) {
  if (cc->num_pluckers == GRPC_MAX_COMPLETION_QUEUE_PLUCKERS) {
    return 0;
  }
  cc->pluckers[cc->num_pluckers].tag = tag;
  cc->pluckers[cc->num_pluckers].worker = worker;
  cc->num_pluckers++;
  return 1;
}

static void del_plucker(grpc_completion_queue *cc, void *tag,
                        grpc_pollset_worker **worker) {
  int i;
  for (i = 0; i < cc->num_pluckers; i++) {
    if (cc->pluckers[i].tag == tag && cc->pluckers[i].worker == worker) {
      cc->num_pluckers--;
      GPR_SWAP(plucker, cc->pluckers[i], cc->pluckers[cc->num_pluckers]);
      return;
    }
  }
  GPR_UNREACHABLE_CODE(return );
}

static bool cq_is_pluck_finished(grpc_exec_ctx *exec_ctx, void *arg) {
  cq_is_finished_arg *a = arg;
  grpc_completion_queue *cq = a->cq;
  GPR_ASSERT(a->stolen_completion == NULL);
  gpr_atm current_last_seen_things_queued_ever =
      gpr_atm_no_barrier_load(&cq->things_queued_ever);
  if (current_last_seen_things_queued_ever != a->last_seen_things_queued_ever) {
    gpr_mu_lock(cq->mu);
    a->last_seen_things_queued_ever =
        gpr_atm_no_barrier_load(&cq->things_queued_ever);
    grpc_cq_completion *c;
    grpc_cq_completion *prev = &cq->completed_head;
    while ((c = (grpc_cq_completion *)(prev->next & ~(uintptr_t)1)) !=
           &cq->completed_head) {
      if (c->tag == a->tag) {
        prev->next = (prev->next & (uintptr_t)1) | (c->next & ~(uintptr_t)1);
        if (c == cq->completed_tail) {
          cq->completed_tail = prev;
        }
        gpr_mu_unlock(cq->mu);
        a->stolen_completion = c;
        return true;
      }
      prev = c;
    }
    gpr_mu_unlock(cq->mu);
  }
  return !a->first_loop &&
         gpr_time_cmp(a->deadline, gpr_now(a->deadline.clock_type)) < 0;
}

grpc_event grpc_completion_queue_pluck(grpc_completion_queue *cc, void *tag,
                                       gpr_timespec deadline, void *reserved) {
  grpc_event ret;
  grpc_cq_completion *c;
  grpc_cq_completion *prev;
  grpc_pollset_worker *worker = NULL;
  gpr_timespec now;

  GPR_TIMER_BEGIN("grpc_completion_queue_pluck", 0);

  if (grpc_cq_pluck_trace) {
    GRPC_API_TRACE(
        "grpc_completion_queue_pluck("
        "cc=%p, tag=%p, "
        "deadline=gpr_timespec { tv_sec: %" PRId64
        ", tv_nsec: %d, clock_type: %d }, "
        "reserved=%p)",
        6, (cc, tag, deadline.tv_sec, deadline.tv_nsec,
            (int)deadline.clock_type, reserved));
  }
  GPR_ASSERT(!reserved);

  dump_pending_tags(cc);

  deadline = gpr_convert_clock_type(deadline, GPR_CLOCK_MONOTONIC);

  GRPC_CQ_INTERNAL_REF(cc, "pluck");
  gpr_mu_lock(cc->mu);
  cq_is_finished_arg is_finished_arg = {
      .last_seen_things_queued_ever =
          gpr_atm_no_barrier_load(&cc->things_queued_ever),
      .cq = cc,
      .deadline = deadline,
      .stolen_completion = NULL,
      .tag = tag,
      .first_loop = true};
  grpc_exec_ctx exec_ctx =
      GRPC_EXEC_CTX_INITIALIZER(0, cq_is_pluck_finished, &is_finished_arg);
  for (;;) {
    if (is_finished_arg.stolen_completion != NULL) {
      gpr_mu_unlock(cc->mu);
      c = is_finished_arg.stolen_completion;
      is_finished_arg.stolen_completion = NULL;
      ret.type = GRPC_OP_COMPLETE;
      ret.success = c->next & 1u;
      ret.tag = c->tag;
      c->done(&exec_ctx, c->done_arg, c);
      break;
    }
    prev = &cc->completed_head;
    while ((c = (grpc_cq_completion *)(prev->next & ~(uintptr_t)1)) !=
           &cc->completed_head) {
      if (c->tag == tag) {
        prev->next = (prev->next & (uintptr_t)1) | (c->next & ~(uintptr_t)1);
        if (c == cc->completed_tail) {
          cc->completed_tail = prev;
        }
        gpr_mu_unlock(cc->mu);
        ret.type = GRPC_OP_COMPLETE;
        ret.success = c->next & 1u;
        ret.tag = c->tag;
        c->done(&exec_ctx, c->done_arg, c);
        goto done;
      }
      prev = c;
    }
    if (cc->shutdown) {
      gpr_mu_unlock(cc->mu);
      memset(&ret, 0, sizeof(ret));
      ret.type = GRPC_QUEUE_SHUTDOWN;
      break;
    }
    if (!add_plucker(cc, tag, &worker)) {
      gpr_log(GPR_DEBUG,
              "Too many outstanding grpc_completion_queue_pluck calls: maximum "
              "is %d",
              GRPC_MAX_COMPLETION_QUEUE_PLUCKERS);
      gpr_mu_unlock(cc->mu);
      memset(&ret, 0, sizeof(ret));
      /* TODO(ctiller): should we use a different result here */
      ret.type = GRPC_QUEUE_TIMEOUT;
      dump_pending_tags(cc);
      break;
    }
    now = gpr_now(GPR_CLOCK_MONOTONIC);
    if (!is_finished_arg.first_loop && gpr_time_cmp(now, deadline) >= 0) {
      del_plucker(cc, tag, &worker);
      gpr_mu_unlock(cc->mu);
      memset(&ret, 0, sizeof(ret));
      ret.type = GRPC_QUEUE_TIMEOUT;
      dump_pending_tags(cc);
      break;
    }
    /* Check alarms - these are a global resource so we just ping
       each time through on every pollset.
       May update deadline to ensure timely wakeups.
       TODO(ctiller): can this work be localized? */
    gpr_timespec iteration_deadline = deadline;
    if (grpc_timer_check(&exec_ctx, now, &iteration_deadline)) {
      GPR_TIMER_MARK("alarm_triggered", 0);
      gpr_mu_unlock(cc->mu);
      grpc_exec_ctx_flush(&exec_ctx);
      gpr_mu_lock(cc->mu);
    } else {
      grpc_error *err = grpc_pollset_work(&exec_ctx, POLLSET_FROM_CQ(cc),
                                          &worker, now, iteration_deadline);
      if (err != GRPC_ERROR_NONE) {
        del_plucker(cc, tag, &worker);
        gpr_mu_unlock(cc->mu);
        const char *msg = grpc_error_string(err);
        gpr_log(GPR_ERROR, "Completion queue next failed: %s", msg);

        GRPC_ERROR_UNREF(err);
        memset(&ret, 0, sizeof(ret));
        ret.type = GRPC_QUEUE_TIMEOUT;
        dump_pending_tags(cc);
        break;
      }
    }
    is_finished_arg.first_loop = false;
    del_plucker(cc, tag, &worker);
  }
done:
  GRPC_SURFACE_TRACE_RETURNED_EVENT(cc, &ret);
  GRPC_CQ_INTERNAL_UNREF(cc, "pluck");
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_ASSERT(is_finished_arg.stolen_completion == NULL);

  GPR_TIMER_END("grpc_completion_queue_pluck", 0);

  return ret;
}

/* Shutdown simply drops a ref that we reserved at creation time; if we drop
   to zero here, then enter shutdown mode and wake up any waiters */
void grpc_completion_queue_shutdown(grpc_completion_queue *cc) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  GPR_TIMER_BEGIN("grpc_completion_queue_shutdown", 0);
  GRPC_API_TRACE("grpc_completion_queue_shutdown(cc=%p)", 1, (cc));
  gpr_mu_lock(cc->mu);
  if (cc->shutdown_called) {
    gpr_mu_unlock(cc->mu);
    GPR_TIMER_END("grpc_completion_queue_shutdown", 0);
    return;
  }
  cc->shutdown_called = 1;
  if (gpr_unref(&cc->pending_events)) {
    GPR_ASSERT(!cc->shutdown);
    cc->shutdown = 1;
    grpc_pollset_shutdown(&exec_ctx, POLLSET_FROM_CQ(cc),
                          &cc->pollset_shutdown_done);
  }
  gpr_mu_unlock(cc->mu);
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_TIMER_END("grpc_completion_queue_shutdown", 0);
}

void grpc_completion_queue_destroy(grpc_completion_queue *cc) {
  GRPC_API_TRACE("grpc_completion_queue_destroy(cc=%p)", 1, (cc));
  GPR_TIMER_BEGIN("grpc_completion_queue_destroy", 0);
  grpc_completion_queue_shutdown(cc);
  GRPC_CQ_INTERNAL_UNREF(cc, "destroy");
  GPR_TIMER_END("grpc_completion_queue_destroy", 0);
}

grpc_pollset *grpc_cq_pollset(grpc_completion_queue *cc) {
  return POLLSET_FROM_CQ(cc);
}

grpc_completion_queue *grpc_cq_from_pollset(grpc_pollset *ps) {
  return CQ_FROM_POLLSET(ps);
}

void grpc_cq_mark_non_listening_server_cq(grpc_completion_queue *cc) {
  cc->is_non_listening_server_cq = 1;
}

bool grpc_cq_is_non_listening_server_cq(grpc_completion_queue *cc) {
  return (cc->is_non_listening_server_cq == 1);
}

void grpc_cq_mark_server_cq(grpc_completion_queue *cc) { cc->is_server_cq = 1; }

int grpc_cq_is_server_cq(grpc_completion_queue *cc) { return cc->is_server_cq; }
