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

#include "src/core/iomgr/pollset.h"
#include "src/core/support/string.h"
#include "src/core/surface/call.h"
#include "src/core/surface/event_string.h"
#include "src/core/surface/surface_trace.h"
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>

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
};

grpc_completion_queue *grpc_completion_queue_create(void) {
  grpc_completion_queue *cc = gpr_malloc(sizeof(grpc_completion_queue));
  memset(cc, 0, sizeof(*cc));
  /* Initial ref is dropped by grpc_completion_queue_shutdown */
  gpr_ref_init(&cc->pending_events, 1);
  /* One for destroy(), one for pollset_shutdown */
  gpr_ref_init(&cc->owning_refs, 2);
  grpc_pollset_init(&cc->pollset);
  cc->completed_tail = &cc->completed_head;
  cc->completed_head.next = (gpr_uintptr)cc->completed_tail;
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

static void on_pollset_destroy_done(void *arg) {
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
    grpc_pollset_destroy(&cc->pollset);
    gpr_free(cc);
  }
}

void grpc_cq_begin_op(grpc_completion_queue *cc) {
  gpr_ref(&cc->pending_events);
}

/* Signal the end of an operation - if this is the last waiting-to-be-queued
   event, then enter shutdown mode */
/* Queue a GRPC_OP_COMPLETED operation */
void grpc_cq_end_op(grpc_completion_queue *cc, void *tag, int success,
                    void (*done)(void *done_arg, grpc_cq_completion *storage),
                    void *done_arg, grpc_cq_completion *storage) {
  int shutdown;

  storage->tag = tag;
  storage->done = done;
  storage->done_arg = done_arg;
  storage->next =
      ((gpr_uintptr)&cc->completed_head) | ((gpr_uintptr)(success != 0));

  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
  shutdown = gpr_unref(&cc->pending_events);
  if (!shutdown) {
    cc->completed_tail->next =
        ((gpr_uintptr)storage) | (1u & (gpr_uintptr)cc->completed_tail->next);
    cc->completed_tail = storage;
    grpc_pollset_kick(&cc->pollset);
    gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
  } else {
    cc->completed_tail->next =
        ((gpr_uintptr)storage) | (1u & (gpr_uintptr)cc->completed_tail->next);
    cc->completed_tail = storage;
    GPR_ASSERT(!cc->shutdown);
    GPR_ASSERT(cc->shutdown_called);
    cc->shutdown = 1;
    gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
    grpc_pollset_shutdown(&cc->pollset, on_pollset_destroy_done, cc);
  }
}

grpc_event grpc_completion_queue_next(grpc_completion_queue *cc,
                                      gpr_timespec deadline) {
  grpc_event ret;

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
      c->done(c->done_arg, c);
      break;
    }
    if (cc->shutdown) {
      gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
      memset(&ret, 0, sizeof(ret));
      ret.type = GRPC_QUEUE_SHUTDOWN;
      break;
    }
    if (!grpc_pollset_work(&cc->pollset, deadline)) {
      gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
      memset(&ret, 0, sizeof(ret));
      ret.type = GRPC_QUEUE_TIMEOUT;
      break;
    }
  }
  GRPC_SURFACE_TRACE_RETURNED_EVENT(cc, &ret);
  GRPC_CQ_INTERNAL_UNREF(cc, "next");
  return ret;
}

grpc_event grpc_completion_queue_pluck(grpc_completion_queue *cc, void *tag,
                                       gpr_timespec deadline) {
  grpc_event ret;
  grpc_cq_completion *c;
  grpc_cq_completion *prev;

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
        c->done(c->done_arg, c);
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
    if (!grpc_pollset_work(&cc->pollset, deadline)) {
      gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
      memset(&ret, 0, sizeof(ret));
      ret.type = GRPC_QUEUE_TIMEOUT;
      break;
    }
  }
done:
  GRPC_SURFACE_TRACE_RETURNED_EVENT(cc, &ret);
  GRPC_CQ_INTERNAL_UNREF(cc, "pluck");
  return ret;
}

/* Shutdown simply drops a ref that we reserved at creation time; if we drop
   to zero here, then enter shutdown mode and wake up any waiters */
void grpc_completion_queue_shutdown(grpc_completion_queue *cc) {
  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
  if (cc->shutdown_called) {
    gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
    return;
  }
  cc->shutdown_called = 1;
  gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));

  if (gpr_unref(&cc->pending_events)) {
    gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
    GPR_ASSERT(!cc->shutdown);
    cc->shutdown = 1;
    gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
    grpc_pollset_shutdown(&cc->pollset, on_pollset_destroy_done, cc);
  }
}

void grpc_completion_queue_destroy(grpc_completion_queue *cc) {
  grpc_completion_queue_shutdown(cc);
  GRPC_CQ_INTERNAL_UNREF(cc, "destroy");
}

grpc_pollset *grpc_cq_pollset(grpc_completion_queue *cc) {
  return &cc->pollset;
}

void grpc_cq_hack_spin_pollset(grpc_completion_queue *cc) {
  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
  grpc_pollset_kick(&cc->pollset);
  grpc_pollset_work(&cc->pollset,
                    gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                 gpr_time_from_millis(100, GPR_TIMESPAN)));
  gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
}

void grpc_cq_mark_server_cq(grpc_completion_queue *cc) { cc->is_server_cq = 1; }

int grpc_cq_is_server_cq(grpc_completion_queue *cc) { return cc->is_server_cq; }
