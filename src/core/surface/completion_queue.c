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

#define NUM_TAG_BUCKETS 31

/* A single event: extends grpc_event to form a linked list with a destruction
   function (on_finish) that is hidden from outside this module */
typedef struct event {
  grpc_event base;
  struct event *queue_next;
  struct event *queue_prev;
  struct event *bucket_next;
  struct event *bucket_prev;
} event;

/* Completion queue structure */
struct grpc_completion_queue {
  /* TODO(ctiller): see if this can be removed */
  int allow_polling;

  /* When refs drops to zero, we are in shutdown mode, and will be destroyable
     once all queued events are drained */
  gpr_refcount refs;
  /* Once owning_refs drops to zero, we will destroy the cq */
  gpr_refcount owning_refs;
  /* the set of low level i/o things that concern this cq */
  grpc_pollset pollset;
  /* 0 initially, 1 once we've begun shutting down */
  int shutdown;
  int shutdown_called;
  /* Head of a linked list of queued events (prev points to the last element) */
  event *queue;
  /* Fixed size chained hash table of events for pluck() */
  event *buckets[NUM_TAG_BUCKETS];
};

grpc_completion_queue *grpc_completion_queue_create(void) {
  grpc_completion_queue *cc = gpr_malloc(sizeof(grpc_completion_queue));
  memset(cc, 0, sizeof(*cc));
  /* Initial ref is dropped by grpc_completion_queue_shutdown */
  gpr_ref_init(&cc->refs, 1);
  gpr_ref_init(&cc->owning_refs, 1);
  grpc_pollset_init(&cc->pollset);
  cc->allow_polling = 1;
  return cc;
}

void grpc_cq_internal_ref(grpc_completion_queue *cc) {
  gpr_ref(&cc->owning_refs);
}

static void on_pollset_destroy_done(void *arg) {
  grpc_completion_queue *cc = arg;
  grpc_pollset_destroy(&cc->pollset);
  gpr_free(cc);
}

void grpc_cq_internal_unref(grpc_completion_queue *cc) {
  if (gpr_unref(&cc->owning_refs)) {
    GPR_ASSERT(cc->queue == NULL);
    grpc_pollset_shutdown(&cc->pollset, on_pollset_destroy_done, cc);
  }
}

void grpc_completion_queue_dont_poll_test_only(grpc_completion_queue *cc) {
  cc->allow_polling = 0;
}

/* Create and append an event to the queue. Returns the event so that its data
   members can be filled in.
   Requires GRPC_POLLSET_MU(&cc->pollset) locked. */
static event *add_locked(grpc_completion_queue *cc, grpc_completion_type type,
                         void *tag, grpc_call *call) {
  event *ev = gpr_malloc(sizeof(event));
  gpr_uintptr bucket = ((gpr_uintptr)tag) % NUM_TAG_BUCKETS;
  ev->base.type = type;
  ev->base.tag = tag;
  if (cc->queue == NULL) {
    cc->queue = ev->queue_next = ev->queue_prev = ev;
  } else {
    ev->queue_next = cc->queue;
    ev->queue_prev = cc->queue->queue_prev;
    ev->queue_next->queue_prev = ev->queue_prev->queue_next = ev;
  }
  if (cc->buckets[bucket] == NULL) {
    cc->buckets[bucket] = ev->bucket_next = ev->bucket_prev = ev;
  } else {
    ev->bucket_next = cc->buckets[bucket];
    ev->bucket_prev = cc->buckets[bucket]->bucket_prev;
    ev->bucket_next->bucket_prev = ev->bucket_prev->bucket_next = ev;
  }
  gpr_cv_broadcast(GRPC_POLLSET_CV(&cc->pollset));
  grpc_pollset_kick(&cc->pollset);
  return ev;
}

void grpc_cq_begin_op(grpc_completion_queue *cc, grpc_call *call) {
  gpr_ref(&cc->refs);
  if (call) GRPC_CALL_INTERNAL_REF(call, "cq");
}

/* Signal the end of an operation - if this is the last waiting-to-be-queued
   event, then enter shutdown mode */
static void end_op_locked(grpc_completion_queue *cc,
                          grpc_completion_type type) {
  if (gpr_unref(&cc->refs)) {
    GPR_ASSERT(!cc->shutdown);
    GPR_ASSERT(cc->shutdown_called);
    cc->shutdown = 1;
    gpr_cv_broadcast(GRPC_POLLSET_CV(&cc->pollset));
  }
}

void grpc_cq_end_op(grpc_completion_queue *cc, void *tag, grpc_call *call,
                    int success) {
  event *ev;
  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
  ev = add_locked(cc, GRPC_OP_COMPLETE, tag, call);
  ev->base.success = success;
  end_op_locked(cc, GRPC_OP_COMPLETE);
  gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
  if (call) GRPC_CALL_INTERNAL_UNREF(call, "cq", 0);
}

/* Create a GRPC_QUEUE_SHUTDOWN event without queuing it anywhere */
static event *create_shutdown_event(void) {
  event *ev = gpr_malloc(sizeof(event));
  ev->base.type = GRPC_QUEUE_SHUTDOWN;
  ev->base.tag = NULL;
  return ev;
}

grpc_event grpc_completion_queue_next(grpc_completion_queue *cc,
                                      gpr_timespec deadline) {
  event *ev = NULL;
  grpc_event ret;

  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
  for (;;) {
    if (cc->queue != NULL) {
      gpr_uintptr bucket;
      ev = cc->queue;
      bucket = ((gpr_uintptr)ev->base.tag) % NUM_TAG_BUCKETS;
      cc->queue = ev->queue_next;
      ev->queue_next->queue_prev = ev->queue_prev;
      ev->queue_prev->queue_next = ev->queue_next;
      ev->bucket_next->bucket_prev = ev->bucket_prev;
      ev->bucket_prev->bucket_next = ev->bucket_next;
      if (ev == cc->buckets[bucket]) {
        cc->buckets[bucket] = ev->bucket_next;
        if (ev == cc->buckets[bucket]) {
          cc->buckets[bucket] = NULL;
        }
      }
      if (cc->queue == ev) {
        cc->queue = NULL;
      }
      break;
    }
    if (cc->shutdown) {
      ev = create_shutdown_event();
      break;
    }
    if (cc->allow_polling && grpc_pollset_work(&cc->pollset, deadline)) {
      continue;
    }
    if (gpr_cv_wait(GRPC_POLLSET_CV(&cc->pollset),
                    GRPC_POLLSET_MU(&cc->pollset), deadline)) {
      gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
      memset(&ret, 0, sizeof(ret));
      ret.type = GRPC_QUEUE_TIMEOUT;
      GRPC_SURFACE_TRACE_RETURNED_EVENT(cc, &ret);
      return ret;
    }
  }
  gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
  ret = ev->base;
  gpr_free(ev);
  GRPC_SURFACE_TRACE_RETURNED_EVENT(cc, &ret);
  return ret;
}

static event *pluck_event(grpc_completion_queue *cc, void *tag) {
  gpr_uintptr bucket = ((gpr_uintptr)tag) % NUM_TAG_BUCKETS;
  event *ev = cc->buckets[bucket];
  if (ev == NULL) return NULL;
  do {
    if (ev->base.tag == tag) {
      ev->queue_next->queue_prev = ev->queue_prev;
      ev->queue_prev->queue_next = ev->queue_next;
      ev->bucket_next->bucket_prev = ev->bucket_prev;
      ev->bucket_prev->bucket_next = ev->bucket_next;
      if (ev == cc->buckets[bucket]) {
        cc->buckets[bucket] = ev->bucket_next;
        if (ev == cc->buckets[bucket]) {
          cc->buckets[bucket] = NULL;
        }
      }
      if (cc->queue == ev) {
        cc->queue = ev->queue_next;
        if (cc->queue == ev) {
          cc->queue = NULL;
        }
      }
      return ev;
    }
    ev = ev->bucket_next;
  } while (ev != cc->buckets[bucket]);
  return NULL;
}

grpc_event grpc_completion_queue_pluck(grpc_completion_queue *cc, void *tag,
                                       gpr_timespec deadline) {
  event *ev = NULL;
  grpc_event ret;

  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
  for (;;) {
    if ((ev = pluck_event(cc, tag))) {
      break;
    }
    if (cc->shutdown) {
      ev = create_shutdown_event();
      break;
    }
    if (cc->allow_polling && grpc_pollset_work(&cc->pollset, deadline)) {
      continue;
    }
    if (gpr_cv_wait(GRPC_POLLSET_CV(&cc->pollset),
                    GRPC_POLLSET_MU(&cc->pollset), deadline)) {
      gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
      memset(&ret, 0, sizeof(ret));
      ret.type = GRPC_QUEUE_TIMEOUT;
      GRPC_SURFACE_TRACE_RETURNED_EVENT(cc, &ret);
      return ret;
    }
  }
  gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
  ret = ev->base;
  gpr_free(ev);
  GRPC_SURFACE_TRACE_RETURNED_EVENT(cc, &ret);
  return ret;
}

/* Shutdown simply drops a ref that we reserved at creation time; if we drop
   to zero here, then enter shutdown mode and wake up any waiters */
void grpc_completion_queue_shutdown(grpc_completion_queue *cc) {
  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
  cc->shutdown_called = 1;
  gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));

  if (gpr_unref(&cc->refs)) {
    gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
    GPR_ASSERT(!cc->shutdown);
    cc->shutdown = 1;
    gpr_cv_broadcast(GRPC_POLLSET_CV(&cc->pollset));
    gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
  }
}

void grpc_completion_queue_destroy(grpc_completion_queue *cc) {
  grpc_cq_internal_unref(cc);
}

grpc_pollset *grpc_cq_pollset(grpc_completion_queue *cc) {
  return &cc->pollset;
}

void grpc_cq_hack_spin_pollset(grpc_completion_queue *cc) {
  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
  grpc_pollset_kick(&cc->pollset);
  grpc_pollset_work(&cc->pollset,
                    gpr_time_add(gpr_now(), gpr_time_from_millis(100)));
  gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
}
