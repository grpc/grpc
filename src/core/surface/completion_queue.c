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

#include "src/core/surface/completion_queue.h"

#include <stdio.h>
#include <string.h>

#include "src/core/iomgr/timer.h"
#include "src/core/iomgr/pollset.h"
#include "src/core/support/string.h"
#include "src/core/surface/api_trace.h"
#include "src/core/surface/call.h"
#include "src/core/surface/event_string.h"
#include "src/core/surface/surface_trace.h"
#include "src/core/profiling/timers.h"
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

typedef struct {
  grpc_pollset_worker *worker;
  void *tag;
} plucker;

/* Completion queue structure */
struct grpc_completion_queue {
  /** completed events */
  grpc_cq_completion completed_head;
  grpc_cq_completion *completed_tail;
  /** Number of pending events (+1 if we're not shutdown) */
  gpr_refcount pending_events;
  /** Once owning_refs drops to zero, we will destroy the cq */
  gpr_refcount owning_refs;
  /** the set of low level i/o things that concern this cq */
  grpc_pollset pollset;
  /** 0 initially, 1 once we've begun shutting down */
  int shutdown;
  int shutdown_called;
  int is_server_cq;
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

static gpr_mu g_freelist_mu;
grpc_completion_queue *g_freelist;

static void on_pollset_shutdown_done(grpc_exec_ctx *exec_ctx, void *cc,
                                     int success);

void grpc_cq_global_init(void) { gpr_mu_init(&g_freelist_mu); }

void grpc_cq_global_shutdown(void) {
  gpr_mu_destroy(&g_freelist_mu);
  while (g_freelist) {
    grpc_completion_queue *next = g_freelist->next_free;
    grpc_pollset_destroy(&g_freelist->pollset);
#ifndef NDEBUG
    gpr_free(g_freelist->outstanding_tags);
#endif
    gpr_free(g_freelist);
    g_freelist = next;
  }
}

struct grpc_cq_alarm {
  grpc_timer alarm;
  grpc_cq_completion completion;
  /** completion queue where events about this alarm will be posted */
  grpc_completion_queue *cq;
  /** user supplied tag */
  void *tag;
};

grpc_completion_queue *grpc_completion_queue_create(void *reserved) {
  grpc_completion_queue *cc;
  GPR_ASSERT(!reserved);

  GPR_TIMER_BEGIN("grpc_completion_queue_create", 0);

  GRPC_API_TRACE("grpc_completion_queue_create(reserved=%p)", 1, (reserved));

  gpr_mu_lock(&g_freelist_mu);
  if (g_freelist == NULL) {
    gpr_mu_unlock(&g_freelist_mu);

    cc = gpr_malloc(sizeof(grpc_completion_queue));
    grpc_pollset_init(&cc->pollset);
#ifndef NDEBUG
    cc->outstanding_tags = NULL;
    cc->outstanding_tag_capacity = 0;
#endif
  } else {
    cc = g_freelist;
    g_freelist = g_freelist->next_free;
    gpr_mu_unlock(&g_freelist_mu);
    /* pollset already initialized */
  }

  /* Initial ref is dropped by grpc_completion_queue_shutdown */
  gpr_ref_init(&cc->pending_events, 1);
  /* One for destroy(), one for pollset_shutdown */
  gpr_ref_init(&cc->owning_refs, 2);
  cc->completed_tail = &cc->completed_head;
  cc->completed_head.next = (gpr_uintptr)cc->completed_tail;
  cc->shutdown = 0;
  cc->shutdown_called = 0;
  cc->is_server_cq = 0;
  cc->num_pluckers = 0;
#ifndef NDEBUG
  cc->outstanding_tag_count = 0;
#endif
  grpc_closure_init(&cc->pollset_shutdown_done, on_pollset_shutdown_done, cc);

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
                                     int success) {
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
    GPR_ASSERT(cc->completed_head.next == (gpr_uintptr)&cc->completed_head);
    grpc_pollset_reset(&cc->pollset);
    gpr_mu_lock(&g_freelist_mu);
    cc->next_free = g_freelist;
    g_freelist = cc;
    gpr_mu_unlock(&g_freelist_mu);
  }
}

void grpc_cq_begin_op(grpc_completion_queue *cc, void *tag) {
#ifndef NDEBUG
  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
  GPR_ASSERT(!cc->shutdown_called);
  if (cc->outstanding_tag_count == cc->outstanding_tag_capacity) {
    cc->outstanding_tag_capacity = GPR_MAX(4, 2 * cc->outstanding_tag_capacity);
    cc->outstanding_tags =
        gpr_realloc(cc->outstanding_tags, sizeof(*cc->outstanding_tags) *
                                              cc->outstanding_tag_capacity);
  }
  cc->outstanding_tags[cc->outstanding_tag_count++] = tag;
  gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
#endif
  gpr_ref(&cc->pending_events);
}

/* Signal the end of an operation - if this is the last waiting-to-be-queued
   event, then enter shutdown mode */
/* Queue a GRPC_OP_COMPLETED operation */
void grpc_cq_end_op(grpc_exec_ctx *exec_ctx, grpc_completion_queue *cc,
                    void *tag, int success,
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

  storage->tag = tag;
  storage->done = done;
  storage->done_arg = done_arg;
  storage->next =
      ((gpr_uintptr)&cc->completed_head) | ((gpr_uintptr)(success != 0));

  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
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
  if (!shutdown) {
    cc->completed_tail->next =
        ((gpr_uintptr)storage) | (1u & (gpr_uintptr)cc->completed_tail->next);
    cc->completed_tail = storage;
    pluck_worker = NULL;
    for (i = 0; i < cc->num_pluckers; i++) {
      if (cc->pluckers[i].tag == tag) {
        pluck_worker = cc->pluckers[i].worker;
        break;
      }
    }
    grpc_pollset_kick(&cc->pollset, pluck_worker);
    gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
  } else {
    cc->completed_tail->next =
        ((gpr_uintptr)storage) | (1u & (gpr_uintptr)cc->completed_tail->next);
    cc->completed_tail = storage;
    GPR_ASSERT(!cc->shutdown);
    GPR_ASSERT(cc->shutdown_called);
    cc->shutdown = 1;
    grpc_pollset_shutdown(exec_ctx, &cc->pollset, &cc->pollset_shutdown_done);
    gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
  }

  GPR_TIMER_END("grpc_cq_end_op", 0);
}

grpc_event grpc_completion_queue_next(grpc_completion_queue *cc,
                                      gpr_timespec deadline, void *reserved) {
  grpc_event ret;
  grpc_pollset_worker worker;
  int first_loop = 1;
  gpr_timespec now;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  GPR_TIMER_BEGIN("grpc_completion_queue_next", 0);

  GRPC_API_TRACE(
      "grpc_completion_queue_next("
      "cc=%p, "
      "deadline=gpr_timespec { tv_sec: %lld, tv_nsec: %d, clock_type: %d }, "
      "reserved=%p)",
      5, (cc, (long long)deadline.tv_sec, (int)deadline.tv_nsec,
          (int)deadline.clock_type, reserved));
  GPR_ASSERT(!reserved);

  deadline = gpr_convert_clock_type(deadline, GPR_CLOCK_MONOTONIC);

  GRPC_CQ_INTERNAL_REF(cc, "next");
  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
  for (;;) {
    if (cc->completed_tail != &cc->completed_head) {
      grpc_cq_completion *c = (grpc_cq_completion *)cc->completed_head.next;
      cc->completed_head.next = c->next & ~(gpr_uintptr)1;
      if (c == cc->completed_tail) {
        cc->completed_tail = &cc->completed_head;
      }
      gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
      ret.type = GRPC_OP_COMPLETE;
      ret.success = c->next & 1u;
      ret.tag = c->tag;
      c->done(&exec_ctx, c->done_arg, c);
      break;
    }
    if (cc->shutdown) {
      gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
      memset(&ret, 0, sizeof(ret));
      ret.type = GRPC_QUEUE_SHUTDOWN;
      break;
    }
    now = gpr_now(GPR_CLOCK_MONOTONIC);
    if (!first_loop && gpr_time_cmp(now, deadline) >= 0) {
      gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
      memset(&ret, 0, sizeof(ret));
      ret.type = GRPC_QUEUE_TIMEOUT;
      break;
    }
    first_loop = 0;
    grpc_pollset_work(&exec_ctx, &cc->pollset, &worker, now, deadline);
  }
  GRPC_SURFACE_TRACE_RETURNED_EVENT(cc, &ret);
  GRPC_CQ_INTERNAL_UNREF(cc, "next");
  grpc_exec_ctx_finish(&exec_ctx);

  GPR_TIMER_END("grpc_completion_queue_next", 0);

  return ret;
}

static int add_plucker(grpc_completion_queue *cc, void *tag,
                       grpc_pollset_worker *worker) {
  if (cc->num_pluckers == GRPC_MAX_COMPLETION_QUEUE_PLUCKERS) {
    return 0;
  }
  cc->pluckers[cc->num_pluckers].tag = tag;
  cc->pluckers[cc->num_pluckers].worker = worker;
  cc->num_pluckers++;
  return 1;
}

static void del_plucker(grpc_completion_queue *cc, void *tag,
                        grpc_pollset_worker *worker) {
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

grpc_event grpc_completion_queue_pluck(grpc_completion_queue *cc, void *tag,
                                       gpr_timespec deadline, void *reserved) {
  grpc_event ret;
  grpc_cq_completion *c;
  grpc_cq_completion *prev;
  grpc_pollset_worker worker;
  gpr_timespec now;
  int first_loop = 1;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  GPR_TIMER_BEGIN("grpc_completion_queue_pluck", 0);

  GRPC_API_TRACE(
      "grpc_completion_queue_pluck("
      "cc=%p, tag=%p, "
      "deadline=gpr_timespec { tv_sec: %lld, tv_nsec: %d, clock_type: %d }, "
      "reserved=%p)",
      6, (cc, tag, (long long)deadline.tv_sec, (int)deadline.tv_nsec,
          (int)deadline.clock_type, reserved));
  GPR_ASSERT(!reserved);

  deadline = gpr_convert_clock_type(deadline, GPR_CLOCK_MONOTONIC);

  GRPC_CQ_INTERNAL_REF(cc, "pluck");
  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
  for (;;) {
    prev = &cc->completed_head;
    while ((c = (grpc_cq_completion *)(prev->next & ~(gpr_uintptr)1)) !=
           &cc->completed_head) {
      if (c->tag == tag) {
        prev->next =
            (prev->next & (gpr_uintptr)1) | (c->next & ~(gpr_uintptr)1);
        if (c == cc->completed_tail) {
          cc->completed_tail = prev;
        }
        gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
        ret.type = GRPC_OP_COMPLETE;
        ret.success = c->next & 1u;
        ret.tag = c->tag;
        c->done(&exec_ctx, c->done_arg, c);
        goto done;
      }
      prev = c;
    }
    if (cc->shutdown) {
      gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
      memset(&ret, 0, sizeof(ret));
      ret.type = GRPC_QUEUE_SHUTDOWN;
      break;
    }
    if (!add_plucker(cc, tag, &worker)) {
      gpr_log(GPR_DEBUG,
              "Too many outstanding grpc_completion_queue_pluck calls: maximum "
              "is %d",
              GRPC_MAX_COMPLETION_QUEUE_PLUCKERS);
      gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
      memset(&ret, 0, sizeof(ret));
      /* TODO(ctiller): should we use a different result here */
      ret.type = GRPC_QUEUE_TIMEOUT;
      break;
    }
    now = gpr_now(GPR_CLOCK_MONOTONIC);
    if (!first_loop && gpr_time_cmp(now, deadline) >= 0) {
      del_plucker(cc, tag, &worker);
      gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
      memset(&ret, 0, sizeof(ret));
      ret.type = GRPC_QUEUE_TIMEOUT;
      break;
    }
    first_loop = 0;
    grpc_pollset_work(&exec_ctx, &cc->pollset, &worker, now, deadline);
    del_plucker(cc, tag, &worker);
  }
done:
  GRPC_SURFACE_TRACE_RETURNED_EVENT(cc, &ret);
  GRPC_CQ_INTERNAL_UNREF(cc, "pluck");
  grpc_exec_ctx_finish(&exec_ctx);

  GPR_TIMER_END("grpc_completion_queue_pluck", 0);

  return ret;
}

/* Shutdown simply drops a ref that we reserved at creation time; if we drop
   to zero here, then enter shutdown mode and wake up any waiters */
void grpc_completion_queue_shutdown(grpc_completion_queue *cc) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  GPR_TIMER_BEGIN("grpc_completion_queue_shutdown", 0);
  GRPC_API_TRACE("grpc_completion_queue_shutdown(cc=%p)", 1, (cc));
  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
  if (cc->shutdown_called) {
    gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
    GPR_TIMER_END("grpc_completion_queue_shutdown", 0);
    return;
  }
  cc->shutdown_called = 1;
  if (gpr_unref(&cc->pending_events)) {
    GPR_ASSERT(!cc->shutdown);
    cc->shutdown = 1;
    grpc_pollset_shutdown(&exec_ctx, &cc->pollset, &cc->pollset_shutdown_done);
  }
  gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
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
  return &cc->pollset;
}

void grpc_cq_mark_server_cq(grpc_completion_queue *cc) { cc->is_server_cq = 1; }

int grpc_cq_is_server_cq(grpc_completion_queue *cc) { return cc->is_server_cq; }
