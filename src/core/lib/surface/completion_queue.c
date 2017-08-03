/*
 *
 * Copyright 2015-2016 gRPC authors.
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
#include "src/core/lib/support/spinlock.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/event_string.h"

grpc_tracer_flag grpc_trace_operation_failures =
    GRPC_TRACER_INITIALIZER(false, "op_failure");
#ifndef NDEBUG
grpc_tracer_flag grpc_trace_pending_tags =
    GRPC_TRACER_INITIALIZER(false, "pending_tags");
grpc_tracer_flag grpc_trace_cq_refcount =
    GRPC_TRACER_INITIALIZER(false, "cq_refcount");
#endif

typedef struct {
  grpc_pollset_worker **worker;
  void *tag;
} plucker;

typedef struct {
  bool can_get_pollset;
  bool can_listen;
  size_t (*size)(void);
  void (*init)(grpc_pollset *pollset, gpr_mu **mu);
  grpc_error *(*kick)(grpc_pollset *pollset,
                      grpc_pollset_worker *specific_worker);
  grpc_error *(*work)(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                      grpc_pollset_worker **worker, gpr_timespec now,
                      gpr_timespec deadline);
  void (*shutdown)(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                   grpc_closure *closure);
  void (*destroy)(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset);
} cq_poller_vtable;

typedef struct non_polling_worker {
  gpr_cv cv;
  bool kicked;
  struct non_polling_worker *next;
  struct non_polling_worker *prev;
} non_polling_worker;

typedef struct {
  gpr_mu mu;
  non_polling_worker *root;
  grpc_closure *shutdown;
} non_polling_poller;

static size_t non_polling_poller_size(void) {
  return sizeof(non_polling_poller);
}

static void non_polling_poller_init(grpc_pollset *pollset, gpr_mu **mu) {
  non_polling_poller *npp = (non_polling_poller *)pollset;
  gpr_mu_init(&npp->mu);
  *mu = &npp->mu;
}

static void non_polling_poller_destroy(grpc_exec_ctx *exec_ctx,
                                       grpc_pollset *pollset) {
  non_polling_poller *npp = (non_polling_poller *)pollset;
  gpr_mu_destroy(&npp->mu);
}

static grpc_error *non_polling_poller_work(grpc_exec_ctx *exec_ctx,
                                           grpc_pollset *pollset,
                                           grpc_pollset_worker **worker,
                                           gpr_timespec now,
                                           gpr_timespec deadline) {
  non_polling_poller *npp = (non_polling_poller *)pollset;
  if (npp->shutdown) return GRPC_ERROR_NONE;
  non_polling_worker w;
  gpr_cv_init(&w.cv);
  if (worker != NULL) *worker = (grpc_pollset_worker *)&w;
  if (npp->root == NULL) {
    npp->root = w.next = w.prev = &w;
  } else {
    w.next = npp->root;
    w.prev = w.next->prev;
    w.next->prev = w.prev->next = &w;
  }
  w.kicked = false;
  while (!npp->shutdown && !w.kicked && !gpr_cv_wait(&w.cv, &npp->mu, deadline))
    ;
  if (&w == npp->root) {
    npp->root = w.next;
    if (&w == npp->root) {
      if (npp->shutdown) {
        GRPC_CLOSURE_SCHED(exec_ctx, npp->shutdown, GRPC_ERROR_NONE);
      }
      npp->root = NULL;
    }
  }
  w.next->prev = w.prev;
  w.prev->next = w.next;
  gpr_cv_destroy(&w.cv);
  if (worker != NULL) *worker = NULL;
  return GRPC_ERROR_NONE;
}

static grpc_error *non_polling_poller_kick(
    grpc_pollset *pollset, grpc_pollset_worker *specific_worker) {
  non_polling_poller *p = (non_polling_poller *)pollset;
  if (specific_worker == NULL) specific_worker = (grpc_pollset_worker *)p->root;
  if (specific_worker != NULL) {
    non_polling_worker *w = (non_polling_worker *)specific_worker;
    if (!w->kicked) {
      w->kicked = true;
      gpr_cv_signal(&w->cv);
    }
  }
  return GRPC_ERROR_NONE;
}

static void non_polling_poller_shutdown(grpc_exec_ctx *exec_ctx,
                                        grpc_pollset *pollset,
                                        grpc_closure *closure) {
  non_polling_poller *p = (non_polling_poller *)pollset;
  GPR_ASSERT(closure != NULL);
  p->shutdown = closure;
  if (p->root == NULL) {
    GRPC_CLOSURE_SCHED(exec_ctx, closure, GRPC_ERROR_NONE);
  } else {
    non_polling_worker *w = p->root;
    do {
      gpr_cv_signal(&w->cv);
      w = w->next;
    } while (w != p->root);
  }
}

static const cq_poller_vtable g_poller_vtable_by_poller_type[] = {
    /* GRPC_CQ_DEFAULT_POLLING */
    {.can_get_pollset = true,
     .can_listen = true,
     .size = grpc_pollset_size,
     .init = grpc_pollset_init,
     .kick = grpc_pollset_kick,
     .work = grpc_pollset_work,
     .shutdown = grpc_pollset_shutdown,
     .destroy = grpc_pollset_destroy},
    /* GRPC_CQ_NON_LISTENING */
    {.can_get_pollset = true,
     .can_listen = false,
     .size = grpc_pollset_size,
     .init = grpc_pollset_init,
     .kick = grpc_pollset_kick,
     .work = grpc_pollset_work,
     .shutdown = grpc_pollset_shutdown,
     .destroy = grpc_pollset_destroy},
    /* GRPC_CQ_NON_POLLING */
    {.can_get_pollset = false,
     .can_listen = false,
     .size = non_polling_poller_size,
     .init = non_polling_poller_init,
     .kick = non_polling_poller_kick,
     .work = non_polling_poller_work,
     .shutdown = non_polling_poller_shutdown,
     .destroy = non_polling_poller_destroy},
};

typedef struct cq_vtable {
  grpc_cq_completion_type cq_completion_type;
  size_t data_size;
  void (*init)(void *data);
  void (*shutdown)(grpc_exec_ctx *exec_ctx, grpc_completion_queue *cq);
  void (*destroy)(void *data);
  bool (*begin_op)(grpc_completion_queue *cq, void *tag);
  void (*end_op)(grpc_exec_ctx *exec_ctx, grpc_completion_queue *cq, void *tag,
                 grpc_error *error,
                 void (*done)(grpc_exec_ctx *exec_ctx, void *done_arg,
                              grpc_cq_completion *storage),
                 void *done_arg, grpc_cq_completion *storage);
  grpc_event (*next)(grpc_completion_queue *cq, gpr_timespec deadline,
                     void *reserved);
  grpc_event (*pluck)(grpc_completion_queue *cq, void *tag,
                      gpr_timespec deadline, void *reserved);
} cq_vtable;

/* Queue that holds the cq_completion_events. Internally uses gpr_mpscq queue
 * (a lockfree multiproducer single consumer queue). It uses a queue_lock
 * to support multiple consumers.
 * Only used in completion queues whose completion_type is GRPC_CQ_NEXT */
typedef struct grpc_cq_event_queue {
  /* Spinlock to serialize consumers i.e pop() operations */
  gpr_spinlock queue_lock;

  gpr_mpscq queue;

  /* A lazy counter of number of items in the queue. This is NOT atomically
     incremented/decremented along with push/pop operations and hence is only
     eventually consistent */
  gpr_atm num_queue_items;
} grpc_cq_event_queue;

typedef struct cq_next_data {
  /** Completed events for completion-queues of type GRPC_CQ_NEXT */
  grpc_cq_event_queue queue;

  /** Counter of how many things have ever been queued on this completion queue
      useful for avoiding locks to check the queue */
  gpr_atm things_queued_ever;

  /* Number of outstanding events (+1 if not shut down) */
  gpr_atm pending_events;

  /** 0 initially. 1 once we initiated shutdown */
  bool shutdown_called;
} cq_next_data;

typedef struct cq_pluck_data {
  /** Completed events for completion-queues of type GRPC_CQ_PLUCK */
  grpc_cq_completion completed_head;
  grpc_cq_completion *completed_tail;

  /** Number of pending events (+1 if we're not shutdown) */
  gpr_atm pending_events;

  /** Counter of how many things have ever been queued on this completion queue
      useful for avoiding locks to check the queue */
  gpr_atm things_queued_ever;

  /** 0 initially. 1 once we completed shutting */
  /* TODO: (sreek) This is not needed since (shutdown == 1) if and only if
   * (pending_events == 0). So consider removing this in future and use
   * pending_events */
  gpr_atm shutdown;

  /** 0 initially. 1 once we initiated shutdown */
  bool shutdown_called;

  int num_pluckers;
  plucker pluckers[GRPC_MAX_COMPLETION_QUEUE_PLUCKERS];
} cq_pluck_data;

/* Completion queue structure */
struct grpc_completion_queue {
  /** Once owning_refs drops to zero, we will destroy the cq */
  gpr_refcount owning_refs;

  gpr_mu *mu;

  const cq_vtable *vtable;
  const cq_poller_vtable *poller_vtable;

#ifndef NDEBUG
  void **outstanding_tags;
  size_t outstanding_tag_count;
  size_t outstanding_tag_capacity;
#endif

  grpc_closure pollset_shutdown_done;
  int num_polls;
};

/* Forward declarations */
static void cq_finish_shutdown_next(grpc_exec_ctx *exec_ctx,
                                    grpc_completion_queue *cq);
static void cq_finish_shutdown_pluck(grpc_exec_ctx *exec_ctx,
                                     grpc_completion_queue *cq);
static void cq_shutdown_next(grpc_exec_ctx *exec_ctx,
                             grpc_completion_queue *cq);
static void cq_shutdown_pluck(grpc_exec_ctx *exec_ctx,
                              grpc_completion_queue *cq);

static bool cq_begin_op_for_next(grpc_completion_queue *cq, void *tag);
static bool cq_begin_op_for_pluck(grpc_completion_queue *cq, void *tag);

static void cq_end_op_for_next(grpc_exec_ctx *exec_ctx,
                               grpc_completion_queue *cq, void *tag,
                               grpc_error *error,
                               void (*done)(grpc_exec_ctx *exec_ctx,
                                            void *done_arg,
                                            grpc_cq_completion *storage),
                               void *done_arg, grpc_cq_completion *storage);

static void cq_end_op_for_pluck(grpc_exec_ctx *exec_ctx,
                                grpc_completion_queue *cq, void *tag,
                                grpc_error *error,
                                void (*done)(grpc_exec_ctx *exec_ctx,
                                             void *done_arg,
                                             grpc_cq_completion *storage),
                                void *done_arg, grpc_cq_completion *storage);

static grpc_event cq_next(grpc_completion_queue *cq, gpr_timespec deadline,
                          void *reserved);

static grpc_event cq_pluck(grpc_completion_queue *cq, void *tag,
                           gpr_timespec deadline, void *reserved);

static void cq_init_next(void *data);
static void cq_init_pluck(void *data);
static void cq_destroy_next(void *data);
static void cq_destroy_pluck(void *data);

/* Completion queue vtables based on the completion-type */
static const cq_vtable g_cq_vtable[] = {
    /* GRPC_CQ_NEXT */
    {.data_size = sizeof(cq_next_data),
     .cq_completion_type = GRPC_CQ_NEXT,
     .init = cq_init_next,
     .shutdown = cq_shutdown_next,
     .destroy = cq_destroy_next,
     .begin_op = cq_begin_op_for_next,
     .end_op = cq_end_op_for_next,
     .next = cq_next,
     .pluck = NULL},
    /* GRPC_CQ_PLUCK */
    {.data_size = sizeof(cq_pluck_data),
     .cq_completion_type = GRPC_CQ_PLUCK,
     .init = cq_init_pluck,
     .shutdown = cq_shutdown_pluck,
     .destroy = cq_destroy_pluck,
     .begin_op = cq_begin_op_for_pluck,
     .end_op = cq_end_op_for_pluck,
     .next = NULL,
     .pluck = cq_pluck},
};

#define DATA_FROM_CQ(cq) ((void *)(cq + 1))
#define POLLSET_FROM_CQ(cq) \
  ((grpc_pollset *)(cq->vtable->data_size + (char *)DATA_FROM_CQ(cq)))

grpc_tracer_flag grpc_cq_pluck_trace =
    GRPC_TRACER_INITIALIZER(true, "queue_pluck");
grpc_tracer_flag grpc_cq_event_timeout_trace =
    GRPC_TRACER_INITIALIZER(true, "queue_timeout");

#define GRPC_SURFACE_TRACE_RETURNED_EVENT(cq, event)    \
  if (GRPC_TRACER_ON(grpc_api_trace) &&                 \
      (GRPC_TRACER_ON(grpc_cq_pluck_trace) ||           \
       (event)->type != GRPC_QUEUE_TIMEOUT)) {          \
    char *_ev = grpc_event_string(event);               \
    gpr_log(GPR_INFO, "RETURN_EVENT[%p]: %s", cq, _ev); \
    gpr_free(_ev);                                      \
  }

static void on_pollset_shutdown_done(grpc_exec_ctx *exec_ctx, void *cq,
                                     grpc_error *error);

static void cq_event_queue_init(grpc_cq_event_queue *q) {
  gpr_mpscq_init(&q->queue);
  q->queue_lock = GPR_SPINLOCK_INITIALIZER;
  gpr_atm_no_barrier_store(&q->num_queue_items, 0);
}

static void cq_event_queue_destroy(grpc_cq_event_queue *q) {
  gpr_mpscq_destroy(&q->queue);
}

static bool cq_event_queue_push(grpc_cq_event_queue *q, grpc_cq_completion *c) {
  gpr_mpscq_push(&q->queue, (gpr_mpscq_node *)c);
  return gpr_atm_no_barrier_fetch_add(&q->num_queue_items, 1) == 0;
}

static grpc_cq_completion *cq_event_queue_pop(grpc_cq_event_queue *q) {
  grpc_cq_completion *c = NULL;
  if (gpr_spinlock_trylock(&q->queue_lock)) {
    c = (grpc_cq_completion *)gpr_mpscq_pop(&q->queue);
    gpr_spinlock_unlock(&q->queue_lock);
  }

  if (c) {
    gpr_atm_no_barrier_fetch_add(&q->num_queue_items, -1);
  }

  return c;
}

/* Note: The counter is not incremented/decremented atomically with push/pop.
 * The count is only eventually consistent */
static long cq_event_queue_num_items(grpc_cq_event_queue *q) {
  return (long)gpr_atm_no_barrier_load(&q->num_queue_items);
}

grpc_completion_queue *grpc_completion_queue_create_internal(
    grpc_cq_completion_type completion_type,
    grpc_cq_polling_type polling_type) {
  grpc_completion_queue *cq;

  GPR_TIMER_BEGIN("grpc_completion_queue_create_internal", 0);

  GRPC_API_TRACE(
      "grpc_completion_queue_create_internal(completion_type=%d, "
      "polling_type=%d)",
      2, (completion_type, polling_type));

  const cq_vtable *vtable = &g_cq_vtable[completion_type];
  const cq_poller_vtable *poller_vtable =
      &g_poller_vtable_by_poller_type[polling_type];

  cq = gpr_zalloc(sizeof(grpc_completion_queue) + vtable->data_size +
                  poller_vtable->size());

  cq->vtable = vtable;
  cq->poller_vtable = poller_vtable;

  /* One for destroy(), one for pollset_shutdown */
  gpr_ref_init(&cq->owning_refs, 2);

  poller_vtable->init(POLLSET_FROM_CQ(cq), &cq->mu);
  vtable->init(DATA_FROM_CQ(cq));

  GRPC_CLOSURE_INIT(&cq->pollset_shutdown_done, on_pollset_shutdown_done, cq,
                    grpc_schedule_on_exec_ctx);

  GPR_TIMER_END("grpc_completion_queue_create_internal", 0);

  return cq;
}

static void cq_init_next(void *ptr) {
  cq_next_data *cqd = ptr;
  /* Initial count is dropped by grpc_completion_queue_shutdown */
  gpr_atm_no_barrier_store(&cqd->pending_events, 1);
  cqd->shutdown_called = false;
  gpr_atm_no_barrier_store(&cqd->things_queued_ever, 0);
  cq_event_queue_init(&cqd->queue);
}

static void cq_destroy_next(void *ptr) {
  cq_next_data *cqd = ptr;
  GPR_ASSERT(cq_event_queue_num_items(&cqd->queue) == 0);
  cq_event_queue_destroy(&cqd->queue);
}

static void cq_init_pluck(void *ptr) {
  cq_pluck_data *cqd = ptr;
  /* Initial count is dropped by grpc_completion_queue_shutdown */
  gpr_atm_no_barrier_store(&cqd->pending_events, 1);
  cqd->completed_tail = &cqd->completed_head;
  cqd->completed_head.next = (uintptr_t)cqd->completed_tail;
  gpr_atm_no_barrier_store(&cqd->shutdown, 0);
  cqd->shutdown_called = false;
  cqd->num_pluckers = 0;
  gpr_atm_no_barrier_store(&cqd->things_queued_ever, 0);
}

static void cq_destroy_pluck(void *ptr) {
  cq_pluck_data *cqd = ptr;
  GPR_ASSERT(cqd->completed_head.next == (uintptr_t)&cqd->completed_head);
}

grpc_cq_completion_type grpc_get_cq_completion_type(grpc_completion_queue *cq) {
  return cq->vtable->cq_completion_type;
}

int grpc_get_cq_poll_num(grpc_completion_queue *cq) {
  int cur_num_polls;
  gpr_mu_lock(cq->mu);
  cur_num_polls = cq->num_polls;
  gpr_mu_unlock(cq->mu);
  return cur_num_polls;
}

#ifndef NDEBUG
void grpc_cq_internal_ref(grpc_completion_queue *cq, const char *reason,
                          const char *file, int line) {
  if (GRPC_TRACER_ON(grpc_trace_cq_refcount)) {
    gpr_atm val = gpr_atm_no_barrier_load(&cq->owning_refs.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "CQ:%p   ref %" PRIdPTR " -> %" PRIdPTR " %s", cq, val, val + 1,
            reason);
  }
#else
void grpc_cq_internal_ref(grpc_completion_queue *cq) {
#endif
  gpr_ref(&cq->owning_refs);
}

static void on_pollset_shutdown_done(grpc_exec_ctx *exec_ctx, void *arg,
                                     grpc_error *error) {
  grpc_completion_queue *cq = arg;
  GRPC_CQ_INTERNAL_UNREF(exec_ctx, cq, "pollset_destroy");
}

#ifndef NDEBUG
void grpc_cq_internal_unref(grpc_exec_ctx *exec_ctx, grpc_completion_queue *cq,
                            const char *reason, const char *file, int line) {
  if (GRPC_TRACER_ON(grpc_trace_cq_refcount)) {
    gpr_atm val = gpr_atm_no_barrier_load(&cq->owning_refs.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "CQ:%p unref %" PRIdPTR " -> %" PRIdPTR " %s", cq, val, val - 1,
            reason);
  }
#else
void grpc_cq_internal_unref(grpc_exec_ctx *exec_ctx,
                            grpc_completion_queue *cq) {
#endif
  if (gpr_unref(&cq->owning_refs)) {
    cq->vtable->destroy(DATA_FROM_CQ(cq));
    cq->poller_vtable->destroy(exec_ctx, POLLSET_FROM_CQ(cq));
#ifndef NDEBUG
    gpr_free(cq->outstanding_tags);
#endif
    gpr_free(cq);
  }
}

#ifndef NDEBUG
static void cq_check_tag(grpc_completion_queue *cq, void *tag, bool lock_cq) {
  int found = 0;
  if (lock_cq) {
    gpr_mu_lock(cq->mu);
  }

  for (int i = 0; i < (int)cq->outstanding_tag_count; i++) {
    if (cq->outstanding_tags[i] == tag) {
      cq->outstanding_tag_count--;
      GPR_SWAP(void *, cq->outstanding_tags[i],
               cq->outstanding_tags[cq->outstanding_tag_count]);
      found = 1;
      break;
    }
  }

  if (lock_cq) {
    gpr_mu_unlock(cq->mu);
  }

  GPR_ASSERT(found);
}
#else
static void cq_check_tag(grpc_completion_queue *cq, void *tag, bool lock_cq) {}
#endif

/* Atomically increments a counter only if the counter is not zero. Returns
 * true if the increment was successful; false if the counter is zero */
static bool atm_inc_if_nonzero(gpr_atm *counter) {
  while (true) {
    gpr_atm count = gpr_atm_no_barrier_load(counter);
    /* If zero, we are done. If not, we must to a CAS (instead of an atomic
     * increment) to maintain the contract: do not increment the counter if it
     * is zero. */
    if (count == 0) {
      return false;
    } else if (gpr_atm_no_barrier_cas(counter, count, count + 1)) {
      break;
    }
  }

  return true;
}

static bool cq_begin_op_for_next(grpc_completion_queue *cq, void *tag) {
  cq_next_data *cqd = DATA_FROM_CQ(cq);
  return atm_inc_if_nonzero(&cqd->pending_events);
}

static bool cq_begin_op_for_pluck(grpc_completion_queue *cq, void *tag) {
  cq_pluck_data *cqd = DATA_FROM_CQ(cq);
  return atm_inc_if_nonzero(&cqd->pending_events);
}

bool grpc_cq_begin_op(grpc_completion_queue *cq, void *tag) {
#ifndef NDEBUG
  gpr_mu_lock(cq->mu);
  if (cq->outstanding_tag_count == cq->outstanding_tag_capacity) {
    cq->outstanding_tag_capacity = GPR_MAX(4, 2 * cq->outstanding_tag_capacity);
    cq->outstanding_tags =
        gpr_realloc(cq->outstanding_tags, sizeof(*cq->outstanding_tags) *
                                              cq->outstanding_tag_capacity);
  }
  cq->outstanding_tags[cq->outstanding_tag_count++] = tag;
  gpr_mu_unlock(cq->mu);
#endif
  return cq->vtable->begin_op(cq, tag);
}

/* Queue a GRPC_OP_COMPLETED operation to a completion queue (with a
 * completion
 * type of GRPC_CQ_NEXT) */
static void cq_end_op_for_next(grpc_exec_ctx *exec_ctx,
                               grpc_completion_queue *cq, void *tag,
                               grpc_error *error,
                               void (*done)(grpc_exec_ctx *exec_ctx,
                                            void *done_arg,
                                            grpc_cq_completion *storage),
                               void *done_arg, grpc_cq_completion *storage) {
  GPR_TIMER_BEGIN("cq_end_op_for_next", 0);

  if (GRPC_TRACER_ON(grpc_api_trace) ||
      (GRPC_TRACER_ON(grpc_trace_operation_failures) &&
       error != GRPC_ERROR_NONE)) {
    const char *errmsg = grpc_error_string(error);
    GRPC_API_TRACE(
        "cq_end_op_for_next(exec_ctx=%p, cq=%p, tag=%p, error=%s, "
        "done=%p, done_arg=%p, storage=%p)",
        7, (exec_ctx, cq, tag, errmsg, done, done_arg, storage));
    if (GRPC_TRACER_ON(grpc_trace_operation_failures) &&
        error != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR, "Operation failed: tag=%p, error=%s", tag, errmsg);
    }
  }

  cq_next_data *cqd = DATA_FROM_CQ(cq);
  int is_success = (error == GRPC_ERROR_NONE);

  storage->tag = tag;
  storage->done = done;
  storage->done_arg = done_arg;
  storage->next = (uintptr_t)(is_success);

  cq_check_tag(cq, tag, true); /* Used in debug builds only */

  /* Add the completion to the queue */
  bool is_first = cq_event_queue_push(&cqd->queue, storage);
  gpr_atm_no_barrier_fetch_add(&cqd->things_queued_ever, 1);
  bool will_definitely_shutdown =
      gpr_atm_no_barrier_load(&cqd->pending_events) == 1;

  if (!will_definitely_shutdown) {
    /* Only kick if this is the first item queued */
    if (is_first) {
      gpr_mu_lock(cq->mu);
      grpc_error *kick_error =
          cq->poller_vtable->kick(POLLSET_FROM_CQ(cq), NULL);
      gpr_mu_unlock(cq->mu);

      if (kick_error != GRPC_ERROR_NONE) {
        const char *msg = grpc_error_string(kick_error);
        gpr_log(GPR_ERROR, "Kick failed: %s", msg);
        GRPC_ERROR_UNREF(kick_error);
      }
    }
    if (gpr_atm_full_fetch_add(&cqd->pending_events, -1) == 1) {
      GRPC_CQ_INTERNAL_REF(cq, "shutting_down");
      gpr_mu_lock(cq->mu);
      cq_finish_shutdown_next(exec_ctx, cq);
      gpr_mu_unlock(cq->mu);
      GRPC_CQ_INTERNAL_UNREF(exec_ctx, cq, "shutting_down");
    }
  } else {
    GRPC_CQ_INTERNAL_REF(cq, "shutting_down");
    gpr_atm_rel_store(&cqd->pending_events, 0);
    gpr_mu_lock(cq->mu);
    cq_finish_shutdown_next(exec_ctx, cq);
    gpr_mu_unlock(cq->mu);
    GRPC_CQ_INTERNAL_UNREF(exec_ctx, cq, "shutting_down");
  }

  GPR_TIMER_END("cq_end_op_for_next", 0);

  GRPC_ERROR_UNREF(error);
}

/* Queue a GRPC_OP_COMPLETED operation to a completion queue (with a
 * completion
 * type of GRPC_CQ_PLUCK) */
static void cq_end_op_for_pluck(grpc_exec_ctx *exec_ctx,
                                grpc_completion_queue *cq, void *tag,
                                grpc_error *error,
                                void (*done)(grpc_exec_ctx *exec_ctx,
                                             void *done_arg,
                                             grpc_cq_completion *storage),
                                void *done_arg, grpc_cq_completion *storage) {
  cq_pluck_data *cqd = DATA_FROM_CQ(cq);
  int is_success = (error == GRPC_ERROR_NONE);

  GPR_TIMER_BEGIN("cq_end_op_for_pluck", 0);

  if (GRPC_TRACER_ON(grpc_api_trace) ||
      (GRPC_TRACER_ON(grpc_trace_operation_failures) &&
       error != GRPC_ERROR_NONE)) {
    const char *errmsg = grpc_error_string(error);
    GRPC_API_TRACE(
        "cq_end_op_for_pluck(exec_ctx=%p, cq=%p, tag=%p, error=%s, "
        "done=%p, done_arg=%p, storage=%p)",
        7, (exec_ctx, cq, tag, errmsg, done, done_arg, storage));
    if (GRPC_TRACER_ON(grpc_trace_operation_failures) &&
        error != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR, "Operation failed: tag=%p, error=%s", tag, errmsg);
    }
  }

  storage->tag = tag;
  storage->done = done;
  storage->done_arg = done_arg;
  storage->next = ((uintptr_t)&cqd->completed_head) | ((uintptr_t)(is_success));

  gpr_mu_lock(cq->mu);
  cq_check_tag(cq, tag, false); /* Used in debug builds only */

  /* Add to the list of completions */
  gpr_atm_no_barrier_fetch_add(&cqd->things_queued_ever, 1);
  cqd->completed_tail->next =
      ((uintptr_t)storage) | (1u & (uintptr_t)cqd->completed_tail->next);
  cqd->completed_tail = storage;

  if (gpr_atm_full_fetch_add(&cqd->pending_events, -1) == 1) {
    cq_finish_shutdown_pluck(exec_ctx, cq);
    gpr_mu_unlock(cq->mu);
  } else {
    grpc_pollset_worker *pluck_worker = NULL;
    for (int i = 0; i < cqd->num_pluckers; i++) {
      if (cqd->pluckers[i].tag == tag) {
        pluck_worker = *cqd->pluckers[i].worker;
        break;
      }
    }

    grpc_error *kick_error =
        cq->poller_vtable->kick(POLLSET_FROM_CQ(cq), pluck_worker);

    gpr_mu_unlock(cq->mu);

    if (kick_error != GRPC_ERROR_NONE) {
      const char *msg = grpc_error_string(kick_error);
      gpr_log(GPR_ERROR, "Kick failed: %s", msg);

      GRPC_ERROR_UNREF(kick_error);
    }
  }

  GPR_TIMER_END("cq_end_op_for_pluck", 0);

  GRPC_ERROR_UNREF(error);
}

void grpc_cq_end_op(grpc_exec_ctx *exec_ctx, grpc_completion_queue *cq,
                    void *tag, grpc_error *error,
                    void (*done)(grpc_exec_ctx *exec_ctx, void *done_arg,
                                 grpc_cq_completion *storage),
                    void *done_arg, grpc_cq_completion *storage) {
  cq->vtable->end_op(exec_ctx, cq, tag, error, done, done_arg, storage);
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
  cq_next_data *cqd = DATA_FROM_CQ(cq);
  GPR_ASSERT(a->stolen_completion == NULL);

  gpr_atm current_last_seen_things_queued_ever =
      gpr_atm_no_barrier_load(&cqd->things_queued_ever);

  if (current_last_seen_things_queued_ever != a->last_seen_things_queued_ever) {
    a->last_seen_things_queued_ever =
        gpr_atm_no_barrier_load(&cqd->things_queued_ever);

    /* Pop a cq_completion from the queue. Returns NULL if the queue is empty
     * might return NULL in some cases even if the queue is not empty; but
     * that
     * is ok and doesn't affect correctness. Might effect the tail latencies a
     * bit) */
    a->stolen_completion = cq_event_queue_pop(&cqd->queue);
    if (a->stolen_completion != NULL) {
      return true;
    }
  }
  return !a->first_loop &&
         gpr_time_cmp(a->deadline, gpr_now(a->deadline.clock_type)) < 0;
}

#ifndef NDEBUG
static void dump_pending_tags(grpc_completion_queue *cq) {
  if (!GRPC_TRACER_ON(grpc_trace_pending_tags)) return;

  gpr_strvec v;
  gpr_strvec_init(&v);
  gpr_strvec_add(&v, gpr_strdup("PENDING TAGS:"));
  gpr_mu_lock(cq->mu);
  for (size_t i = 0; i < cq->outstanding_tag_count; i++) {
    char *s;
    gpr_asprintf(&s, " %p", cq->outstanding_tags[i]);
    gpr_strvec_add(&v, s);
  }
  gpr_mu_unlock(cq->mu);
  char *out = gpr_strvec_flatten(&v, NULL);
  gpr_strvec_destroy(&v);
  gpr_log(GPR_DEBUG, "%s", out);
  gpr_free(out);
}
#else
static void dump_pending_tags(grpc_completion_queue *cq) {}
#endif

static grpc_event cq_next(grpc_completion_queue *cq, gpr_timespec deadline,
                          void *reserved) {
  grpc_event ret;
  gpr_timespec now;
  cq_next_data *cqd = DATA_FROM_CQ(cq);

  GPR_TIMER_BEGIN("grpc_completion_queue_next", 0);

  GRPC_API_TRACE(
      "grpc_completion_queue_next("
      "cq=%p, "
      "deadline=gpr_timespec { tv_sec: %" PRId64
      ", tv_nsec: %d, clock_type: %d }, "
      "reserved=%p)",
      5, (cq, deadline.tv_sec, deadline.tv_nsec, (int)deadline.clock_type,
          reserved));
  GPR_ASSERT(!reserved);

  dump_pending_tags(cq);

  deadline = gpr_convert_clock_type(deadline, GPR_CLOCK_MONOTONIC);

  GRPC_CQ_INTERNAL_REF(cq, "next");

  cq_is_finished_arg is_finished_arg = {
      .last_seen_things_queued_ever =
          gpr_atm_no_barrier_load(&cqd->things_queued_ever),
      .cq = cq,
      .deadline = deadline,
      .stolen_completion = NULL,
      .tag = NULL,
      .first_loop = true};
  grpc_exec_ctx exec_ctx =
      GRPC_EXEC_CTX_INITIALIZER(0, cq_is_next_finished, &is_finished_arg);

  for (;;) {
    gpr_timespec iteration_deadline = deadline;

    if (is_finished_arg.stolen_completion != NULL) {
      grpc_cq_completion *c = is_finished_arg.stolen_completion;
      is_finished_arg.stolen_completion = NULL;
      ret.type = GRPC_OP_COMPLETE;
      ret.success = c->next & 1u;
      ret.tag = c->tag;
      c->done(&exec_ctx, c->done_arg, c);
      break;
    }

    grpc_cq_completion *c = cq_event_queue_pop(&cqd->queue);

    if (c != NULL) {
      ret.type = GRPC_OP_COMPLETE;
      ret.success = c->next & 1u;
      ret.tag = c->tag;
      c->done(&exec_ctx, c->done_arg, c);
      break;
    } else {
      /* If c == NULL it means either the queue is empty OR in an transient
         inconsistent state. If it is the latter, we shold do a 0-timeout poll
         so that the thread comes back quickly from poll to make a second
         attempt at popping. Not doing this can potentially deadlock this
         thread forever (if the deadline is infinity) */
      if (cq_event_queue_num_items(&cqd->queue) > 0) {
        iteration_deadline = gpr_time_0(GPR_CLOCK_MONOTONIC);
      }
    }

    if (gpr_atm_no_barrier_load(&cqd->pending_events) == 0) {
      /* Before returning, check if the queue has any items left over (since
         gpr_mpscq_pop() can sometimes return NULL even if the queue is not
         empty. If so, keep retrying but do not return GRPC_QUEUE_SHUTDOWN */
      if (cq_event_queue_num_items(&cqd->queue) > 0) {
        /* Go to the beginning of the loop. No point doing a poll because
           (cq->shutdown == true) is only possible when there is no pending
           work (i.e cq->pending_events == 0) and any outstanding completion
           events should have already been queued on this cq */
        continue;
      }

      memset(&ret, 0, sizeof(ret));
      ret.type = GRPC_QUEUE_SHUTDOWN;
      break;
    }

    now = gpr_now(GPR_CLOCK_MONOTONIC);
    if (!is_finished_arg.first_loop && gpr_time_cmp(now, deadline) >= 0) {
      memset(&ret, 0, sizeof(ret));
      ret.type = GRPC_QUEUE_TIMEOUT;
      dump_pending_tags(cq);
      break;
    }

    /* The main polling work happens in grpc_pollset_work */
    gpr_mu_lock(cq->mu);
    cq->num_polls++;
    grpc_error *err = cq->poller_vtable->work(&exec_ctx, POLLSET_FROM_CQ(cq),
                                              NULL, now, iteration_deadline);
    gpr_mu_unlock(cq->mu);

    if (err != GRPC_ERROR_NONE) {
      const char *msg = grpc_error_string(err);
      gpr_log(GPR_ERROR, "Completion queue next failed: %s", msg);

      GRPC_ERROR_UNREF(err);
      memset(&ret, 0, sizeof(ret));
      ret.type = GRPC_QUEUE_TIMEOUT;
      dump_pending_tags(cq);
      break;
    }
    is_finished_arg.first_loop = false;
  }

  if (cq_event_queue_num_items(&cqd->queue) > 0 &&
      gpr_atm_no_barrier_load(&cqd->pending_events) > 0) {
    gpr_mu_lock(cq->mu);
    cq->poller_vtable->kick(POLLSET_FROM_CQ(cq), NULL);
    gpr_mu_unlock(cq->mu);
  }

  GRPC_SURFACE_TRACE_RETURNED_EVENT(cq, &ret);
  GRPC_CQ_INTERNAL_UNREF(&exec_ctx, cq, "next");
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_ASSERT(is_finished_arg.stolen_completion == NULL);

  GPR_TIMER_END("grpc_completion_queue_next", 0);

  return ret;
}

/* Finishes the completion queue shutdown. This means that there are no more
   completion events / tags expected from the completion queue
   - Must be called under completion queue lock
   - Must be called only once in completion queue's lifetime
   - grpc_completion_queue_shutdown() MUST have been called before calling
   this function */
static void cq_finish_shutdown_next(grpc_exec_ctx *exec_ctx,
                                    grpc_completion_queue *cq) {
  cq_next_data *cqd = DATA_FROM_CQ(cq);

  GPR_ASSERT(cqd->shutdown_called);
  GPR_ASSERT(gpr_atm_no_barrier_load(&cqd->pending_events) == 0);

  cq->poller_vtable->shutdown(exec_ctx, POLLSET_FROM_CQ(cq),
                              &cq->pollset_shutdown_done);
}

static void cq_shutdown_next(grpc_exec_ctx *exec_ctx,
                             grpc_completion_queue *cq) {
  cq_next_data *cqd = DATA_FROM_CQ(cq);

  /* Need an extra ref for cq here because:
   * We call cq_finish_shutdown_next() below, that would call pollset shutdown.
   * Pollset shutdown decrements the cq ref count which can potentially destroy
   * the cq (if that happens to be the last ref).
   * Creating an extra ref here prevents the cq from getting destroyed while
   * this function is still active */
  GRPC_CQ_INTERNAL_REF(cq, "shutting_down");
  gpr_mu_lock(cq->mu);
  if (cqd->shutdown_called) {
    gpr_mu_unlock(cq->mu);
    GRPC_CQ_INTERNAL_UNREF(exec_ctx, cq, "shutting_down");
    GPR_TIMER_END("grpc_completion_queue_shutdown", 0);
    return;
  }
  cqd->shutdown_called = true;
  if (gpr_atm_full_fetch_add(&cqd->pending_events, -1) == 1) {
    cq_finish_shutdown_next(exec_ctx, cq);
  }
  gpr_mu_unlock(cq->mu);
  GRPC_CQ_INTERNAL_UNREF(exec_ctx, cq, "shutting_down");
}

grpc_event grpc_completion_queue_next(grpc_completion_queue *cq,
                                      gpr_timespec deadline, void *reserved) {
  return cq->vtable->next(cq, deadline, reserved);
}

static int add_plucker(grpc_completion_queue *cq, void *tag,
                       grpc_pollset_worker **worker) {
  cq_pluck_data *cqd = DATA_FROM_CQ(cq);
  if (cqd->num_pluckers == GRPC_MAX_COMPLETION_QUEUE_PLUCKERS) {
    return 0;
  }
  cqd->pluckers[cqd->num_pluckers].tag = tag;
  cqd->pluckers[cqd->num_pluckers].worker = worker;
  cqd->num_pluckers++;
  return 1;
}

static void del_plucker(grpc_completion_queue *cq, void *tag,
                        grpc_pollset_worker **worker) {
  cq_pluck_data *cqd = DATA_FROM_CQ(cq);
  for (int i = 0; i < cqd->num_pluckers; i++) {
    if (cqd->pluckers[i].tag == tag && cqd->pluckers[i].worker == worker) {
      cqd->num_pluckers--;
      GPR_SWAP(plucker, cqd->pluckers[i], cqd->pluckers[cqd->num_pluckers]);
      return;
    }
  }
  GPR_UNREACHABLE_CODE(return );
}

static bool cq_is_pluck_finished(grpc_exec_ctx *exec_ctx, void *arg) {
  cq_is_finished_arg *a = arg;
  grpc_completion_queue *cq = a->cq;
  cq_pluck_data *cqd = DATA_FROM_CQ(cq);

  GPR_ASSERT(a->stolen_completion == NULL);
  gpr_atm current_last_seen_things_queued_ever =
      gpr_atm_no_barrier_load(&cqd->things_queued_ever);
  if (current_last_seen_things_queued_ever != a->last_seen_things_queued_ever) {
    gpr_mu_lock(cq->mu);
    a->last_seen_things_queued_ever =
        gpr_atm_no_barrier_load(&cqd->things_queued_ever);
    grpc_cq_completion *c;
    grpc_cq_completion *prev = &cqd->completed_head;
    while ((c = (grpc_cq_completion *)(prev->next & ~(uintptr_t)1)) !=
           &cqd->completed_head) {
      if (c->tag == a->tag) {
        prev->next = (prev->next & (uintptr_t)1) | (c->next & ~(uintptr_t)1);
        if (c == cqd->completed_tail) {
          cqd->completed_tail = prev;
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

static grpc_event cq_pluck(grpc_completion_queue *cq, void *tag,
                           gpr_timespec deadline, void *reserved) {
  grpc_event ret;
  grpc_cq_completion *c;
  grpc_cq_completion *prev;
  grpc_pollset_worker *worker = NULL;
  gpr_timespec now;
  cq_pluck_data *cqd = DATA_FROM_CQ(cq);

  GPR_TIMER_BEGIN("grpc_completion_queue_pluck", 0);

  if (GRPC_TRACER_ON(grpc_cq_pluck_trace)) {
    GRPC_API_TRACE(
        "grpc_completion_queue_pluck("
        "cq=%p, tag=%p, "
        "deadline=gpr_timespec { tv_sec: %" PRId64
        ", tv_nsec: %d, clock_type: %d }, "
        "reserved=%p)",
        6, (cq, tag, deadline.tv_sec, deadline.tv_nsec,
            (int)deadline.clock_type, reserved));
  }
  GPR_ASSERT(!reserved);

  dump_pending_tags(cq);

  deadline = gpr_convert_clock_type(deadline, GPR_CLOCK_MONOTONIC);

  GRPC_CQ_INTERNAL_REF(cq, "pluck");
  gpr_mu_lock(cq->mu);
  cq_is_finished_arg is_finished_arg = {
      .last_seen_things_queued_ever =
          gpr_atm_no_barrier_load(&cqd->things_queued_ever),
      .cq = cq,
      .deadline = deadline,
      .stolen_completion = NULL,
      .tag = tag,
      .first_loop = true};
  grpc_exec_ctx exec_ctx =
      GRPC_EXEC_CTX_INITIALIZER(0, cq_is_pluck_finished, &is_finished_arg);
  for (;;) {
    if (is_finished_arg.stolen_completion != NULL) {
      gpr_mu_unlock(cq->mu);
      c = is_finished_arg.stolen_completion;
      is_finished_arg.stolen_completion = NULL;
      ret.type = GRPC_OP_COMPLETE;
      ret.success = c->next & 1u;
      ret.tag = c->tag;
      c->done(&exec_ctx, c->done_arg, c);
      break;
    }
    prev = &cqd->completed_head;
    while ((c = (grpc_cq_completion *)(prev->next & ~(uintptr_t)1)) !=
           &cqd->completed_head) {
      if (c->tag == tag) {
        prev->next = (prev->next & (uintptr_t)1) | (c->next & ~(uintptr_t)1);
        if (c == cqd->completed_tail) {
          cqd->completed_tail = prev;
        }
        gpr_mu_unlock(cq->mu);
        ret.type = GRPC_OP_COMPLETE;
        ret.success = c->next & 1u;
        ret.tag = c->tag;
        c->done(&exec_ctx, c->done_arg, c);
        goto done;
      }
      prev = c;
    }
    if (gpr_atm_no_barrier_load(&cqd->shutdown)) {
      gpr_mu_unlock(cq->mu);
      memset(&ret, 0, sizeof(ret));
      ret.type = GRPC_QUEUE_SHUTDOWN;
      break;
    }
    if (!add_plucker(cq, tag, &worker)) {
      gpr_log(GPR_DEBUG,
              "Too many outstanding grpc_completion_queue_pluck calls: maximum "
              "is %d",
              GRPC_MAX_COMPLETION_QUEUE_PLUCKERS);
      gpr_mu_unlock(cq->mu);
      memset(&ret, 0, sizeof(ret));
      /* TODO(ctiller): should we use a different result here */
      ret.type = GRPC_QUEUE_TIMEOUT;
      dump_pending_tags(cq);
      break;
    }
    now = gpr_now(GPR_CLOCK_MONOTONIC);
    if (!is_finished_arg.first_loop && gpr_time_cmp(now, deadline) >= 0) {
      del_plucker(cq, tag, &worker);
      gpr_mu_unlock(cq->mu);
      memset(&ret, 0, sizeof(ret));
      ret.type = GRPC_QUEUE_TIMEOUT;
      dump_pending_tags(cq);
      break;
    }

    cq->num_polls++;
    grpc_error *err = cq->poller_vtable->work(&exec_ctx, POLLSET_FROM_CQ(cq),
                                              &worker, now, deadline);
    if (err != GRPC_ERROR_NONE) {
      del_plucker(cq, tag, &worker);
      gpr_mu_unlock(cq->mu);
      const char *msg = grpc_error_string(err);
      gpr_log(GPR_ERROR, "Completion queue pluck failed: %s", msg);

      GRPC_ERROR_UNREF(err);
      memset(&ret, 0, sizeof(ret));
      ret.type = GRPC_QUEUE_TIMEOUT;
      dump_pending_tags(cq);
      break;
    }
    is_finished_arg.first_loop = false;
    del_plucker(cq, tag, &worker);
  }
done:
  GRPC_SURFACE_TRACE_RETURNED_EVENT(cq, &ret);
  GRPC_CQ_INTERNAL_UNREF(&exec_ctx, cq, "pluck");
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_ASSERT(is_finished_arg.stolen_completion == NULL);

  GPR_TIMER_END("grpc_completion_queue_pluck", 0);

  return ret;
}

grpc_event grpc_completion_queue_pluck(grpc_completion_queue *cq, void *tag,
                                       gpr_timespec deadline, void *reserved) {
  return cq->vtable->pluck(cq, tag, deadline, reserved);
}

static void cq_finish_shutdown_pluck(grpc_exec_ctx *exec_ctx,
                                     grpc_completion_queue *cq) {
  cq_pluck_data *cqd = DATA_FROM_CQ(cq);

  GPR_ASSERT(cqd->shutdown_called);
  GPR_ASSERT(!gpr_atm_no_barrier_load(&cqd->shutdown));
  gpr_atm_no_barrier_store(&cqd->shutdown, 1);

  cq->poller_vtable->shutdown(exec_ctx, POLLSET_FROM_CQ(cq),
                              &cq->pollset_shutdown_done);
}

/* NOTE: This function is almost exactly identical to cq_shutdown_next() but
 * merging them is a bit tricky and probably not worth it */
static void cq_shutdown_pluck(grpc_exec_ctx *exec_ctx,
                              grpc_completion_queue *cq) {
  cq_pluck_data *cqd = DATA_FROM_CQ(cq);

  /* Need an extra ref for cq here because:
   * We call cq_finish_shutdown_pluck() below, that would call pollset shutdown.
   * Pollset shutdown decrements the cq ref count which can potentially destroy
   * the cq (if that happens to be the last ref).
   * Creating an extra ref here prevents the cq from getting destroyed while
   * this function is still active */
  GRPC_CQ_INTERNAL_REF(cq, "shutting_down (pluck cq)");
  gpr_mu_lock(cq->mu);
  if (cqd->shutdown_called) {
    gpr_mu_unlock(cq->mu);
    GRPC_CQ_INTERNAL_UNREF(exec_ctx, cq, "shutting_down (pluck cq)");
    GPR_TIMER_END("grpc_completion_queue_shutdown", 0);
    return;
  }
  cqd->shutdown_called = true;
  if (gpr_atm_full_fetch_add(&cqd->pending_events, -1) == 1) {
    cq_finish_shutdown_pluck(exec_ctx, cq);
  }
  gpr_mu_unlock(cq->mu);
  GRPC_CQ_INTERNAL_UNREF(exec_ctx, cq, "shutting_down (pluck cq)");
}

/* Shutdown simply drops a ref that we reserved at creation time; if we drop
   to zero here, then enter shutdown mode and wake up any waiters */
void grpc_completion_queue_shutdown(grpc_completion_queue *cq) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  GPR_TIMER_BEGIN("grpc_completion_queue_shutdown", 0);
  GRPC_API_TRACE("grpc_completion_queue_shutdown(cq=%p)", 1, (cq));
  cq->vtable->shutdown(&exec_ctx, cq);
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_TIMER_END("grpc_completion_queue_shutdown", 0);
}

void grpc_completion_queue_destroy(grpc_completion_queue *cq) {
  GRPC_API_TRACE("grpc_completion_queue_destroy(cq=%p)", 1, (cq));
  GPR_TIMER_BEGIN("grpc_completion_queue_destroy", 0);
  grpc_completion_queue_shutdown(cq);

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  GRPC_CQ_INTERNAL_UNREF(&exec_ctx, cq, "destroy");
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_TIMER_END("grpc_completion_queue_destroy", 0);
}

grpc_pollset *grpc_cq_pollset(grpc_completion_queue *cq) {
  return cq->poller_vtable->can_get_pollset ? POLLSET_FROM_CQ(cq) : NULL;
}

bool grpc_cq_can_listen(grpc_completion_queue *cq) {
  return cq->poller_vtable->can_listen;
}
