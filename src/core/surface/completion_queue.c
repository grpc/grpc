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
  grpc_event_finish_func on_finish;
  void *on_finish_user_data;
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
  /* the set of low level i/o things that concern this cq */
  grpc_pollset pollset;
  /* 0 initially, 1 once we've begun shutting down */
  int shutdown;
  int shutdown_called;
  /* Head of a linked list of queued events (prev points to the last element) */
  event *queue;
  /* Fixed size chained hash table of events for pluck() */
  event *buckets[NUM_TAG_BUCKETS];

#ifndef NDEBUG
  /* Debug support: track which operations are in flight at any given time */
  gpr_atm pending_op_count[GRPC_COMPLETION_DO_NOT_USE];
#endif
};

/* Default do-nothing on_finish function */
static void null_on_finish(void *user_data, grpc_op_error error) {}

grpc_completion_queue *grpc_completion_queue_create(void) {
  grpc_completion_queue *cc = gpr_malloc(sizeof(grpc_completion_queue));
  memset(cc, 0, sizeof(*cc));
  /* Initial ref is dropped by grpc_completion_queue_shutdown */
  gpr_ref_init(&cc->refs, 1);
  grpc_pollset_init(&cc->pollset);
  cc->allow_polling = 1;
  return cc;
}

void grpc_completion_queue_dont_poll_test_only(grpc_completion_queue *cc) {
  cc->allow_polling = 0;
}

/* Create and append an event to the queue. Returns the event so that its data
   members can be filled in.
   Requires GRPC_POLLSET_MU(&cc->pollset) locked. */
static event *add_locked(grpc_completion_queue *cc, grpc_completion_type type,
                         void *tag, grpc_call *call,
                         grpc_event_finish_func on_finish, void *user_data) {
  event *ev = gpr_malloc(sizeof(event));
  gpr_uintptr bucket = ((gpr_uintptr)tag) % NUM_TAG_BUCKETS;
  ev->base.type = type;
  ev->base.tag = tag;
  ev->base.call = call;
  ev->on_finish = on_finish ? on_finish : null_on_finish;
  ev->on_finish_user_data = user_data;
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

void grpc_cq_begin_op(grpc_completion_queue *cc, grpc_call *call,
                      grpc_completion_type type) {
  gpr_ref(&cc->refs);
  if (call) grpc_call_internal_ref(call);
#ifndef NDEBUG
  gpr_atm_no_barrier_fetch_add(&cc->pending_op_count[type], 1);
#endif
}

/* Signal the end of an operation - if this is the last waiting-to-be-queued
   event, then enter shutdown mode */
static void end_op_locked(grpc_completion_queue *cc,
                          grpc_completion_type type) {
#ifndef NDEBUG
  GPR_ASSERT(gpr_atm_full_fetch_add(&cc->pending_op_count[type], -1) > 0);
#endif
  if (gpr_unref(&cc->refs)) {
    GPR_ASSERT(!cc->shutdown);
    GPR_ASSERT(cc->shutdown_called);
    cc->shutdown = 1;
    gpr_cv_broadcast(GRPC_POLLSET_CV(&cc->pollset));
  }
}

void grpc_cq_end_server_shutdown(grpc_completion_queue *cc, void *tag) {
  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
  add_locked(cc, GRPC_SERVER_SHUTDOWN, tag, NULL, NULL, NULL);
  end_op_locked(cc, GRPC_SERVER_SHUTDOWN);
  gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
}

void grpc_cq_end_read(grpc_completion_queue *cc, void *tag, grpc_call *call,
                      grpc_event_finish_func on_finish, void *user_data,
                      grpc_byte_buffer *read) {
  event *ev;
  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
  ev = add_locked(cc, GRPC_READ, tag, call, on_finish, user_data);
  ev->base.data.read = read;
  end_op_locked(cc, GRPC_READ);
  gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
}

void grpc_cq_end_write_accepted(grpc_completion_queue *cc, void *tag,
                                grpc_call *call,
                                grpc_event_finish_func on_finish,
                                void *user_data, grpc_op_error error) {
  event *ev;
  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
  ev = add_locked(cc, GRPC_WRITE_ACCEPTED, tag, call, on_finish, user_data);
  ev->base.data.write_accepted = error;
  end_op_locked(cc, GRPC_WRITE_ACCEPTED);
  gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
}

void grpc_cq_end_op_complete(grpc_completion_queue *cc, void *tag,
                             grpc_call *call, grpc_event_finish_func on_finish,
                             void *user_data, grpc_op_error error) {
  event *ev;
  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
  ev = add_locked(cc, GRPC_OP_COMPLETE, tag, call, on_finish, user_data);
  ev->base.data.write_accepted = error;
  end_op_locked(cc, GRPC_OP_COMPLETE);
  gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
}

void grpc_cq_end_op(grpc_completion_queue *cc, void *tag, grpc_call *call,
                    grpc_event_finish_func on_finish, void *user_data,
                    grpc_op_error error) {
  event *ev;
  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
  ev = add_locked(cc, GRPC_OP_COMPLETE, tag, call, on_finish, user_data);
  ev->base.data.write_accepted = error;
  end_op_locked(cc, GRPC_OP_COMPLETE);
  gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
}

void grpc_cq_end_finish_accepted(grpc_completion_queue *cc, void *tag,
                                 grpc_call *call,
                                 grpc_event_finish_func on_finish,
                                 void *user_data, grpc_op_error error) {
  event *ev;
  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
  ev = add_locked(cc, GRPC_FINISH_ACCEPTED, tag, call, on_finish, user_data);
  ev->base.data.finish_accepted = error;
  end_op_locked(cc, GRPC_FINISH_ACCEPTED);
  gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
}

void grpc_cq_end_client_metadata_read(grpc_completion_queue *cc, void *tag,
                                      grpc_call *call,
                                      grpc_event_finish_func on_finish,
                                      void *user_data, size_t count,
                                      grpc_metadata *elements) {
  event *ev;
  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
  ev = add_locked(cc, GRPC_CLIENT_METADATA_READ, tag, call, on_finish,
                  user_data);
  ev->base.data.client_metadata_read.count = count;
  ev->base.data.client_metadata_read.elements = elements;
  end_op_locked(cc, GRPC_CLIENT_METADATA_READ);
  gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
}

void grpc_cq_end_finished(grpc_completion_queue *cc, void *tag, grpc_call *call,
                          grpc_event_finish_func on_finish, void *user_data,
                          grpc_status_code status, const char *details,
                          grpc_metadata *metadata_elements,
                          size_t metadata_count) {
  event *ev;
  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
  ev = add_locked(cc, GRPC_FINISHED, tag, call, on_finish, user_data);
  ev->base.data.finished.status = status;
  ev->base.data.finished.details = details;
  ev->base.data.finished.metadata_count = metadata_count;
  ev->base.data.finished.metadata_elements = metadata_elements;
  end_op_locked(cc, GRPC_FINISHED);
  gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
}

void grpc_cq_end_new_rpc(grpc_completion_queue *cc, void *tag, grpc_call *call,
                         grpc_event_finish_func on_finish, void *user_data,
                         const char *method, const char *host,
                         gpr_timespec deadline, size_t metadata_count,
                         grpc_metadata *metadata_elements) {
  event *ev;
  gpr_mu_lock(GRPC_POLLSET_MU(&cc->pollset));
  ev = add_locked(cc, GRPC_SERVER_RPC_NEW, tag, call, on_finish, user_data);
  ev->base.data.server_rpc_new.method = method;
  ev->base.data.server_rpc_new.host = host;
  ev->base.data.server_rpc_new.deadline = deadline;
  ev->base.data.server_rpc_new.metadata_count = metadata_count;
  ev->base.data.server_rpc_new.metadata_elements = metadata_elements;
  end_op_locked(cc, GRPC_SERVER_RPC_NEW);
  gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
}

/* Create a GRPC_QUEUE_SHUTDOWN event without queuing it anywhere */
static event *create_shutdown_event(void) {
  event *ev = gpr_malloc(sizeof(event));
  ev->base.type = GRPC_QUEUE_SHUTDOWN;
  ev->base.call = NULL;
  ev->base.tag = NULL;
  ev->on_finish = null_on_finish;
  return ev;
}

grpc_event *grpc_completion_queue_next(grpc_completion_queue *cc,
                                       gpr_timespec deadline) {
  event *ev = NULL;

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
      return NULL;
    }
  }
  gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
  GRPC_SURFACE_TRACE_RETURNED_EVENT(cc, &ev->base);
  return &ev->base;
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

grpc_event *grpc_completion_queue_pluck(grpc_completion_queue *cc, void *tag,
                                        gpr_timespec deadline) {
  event *ev = NULL;

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
      return NULL;
    }
  }
  gpr_mu_unlock(GRPC_POLLSET_MU(&cc->pollset));
  GRPC_SURFACE_TRACE_RETURNED_EVENT(cc, &ev->base);
  return &ev->base;
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

static void on_pollset_destroy_done(void *arg) {
  grpc_completion_queue *cc = arg;
  grpc_pollset_destroy(&cc->pollset);
  gpr_free(cc);
}

void grpc_completion_queue_destroy(grpc_completion_queue *cc) {
  GPR_ASSERT(cc->queue == NULL);
  grpc_pollset_shutdown(&cc->pollset, on_pollset_destroy_done, cc);
}

void grpc_event_finish(grpc_event *base) {
  event *ev = (event *)base;
  ev->on_finish(ev->on_finish_user_data, GRPC_OP_OK);
  if (ev->base.call) {
    grpc_call_internal_unref(ev->base.call, 1);
  }
  gpr_free(ev);
}

void grpc_cq_dump_pending_ops(grpc_completion_queue *cc) {
#ifndef NDEBUG
  char tmp[GRPC_COMPLETION_DO_NOT_USE * (1 + GPR_LTOA_MIN_BUFSIZE)];
  char *p = tmp;
  int i;

  for (i = 0; i < GRPC_COMPLETION_DO_NOT_USE; i++) {
    *p++ = ' ';
    p += gpr_ltoa(cc->pending_op_count[i], p);
  }

  gpr_log(GPR_INFO, "pending ops:%s", tmp);
#endif
}

grpc_pollset *grpc_cq_pollset(grpc_completion_queue *cc) {
  return &cc->pollset;
}
