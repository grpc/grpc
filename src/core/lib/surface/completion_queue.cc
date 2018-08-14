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
#include <grpc/support/port_platform.h>

#include "src/core/lib/surface/completion_queue.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/debug/stats.h"
#include "src/core/lib/gpr/spinlock.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/tls.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/event_string.h"

grpc_core::TraceFlag grpc_trace_operation_failures(false, "op_failure");
grpc_core::DebugOnlyTraceFlag grpc_trace_pending_tags(false, "pending_tags");
grpc_core::DebugOnlyTraceFlag grpc_trace_cq_refcount(false, "cq_refcount");

// Specifies a cq thread local cache.
// The first event that occurs on a thread
// with a cq cache will go into that cache, and
// will only be returned on the thread that initialized the cache.
// NOTE: Only one event will ever be cached.
GPR_TLS_DECL(g_cached_event);
GPR_TLS_DECL(g_cached_cq);

typedef struct {
  grpc_pollset_worker** worker;
  void* tag;
} plucker;

typedef struct {
  bool can_get_pollset;
  bool can_listen;
  size_t (*size)(void);
  void (*init)(grpc_pollset* pollset, gpr_mu** mu);
  grpc_error* (*kick)(grpc_pollset* pollset,
                      grpc_pollset_worker* specific_worker);
  grpc_error* (*work)(grpc_pollset* pollset, grpc_pollset_worker** worker,
                      grpc_millis deadline);
  void (*shutdown)(grpc_pollset* pollset, grpc_closure* closure);
  void (*destroy)(grpc_pollset* pollset);
} cq_poller_vtable;

typedef struct non_polling_worker {
  gpr_cv cv;
  bool kicked;
  struct non_polling_worker* next;
  struct non_polling_worker* prev;
} non_polling_worker;

typedef struct {
  gpr_mu mu;
  non_polling_worker* root;
  grpc_closure* shutdown;
} non_polling_poller;

static size_t non_polling_poller_size(void) {
  return sizeof(non_polling_poller);
}

static void non_polling_poller_init(grpc_pollset* pollset, gpr_mu** mu) {
  non_polling_poller* npp = reinterpret_cast<non_polling_poller*>(pollset);
  gpr_mu_init(&npp->mu);
  *mu = &npp->mu;
}

static void non_polling_poller_destroy(grpc_pollset* pollset) {
  non_polling_poller* npp = reinterpret_cast<non_polling_poller*>(pollset);
  gpr_mu_destroy(&npp->mu);
}

static grpc_error* non_polling_poller_work(grpc_pollset* pollset,
                                           grpc_pollset_worker** worker,
                                           grpc_millis deadline) {
  non_polling_poller* npp = reinterpret_cast<non_polling_poller*>(pollset);
  if (npp->shutdown) return GRPC_ERROR_NONE;
  non_polling_worker w;
  gpr_cv_init(&w.cv);
  if (worker != nullptr) *worker = reinterpret_cast<grpc_pollset_worker*>(&w);
  if (npp->root == nullptr) {
    npp->root = w.next = w.prev = &w;
  } else {
    w.next = npp->root;
    w.prev = w.next->prev;
    w.next->prev = w.prev->next = &w;
  }
  w.kicked = false;
  gpr_timespec deadline_ts =
      grpc_millis_to_timespec(deadline, GPR_CLOCK_MONOTONIC);
  while (!npp->shutdown && !w.kicked &&
         !gpr_cv_wait(&w.cv, &npp->mu, deadline_ts))
    ;
  grpc_core::ExecCtx::Get()->InvalidateNow();
  if (&w == npp->root) {
    npp->root = w.next;
    if (&w == npp->root) {
      if (npp->shutdown) {
        GRPC_CLOSURE_SCHED(npp->shutdown, GRPC_ERROR_NONE);
      }
      npp->root = nullptr;
    }
  }
  w.next->prev = w.prev;
  w.prev->next = w.next;
  gpr_cv_destroy(&w.cv);
  if (worker != nullptr) *worker = nullptr;
  return GRPC_ERROR_NONE;
}

static grpc_error* non_polling_poller_kick(
    grpc_pollset* pollset, grpc_pollset_worker* specific_worker) {
  non_polling_poller* p = reinterpret_cast<non_polling_poller*>(pollset);
  if (specific_worker == nullptr)
    specific_worker = reinterpret_cast<grpc_pollset_worker*>(p->root);
  if (specific_worker != nullptr) {
    non_polling_worker* w =
        reinterpret_cast<non_polling_worker*>(specific_worker);
    if (!w->kicked) {
      w->kicked = true;
      gpr_cv_signal(&w->cv);
    }
  }
  return GRPC_ERROR_NONE;
}

static void non_polling_poller_shutdown(grpc_pollset* pollset,
                                        grpc_closure* closure) {
  non_polling_poller* p = reinterpret_cast<non_polling_poller*>(pollset);
  GPR_ASSERT(closure != nullptr);
  p->shutdown = closure;
  if (p->root == nullptr) {
    GRPC_CLOSURE_SCHED(closure, GRPC_ERROR_NONE);
  } else {
    non_polling_worker* w = p->root;
    do {
      gpr_cv_signal(&w->cv);
      w = w->next;
    } while (w != p->root);
  }
}

static const cq_poller_vtable g_poller_vtable_by_poller_type[] = {
    /* GRPC_CQ_DEFAULT_POLLING */
    {true, true, grpc_pollset_size, grpc_pollset_init, grpc_pollset_kick,
     grpc_pollset_work, grpc_pollset_shutdown, grpc_pollset_destroy},
    /* GRPC_CQ_NON_LISTENING */
    {true, false, grpc_pollset_size, grpc_pollset_init, grpc_pollset_kick,
     grpc_pollset_work, grpc_pollset_shutdown, grpc_pollset_destroy},
    /* GRPC_CQ_NON_POLLING */
    {false, false, non_polling_poller_size, non_polling_poller_init,
     non_polling_poller_kick, non_polling_poller_work,
     non_polling_poller_shutdown, non_polling_poller_destroy},
};

typedef struct cq_vtable {
  grpc_cq_completion_type cq_completion_type;
  size_t data_size;
  void (*init)(void* data, grpc_core::CQCallbackInterface* shutdown_callback);
  void (*shutdown)(grpc_completion_queue* cq);
  void (*destroy)(void* data);
  bool (*begin_op)(grpc_completion_queue* cq, void* tag);
  void (*end_op)(grpc_completion_queue* cq, void* tag, grpc_error* error,
                 void (*done)(void* done_arg, grpc_cq_completion* storage),
                 void* done_arg, grpc_cq_completion* storage);
  grpc_event (*next)(grpc_completion_queue* cq, gpr_timespec deadline,
                     void* reserved);
  grpc_event (*pluck)(grpc_completion_queue* cq, void* tag,
                      gpr_timespec deadline, void* reserved);
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
  grpc_cq_completion* completed_tail;

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

typedef struct cq_callback_data {
  /** No actual completed events queue, unlike other types */

  /** Number of pending events (+1 if we're not shutdown) */
  gpr_atm pending_events;

  /** Counter of how many things have ever been queued on this completion queue
      useful for avoiding locks to check the queue */
  gpr_atm things_queued_ever;

  /** 0 initially. 1 once we initiated shutdown */
  bool shutdown_called;

  /** A callback that gets invoked when the CQ completes shutdown */
  grpc_core::CQCallbackInterface* shutdown_callback;
} cq_callback_data;

/* Completion queue structure */
struct grpc_completion_queue {
  /** Once owning_refs drops to zero, we will destroy the cq */
  gpr_refcount owning_refs;

  gpr_mu* mu;

  const cq_vtable* vtable;
  const cq_poller_vtable* poller_vtable;

#ifndef NDEBUG
  void** outstanding_tags;
  size_t outstanding_tag_count;
  size_t outstanding_tag_capacity;
#endif

  grpc_closure pollset_shutdown_done;
  int num_polls;
};

/* Forward declarations */
static void cq_finish_shutdown_next(grpc_completion_queue* cq);
static void cq_finish_shutdown_pluck(grpc_completion_queue* cq);
static void cq_finish_shutdown_callback(grpc_completion_queue* cq);
static void cq_shutdown_next(grpc_completion_queue* cq);
static void cq_shutdown_pluck(grpc_completion_queue* cq);
static void cq_shutdown_callback(grpc_completion_queue* cq);

static bool cq_begin_op_for_next(grpc_completion_queue* cq, void* tag);
static bool cq_begin_op_for_pluck(grpc_completion_queue* cq, void* tag);
static bool cq_begin_op_for_callback(grpc_completion_queue* cq, void* tag);

// A cq_end_op function is called when an operation on a given CQ with
// a given tag has completed. The storage argument is a reference to the
// space reserved for this completion as it is placed into the corresponding
// queue. The done argument is a callback that will be invoked when it is
// safe to free up that storage. The storage MUST NOT be freed until the
// done callback is invoked.
static void cq_end_op_for_next(grpc_completion_queue* cq, void* tag,
                               grpc_error* error,
                               void (*done)(void* done_arg,
                                            grpc_cq_completion* storage),
                               void* done_arg, grpc_cq_completion* storage);

static void cq_end_op_for_pluck(grpc_completion_queue* cq, void* tag,
                                grpc_error* error,
                                void (*done)(void* done_arg,
                                             grpc_cq_completion* storage),
                                void* done_arg, grpc_cq_completion* storage);

static void cq_end_op_for_callback(grpc_completion_queue* cq, void* tag,
                                   grpc_error* error,
                                   void (*done)(void* done_arg,
                                                grpc_cq_completion* storage),
                                   void* done_arg, grpc_cq_completion* storage);

static grpc_event cq_next(grpc_completion_queue* cq, gpr_timespec deadline,
                          void* reserved);

static grpc_event cq_pluck(grpc_completion_queue* cq, void* tag,
                           gpr_timespec deadline, void* reserved);

// Note that cq_init_next and cq_init_pluck do not use the shutdown_callback
static void cq_init_next(void* data,
                         grpc_core::CQCallbackInterface* shutdown_callback);
static void cq_init_pluck(void* data,
                          grpc_core::CQCallbackInterface* shutdown_callback);
static void cq_init_callback(void* data,
                             grpc_core::CQCallbackInterface* shutdown_callback);
static void cq_destroy_next(void* data);
static void cq_destroy_pluck(void* data);
static void cq_destroy_callback(void* data);

/* Completion queue vtables based on the completion-type */
static const cq_vtable g_cq_vtable[] = {
    /* GRPC_CQ_NEXT */
    {GRPC_CQ_NEXT, sizeof(cq_next_data), cq_init_next, cq_shutdown_next,
     cq_destroy_next, cq_begin_op_for_next, cq_end_op_for_next, cq_next,
     nullptr},
    /* GRPC_CQ_PLUCK */
    {GRPC_CQ_PLUCK, sizeof(cq_pluck_data), cq_init_pluck, cq_shutdown_pluck,
     cq_destroy_pluck, cq_begin_op_for_pluck, cq_end_op_for_pluck, nullptr,
     cq_pluck},
    /* GRPC_CQ_CALLBACK */
    {GRPC_CQ_CALLBACK, sizeof(cq_callback_data), cq_init_callback,
     cq_shutdown_callback, cq_destroy_callback, cq_begin_op_for_callback,
     cq_end_op_for_callback, nullptr, nullptr},
};

#define DATA_FROM_CQ(cq) ((void*)(cq + 1))
#define POLLSET_FROM_CQ(cq) \
  ((grpc_pollset*)(cq->vtable->data_size + (char*)DATA_FROM_CQ(cq)))

grpc_core::TraceFlag grpc_cq_pluck_trace(false, "queue_pluck");

#define GRPC_SURFACE_TRACE_RETURNED_EVENT(cq, event)                       \
  if (grpc_api_trace.enabled() && (grpc_cq_pluck_trace.enabled() ||        \
                                   (event)->type != GRPC_QUEUE_TIMEOUT)) { \
    char* _ev = grpc_event_string(event);                                  \
    gpr_log(GPR_INFO, "RETURN_EVENT[%p]: %s", cq, _ev);                    \
    gpr_free(_ev);                                                         \
  }

static void on_pollset_shutdown_done(void* cq, grpc_error* error);

void grpc_cq_global_init() {
  gpr_tls_init(&g_cached_event);
  gpr_tls_init(&g_cached_cq);
}

void grpc_completion_queue_thread_local_cache_init(grpc_completion_queue* cq) {
  if ((grpc_completion_queue*)gpr_tls_get(&g_cached_cq) == nullptr) {
    gpr_tls_set(&g_cached_event, (intptr_t)0);
    gpr_tls_set(&g_cached_cq, (intptr_t)cq);
  }
}

int grpc_completion_queue_thread_local_cache_flush(grpc_completion_queue* cq,
                                                   void** tag, int* ok) {
  grpc_cq_completion* storage =
      (grpc_cq_completion*)gpr_tls_get(&g_cached_event);
  int ret = 0;
  if (storage != nullptr &&
      (grpc_completion_queue*)gpr_tls_get(&g_cached_cq) == cq) {
    *tag = storage->tag;
    grpc_core::ExecCtx exec_ctx;
    *ok = (storage->next & static_cast<uintptr_t>(1)) == 1;
    storage->done(storage->done_arg, storage);
    ret = 1;
    cq_next_data* cqd = static_cast<cq_next_data*> DATA_FROM_CQ(cq);
    if (gpr_atm_full_fetch_add(&cqd->pending_events, -1) == 1) {
      GRPC_CQ_INTERNAL_REF(cq, "shutting_down");
      gpr_mu_lock(cq->mu);
      cq_finish_shutdown_next(cq);
      gpr_mu_unlock(cq->mu);
      GRPC_CQ_INTERNAL_UNREF(cq, "shutting_down");
    }
  }
  gpr_tls_set(&g_cached_event, (intptr_t)0);
  gpr_tls_set(&g_cached_cq, (intptr_t)0);

  return ret;
}

static void cq_event_queue_init(grpc_cq_event_queue* q) {
  gpr_mpscq_init(&q->queue);
  q->queue_lock = GPR_SPINLOCK_INITIALIZER;
  gpr_atm_no_barrier_store(&q->num_queue_items, 0);
}

static void cq_event_queue_destroy(grpc_cq_event_queue* q) {
  gpr_mpscq_destroy(&q->queue);
}

static bool cq_event_queue_push(grpc_cq_event_queue* q, grpc_cq_completion* c) {
  gpr_mpscq_push(&q->queue, reinterpret_cast<gpr_mpscq_node*>(c));
  return gpr_atm_no_barrier_fetch_add(&q->num_queue_items, 1) == 0;
}

static grpc_cq_completion* cq_event_queue_pop(grpc_cq_event_queue* q) {
  grpc_cq_completion* c = nullptr;

  if (gpr_spinlock_trylock(&q->queue_lock)) {
    GRPC_STATS_INC_CQ_EV_QUEUE_TRYLOCK_SUCCESSES();

    bool is_empty = false;
    c = reinterpret_cast<grpc_cq_completion*>(
        gpr_mpscq_pop_and_check_end(&q->queue, &is_empty));
    gpr_spinlock_unlock(&q->queue_lock);

    if (c == nullptr && !is_empty) {
      GRPC_STATS_INC_CQ_EV_QUEUE_TRANSIENT_POP_FAILURES();
    }
  } else {
    GRPC_STATS_INC_CQ_EV_QUEUE_TRYLOCK_FAILURES();
  }

  if (c) {
    gpr_atm_no_barrier_fetch_add(&q->num_queue_items, -1);
  }

  return c;
}

/* Note: The counter is not incremented/decremented atomically with push/pop.
 * The count is only eventually consistent */
static long cq_event_queue_num_items(grpc_cq_event_queue* q) {
  return static_cast<long>(gpr_atm_no_barrier_load(&q->num_queue_items));
}

grpc_completion_queue* grpc_completion_queue_create_internal(
    grpc_cq_completion_type completion_type, grpc_cq_polling_type polling_type,
    grpc_core::CQCallbackInterface* shutdown_callback) {
  GPR_TIMER_SCOPE("grpc_completion_queue_create_internal", 0);

  grpc_completion_queue* cq;

  GRPC_API_TRACE(
      "grpc_completion_queue_create_internal(completion_type=%d, "
      "polling_type=%d)",
      2, (completion_type, polling_type));

  const cq_vtable* vtable = &g_cq_vtable[completion_type];
  const cq_poller_vtable* poller_vtable =
      &g_poller_vtable_by_poller_type[polling_type];

  grpc_core::ExecCtx exec_ctx;
  GRPC_STATS_INC_CQS_CREATED();

  cq = static_cast<grpc_completion_queue*>(
      gpr_zalloc(sizeof(grpc_completion_queue) + vtable->data_size +
                 poller_vtable->size()));

  cq->vtable = vtable;
  cq->poller_vtable = poller_vtable;

  /* One for destroy(), one for pollset_shutdown */
  gpr_ref_init(&cq->owning_refs, 2);

  poller_vtable->init(POLLSET_FROM_CQ(cq), &cq->mu);
  vtable->init(DATA_FROM_CQ(cq), shutdown_callback);

  GRPC_CLOSURE_INIT(&cq->pollset_shutdown_done, on_pollset_shutdown_done, cq,
                    grpc_schedule_on_exec_ctx);
  return cq;
}

static void cq_init_next(void* data,
                         grpc_core::CQCallbackInterface* shutdown_callback) {
  cq_next_data* cqd = static_cast<cq_next_data*>(data);
  /* Initial count is dropped by grpc_completion_queue_shutdown */
  gpr_atm_no_barrier_store(&cqd->pending_events, 1);
  cqd->shutdown_called = false;
  gpr_atm_no_barrier_store(&cqd->things_queued_ever, 0);
  cq_event_queue_init(&cqd->queue);
}

static void cq_destroy_next(void* data) {
  cq_next_data* cqd = static_cast<cq_next_data*>(data);
  GPR_ASSERT(cq_event_queue_num_items(&cqd->queue) == 0);
  cq_event_queue_destroy(&cqd->queue);
}

static void cq_init_pluck(void* data,
                          grpc_core::CQCallbackInterface* shutdown_callback) {
  cq_pluck_data* cqd = static_cast<cq_pluck_data*>(data);
  /* Initial count is dropped by grpc_completion_queue_shutdown */
  gpr_atm_no_barrier_store(&cqd->pending_events, 1);
  cqd->completed_tail = &cqd->completed_head;
  cqd->completed_head.next = (uintptr_t)cqd->completed_tail;
  gpr_atm_no_barrier_store(&cqd->shutdown, 0);
  cqd->shutdown_called = false;
  cqd->num_pluckers = 0;
  gpr_atm_no_barrier_store(&cqd->things_queued_ever, 0);
}

static void cq_destroy_pluck(void* data) {
  cq_pluck_data* cqd = static_cast<cq_pluck_data*>(data);
  GPR_ASSERT(cqd->completed_head.next == (uintptr_t)&cqd->completed_head);
}

static void cq_init_callback(
    void* data, grpc_core::CQCallbackInterface* shutdown_callback) {
  cq_callback_data* cqd = static_cast<cq_callback_data*>(data);
  /* Initial count is dropped by grpc_completion_queue_shutdown */
  gpr_atm_no_barrier_store(&cqd->pending_events, 1);
  cqd->shutdown_called = false;
  gpr_atm_no_barrier_store(&cqd->things_queued_ever, 0);
  cqd->shutdown_callback = shutdown_callback;
}

static void cq_destroy_callback(void* data) {}

grpc_cq_completion_type grpc_get_cq_completion_type(grpc_completion_queue* cq) {
  return cq->vtable->cq_completion_type;
}

int grpc_get_cq_poll_num(grpc_completion_queue* cq) {
  int cur_num_polls;
  gpr_mu_lock(cq->mu);
  cur_num_polls = cq->num_polls;
  gpr_mu_unlock(cq->mu);
  return cur_num_polls;
}

#ifndef NDEBUG
void grpc_cq_internal_ref(grpc_completion_queue* cq, const char* reason,
                          const char* file, int line) {
  if (grpc_trace_cq_refcount.enabled()) {
    gpr_atm val = gpr_atm_no_barrier_load(&cq->owning_refs.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "CQ:%p   ref %" PRIdPTR " -> %" PRIdPTR " %s", cq, val, val + 1,
            reason);
  }
#else
void grpc_cq_internal_ref(grpc_completion_queue* cq) {
#endif
  gpr_ref(&cq->owning_refs);
}

static void on_pollset_shutdown_done(void* arg, grpc_error* error) {
  grpc_completion_queue* cq = static_cast<grpc_completion_queue*>(arg);
  GRPC_CQ_INTERNAL_UNREF(cq, "pollset_destroy");
}

#ifndef NDEBUG
void grpc_cq_internal_unref(grpc_completion_queue* cq, const char* reason,
                            const char* file, int line) {
  if (grpc_trace_cq_refcount.enabled()) {
    gpr_atm val = gpr_atm_no_barrier_load(&cq->owning_refs.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "CQ:%p unref %" PRIdPTR " -> %" PRIdPTR " %s", cq, val, val - 1,
            reason);
  }
#else
void grpc_cq_internal_unref(grpc_completion_queue* cq) {
#endif
  if (gpr_unref(&cq->owning_refs)) {
    cq->vtable->destroy(DATA_FROM_CQ(cq));
    cq->poller_vtable->destroy(POLLSET_FROM_CQ(cq));
#ifndef NDEBUG
    gpr_free(cq->outstanding_tags);
#endif
    gpr_free(cq);
  }
}

#ifndef NDEBUG
static void cq_check_tag(grpc_completion_queue* cq, void* tag, bool lock_cq) {
  int found = 0;
  if (lock_cq) {
    gpr_mu_lock(cq->mu);
  }

  for (int i = 0; i < static_cast<int>(cq->outstanding_tag_count); i++) {
    if (cq->outstanding_tags[i] == tag) {
      cq->outstanding_tag_count--;
      GPR_SWAP(void*, cq->outstanding_tags[i],
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
static void cq_check_tag(grpc_completion_queue* cq, void* tag, bool lock_cq) {}
#endif

/* Atomically increments a counter only if the counter is not zero. Returns
 * true if the increment was successful; false if the counter is zero */
static bool atm_inc_if_nonzero(gpr_atm* counter) {
  while (true) {
    gpr_atm count = gpr_atm_acq_load(counter);
    /* If zero, we are done. If not, we must to a CAS (instead of an atomic
     * increment) to maintain the contract: do not increment the counter if it
     * is zero. */
    if (count == 0) {
      return false;
    } else if (gpr_atm_full_cas(counter, count, count + 1)) {
      break;
    }
  }

  return true;
}

static bool cq_begin_op_for_next(grpc_completion_queue* cq, void* tag) {
  cq_next_data* cqd = static_cast<cq_next_data*> DATA_FROM_CQ(cq);
  return atm_inc_if_nonzero(&cqd->pending_events);
}

static bool cq_begin_op_for_pluck(grpc_completion_queue* cq, void* tag) {
  cq_pluck_data* cqd = static_cast<cq_pluck_data*> DATA_FROM_CQ(cq);
  return atm_inc_if_nonzero(&cqd->pending_events);
}

static bool cq_begin_op_for_callback(grpc_completion_queue* cq, void* tag) {
  cq_callback_data* cqd = static_cast<cq_callback_data*> DATA_FROM_CQ(cq);
  return atm_inc_if_nonzero(&cqd->pending_events);
}

bool grpc_cq_begin_op(grpc_completion_queue* cq, void* tag) {
#ifndef NDEBUG
  gpr_mu_lock(cq->mu);
  if (cq->outstanding_tag_count == cq->outstanding_tag_capacity) {
    cq->outstanding_tag_capacity = GPR_MAX(4, 2 * cq->outstanding_tag_capacity);
    cq->outstanding_tags = static_cast<void**>(gpr_realloc(
        cq->outstanding_tags,
        sizeof(*cq->outstanding_tags) * cq->outstanding_tag_capacity));
  }
  cq->outstanding_tags[cq->outstanding_tag_count++] = tag;
  gpr_mu_unlock(cq->mu);
#endif
  return cq->vtable->begin_op(cq, tag);
}

/* Queue a GRPC_OP_COMPLETED operation to a completion queue (with a
 * completion
 * type of GRPC_CQ_NEXT) */
static void cq_end_op_for_next(grpc_completion_queue* cq, void* tag,
                               grpc_error* error,
                               void (*done)(void* done_arg,
                                            grpc_cq_completion* storage),
                               void* done_arg, grpc_cq_completion* storage) {
  GPR_TIMER_SCOPE("cq_end_op_for_next", 0);

  if (grpc_api_trace.enabled() ||
      (grpc_trace_operation_failures.enabled() && error != GRPC_ERROR_NONE)) {
    const char* errmsg = grpc_error_string(error);
    GRPC_API_TRACE(
        "cq_end_op_for_next(cq=%p, tag=%p, error=%s, "
        "done=%p, done_arg=%p, storage=%p)",
        6, (cq, tag, errmsg, done, done_arg, storage));
    if (grpc_trace_operation_failures.enabled() && error != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR, "Operation failed: tag=%p, error=%s", tag, errmsg);
    }
  }
  cq_next_data* cqd = static_cast<cq_next_data*> DATA_FROM_CQ(cq);
  int is_success = (error == GRPC_ERROR_NONE);

  storage->tag = tag;
  storage->done = done;
  storage->done_arg = done_arg;
  storage->next = static_cast<uintptr_t>(is_success);

  cq_check_tag(cq, tag, true); /* Used in debug builds only */

  if ((grpc_completion_queue*)gpr_tls_get(&g_cached_cq) == cq &&
      (grpc_cq_completion*)gpr_tls_get(&g_cached_event) == nullptr) {
    gpr_tls_set(&g_cached_event, (intptr_t)storage);
  } else {
    /* Add the completion to the queue */
    bool is_first = cq_event_queue_push(&cqd->queue, storage);
    gpr_atm_no_barrier_fetch_add(&cqd->things_queued_ever, 1);

    /* Since we do not hold the cq lock here, it is important to do an 'acquire'
       load here (instead of a 'no_barrier' load) to match with the release
       store
       (done via gpr_atm_full_fetch_add(pending_events, -1)) in cq_shutdown_next
       */
    bool will_definitely_shutdown = gpr_atm_acq_load(&cqd->pending_events) == 1;

    if (!will_definitely_shutdown) {
      /* Only kick if this is the first item queued */
      if (is_first) {
        gpr_mu_lock(cq->mu);
        grpc_error* kick_error =
            cq->poller_vtable->kick(POLLSET_FROM_CQ(cq), nullptr);
        gpr_mu_unlock(cq->mu);

        if (kick_error != GRPC_ERROR_NONE) {
          const char* msg = grpc_error_string(kick_error);
          gpr_log(GPR_ERROR, "Kick failed: %s", msg);
          GRPC_ERROR_UNREF(kick_error);
        }
      }
      if (gpr_atm_full_fetch_add(&cqd->pending_events, -1) == 1) {
        GRPC_CQ_INTERNAL_REF(cq, "shutting_down");
        gpr_mu_lock(cq->mu);
        cq_finish_shutdown_next(cq);
        gpr_mu_unlock(cq->mu);
        GRPC_CQ_INTERNAL_UNREF(cq, "shutting_down");
      }
    } else {
      GRPC_CQ_INTERNAL_REF(cq, "shutting_down");
      gpr_atm_rel_store(&cqd->pending_events, 0);
      gpr_mu_lock(cq->mu);
      cq_finish_shutdown_next(cq);
      gpr_mu_unlock(cq->mu);
      GRPC_CQ_INTERNAL_UNREF(cq, "shutting_down");
    }
  }

  GRPC_ERROR_UNREF(error);
}

/* Queue a GRPC_OP_COMPLETED operation to a completion queue (with a
 * completion
 * type of GRPC_CQ_PLUCK) */
static void cq_end_op_for_pluck(grpc_completion_queue* cq, void* tag,
                                grpc_error* error,
                                void (*done)(void* done_arg,
                                             grpc_cq_completion* storage),
                                void* done_arg, grpc_cq_completion* storage) {
  GPR_TIMER_SCOPE("cq_end_op_for_pluck", 0);

  cq_pluck_data* cqd = static_cast<cq_pluck_data*> DATA_FROM_CQ(cq);
  int is_success = (error == GRPC_ERROR_NONE);

  if (grpc_api_trace.enabled() ||
      (grpc_trace_operation_failures.enabled() && error != GRPC_ERROR_NONE)) {
    const char* errmsg = grpc_error_string(error);
    GRPC_API_TRACE(
        "cq_end_op_for_pluck(cq=%p, tag=%p, error=%s, "
        "done=%p, done_arg=%p, storage=%p)",
        6, (cq, tag, errmsg, done, done_arg, storage));
    if (grpc_trace_operation_failures.enabled() && error != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR, "Operation failed: tag=%p, error=%s", tag, errmsg);
    }
  }

  storage->tag = tag;
  storage->done = done;
  storage->done_arg = done_arg;
  storage->next =
      ((uintptr_t)&cqd->completed_head) | (static_cast<uintptr_t>(is_success));

  gpr_mu_lock(cq->mu);
  cq_check_tag(cq, tag, false); /* Used in debug builds only */

  /* Add to the list of completions */
  gpr_atm_no_barrier_fetch_add(&cqd->things_queued_ever, 1);
  cqd->completed_tail->next =
      ((uintptr_t)storage) | (1u & cqd->completed_tail->next);
  cqd->completed_tail = storage;

  if (gpr_atm_full_fetch_add(&cqd->pending_events, -1) == 1) {
    cq_finish_shutdown_pluck(cq);
    gpr_mu_unlock(cq->mu);
  } else {
    grpc_pollset_worker* pluck_worker = nullptr;
    for (int i = 0; i < cqd->num_pluckers; i++) {
      if (cqd->pluckers[i].tag == tag) {
        pluck_worker = *cqd->pluckers[i].worker;
        break;
      }
    }

    grpc_error* kick_error =
        cq->poller_vtable->kick(POLLSET_FROM_CQ(cq), pluck_worker);

    gpr_mu_unlock(cq->mu);

    if (kick_error != GRPC_ERROR_NONE) {
      const char* msg = grpc_error_string(kick_error);
      gpr_log(GPR_ERROR, "Kick failed: %s", msg);

      GRPC_ERROR_UNREF(kick_error);
    }
  }

  GRPC_ERROR_UNREF(error);
}

/* Complete an event on a completion queue of type GRPC_CQ_CALLBACK */
static void cq_end_op_for_callback(
    grpc_completion_queue* cq, void* tag, grpc_error* error,
    void (*done)(void* done_arg, grpc_cq_completion* storage), void* done_arg,
    grpc_cq_completion* storage) {
  GPR_TIMER_SCOPE("cq_end_op_for_callback", 0);

  cq_callback_data* cqd = static_cast<cq_callback_data*> DATA_FROM_CQ(cq);
  bool is_success = (error == GRPC_ERROR_NONE);

  if (grpc_api_trace.enabled() ||
      (grpc_trace_operation_failures.enabled() && error != GRPC_ERROR_NONE)) {
    const char* errmsg = grpc_error_string(error);
    GRPC_API_TRACE(
        "cq_end_op_for_callback(cq=%p, tag=%p, error=%s, "
        "done=%p, done_arg=%p, storage=%p)",
        6, (cq, tag, errmsg, done, done_arg, storage));
    if (grpc_trace_operation_failures.enabled() && error != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR, "Operation failed: tag=%p, error=%s", tag, errmsg);
    }
  }

  // The callback-based CQ isn't really a queue at all and thus has no need
  // for reserved storage. Invoke the done callback right away to release it.
  done(done_arg, storage);

  gpr_mu_lock(cq->mu);
  cq_check_tag(cq, tag, false); /* Used in debug builds only */

  gpr_atm_no_barrier_fetch_add(&cqd->things_queued_ever, 1);
  if (gpr_atm_full_fetch_add(&cqd->pending_events, -1) == 1) {
    cq_finish_shutdown_callback(cq);
    gpr_mu_unlock(cq->mu);
  } else {
    gpr_mu_unlock(cq->mu);
  }

  GRPC_ERROR_UNREF(error);

  (static_cast<grpc_core::CQCallbackInterface*>(tag))->Run(is_success);
}

void grpc_cq_end_op(grpc_completion_queue* cq, void* tag, grpc_error* error,
                    void (*done)(void* done_arg, grpc_cq_completion* storage),
                    void* done_arg, grpc_cq_completion* storage) {
  cq->vtable->end_op(cq, tag, error, done, done_arg, storage);
}

typedef struct {
  gpr_atm last_seen_things_queued_ever;
  grpc_completion_queue* cq;
  grpc_millis deadline;
  grpc_cq_completion* stolen_completion;
  void* tag; /* for pluck */
  bool first_loop;
} cq_is_finished_arg;

class ExecCtxNext : public grpc_core::ExecCtx {
 public:
  ExecCtxNext(void* arg) : ExecCtx(0), check_ready_to_finish_arg_(arg) {}

  bool CheckReadyToFinish() override {
    cq_is_finished_arg* a =
        static_cast<cq_is_finished_arg*>(check_ready_to_finish_arg_);
    grpc_completion_queue* cq = a->cq;
    cq_next_data* cqd = static_cast<cq_next_data*> DATA_FROM_CQ(cq);
    GPR_ASSERT(a->stolen_completion == nullptr);

    gpr_atm current_last_seen_things_queued_ever =
        gpr_atm_no_barrier_load(&cqd->things_queued_ever);

    if (current_last_seen_things_queued_ever !=
        a->last_seen_things_queued_ever) {
      a->last_seen_things_queued_ever =
          gpr_atm_no_barrier_load(&cqd->things_queued_ever);

      /* Pop a cq_completion from the queue. Returns NULL if the queue is empty
       * might return NULL in some cases even if the queue is not empty; but
       * that
       * is ok and doesn't affect correctness. Might effect the tail latencies a
       * bit) */
      a->stolen_completion = cq_event_queue_pop(&cqd->queue);
      if (a->stolen_completion != nullptr) {
        return true;
      }
    }
    return !a->first_loop && a->deadline < grpc_core::ExecCtx::Get()->Now();
  }

 private:
  void* check_ready_to_finish_arg_;
};

#ifndef NDEBUG
static void dump_pending_tags(grpc_completion_queue* cq) {
  if (!grpc_trace_pending_tags.enabled()) return;

  gpr_strvec v;
  gpr_strvec_init(&v);
  gpr_strvec_add(&v, gpr_strdup("PENDING TAGS:"));
  gpr_mu_lock(cq->mu);
  for (size_t i = 0; i < cq->outstanding_tag_count; i++) {
    char* s;
    gpr_asprintf(&s, " %p", cq->outstanding_tags[i]);
    gpr_strvec_add(&v, s);
  }
  gpr_mu_unlock(cq->mu);
  char* out = gpr_strvec_flatten(&v, nullptr);
  gpr_strvec_destroy(&v);
  gpr_log(GPR_DEBUG, "%s", out);
  gpr_free(out);
}
#else
static void dump_pending_tags(grpc_completion_queue* cq) {}
#endif

static grpc_event cq_next(grpc_completion_queue* cq, gpr_timespec deadline,
                          void* reserved) {
  GPR_TIMER_SCOPE("grpc_completion_queue_next", 0);

  grpc_event ret;
  cq_next_data* cqd = static_cast<cq_next_data*> DATA_FROM_CQ(cq);

  GRPC_API_TRACE(
      "grpc_completion_queue_next("
      "cq=%p, "
      "deadline=gpr_timespec { tv_sec: %" PRId64
      ", tv_nsec: %d, clock_type: %d }, "
      "reserved=%p)",
      5,
      (cq, deadline.tv_sec, deadline.tv_nsec, (int)deadline.clock_type,
       reserved));
  GPR_ASSERT(!reserved);

  dump_pending_tags(cq);

  GRPC_CQ_INTERNAL_REF(cq, "next");

  grpc_millis deadline_millis = grpc_timespec_to_millis_round_up(deadline);
  cq_is_finished_arg is_finished_arg = {
      gpr_atm_no_barrier_load(&cqd->things_queued_ever),
      cq,
      deadline_millis,
      nullptr,
      nullptr,
      true};
  ExecCtxNext exec_ctx(&is_finished_arg);
  for (;;) {
    grpc_millis iteration_deadline = deadline_millis;

    if (is_finished_arg.stolen_completion != nullptr) {
      grpc_cq_completion* c = is_finished_arg.stolen_completion;
      is_finished_arg.stolen_completion = nullptr;
      ret.type = GRPC_OP_COMPLETE;
      ret.success = c->next & 1u;
      ret.tag = c->tag;
      c->done(c->done_arg, c);
      break;
    }

    grpc_cq_completion* c = cq_event_queue_pop(&cqd->queue);

    if (c != nullptr) {
      ret.type = GRPC_OP_COMPLETE;
      ret.success = c->next & 1u;
      ret.tag = c->tag;
      c->done(c->done_arg, c);
      break;
    } else {
      /* If c == NULL it means either the queue is empty OR in an transient
         inconsistent state. If it is the latter, we shold do a 0-timeout poll
         so that the thread comes back quickly from poll to make a second
         attempt at popping. Not doing this can potentially deadlock this
         thread forever (if the deadline is infinity) */
      if (cq_event_queue_num_items(&cqd->queue) > 0) {
        iteration_deadline = 0;
      }
    }

    if (gpr_atm_acq_load(&cqd->pending_events) == 0) {
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

    if (!is_finished_arg.first_loop &&
        grpc_core::ExecCtx::Get()->Now() >= deadline_millis) {
      memset(&ret, 0, sizeof(ret));
      ret.type = GRPC_QUEUE_TIMEOUT;
      dump_pending_tags(cq);
      break;
    }

    /* The main polling work happens in grpc_pollset_work */
    gpr_mu_lock(cq->mu);
    cq->num_polls++;
    grpc_error* err = cq->poller_vtable->work(POLLSET_FROM_CQ(cq), nullptr,
                                              iteration_deadline);
    gpr_mu_unlock(cq->mu);

    if (err != GRPC_ERROR_NONE) {
      const char* msg = grpc_error_string(err);
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
      gpr_atm_acq_load(&cqd->pending_events) > 0) {
    gpr_mu_lock(cq->mu);
    cq->poller_vtable->kick(POLLSET_FROM_CQ(cq), nullptr);
    gpr_mu_unlock(cq->mu);
  }

  GRPC_SURFACE_TRACE_RETURNED_EVENT(cq, &ret);
  GRPC_CQ_INTERNAL_UNREF(cq, "next");

  GPR_ASSERT(is_finished_arg.stolen_completion == nullptr);

  return ret;
}

/* Finishes the completion queue shutdown. This means that there are no more
   completion events / tags expected from the completion queue
   - Must be called under completion queue lock
   - Must be called only once in completion queue's lifetime
   - grpc_completion_queue_shutdown() MUST have been called before calling
   this function */
static void cq_finish_shutdown_next(grpc_completion_queue* cq) {
  cq_next_data* cqd = static_cast<cq_next_data*> DATA_FROM_CQ(cq);

  GPR_ASSERT(cqd->shutdown_called);
  GPR_ASSERT(gpr_atm_no_barrier_load(&cqd->pending_events) == 0);

  cq->poller_vtable->shutdown(POLLSET_FROM_CQ(cq), &cq->pollset_shutdown_done);
}

static void cq_shutdown_next(grpc_completion_queue* cq) {
  cq_next_data* cqd = static_cast<cq_next_data*> DATA_FROM_CQ(cq);

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
    GRPC_CQ_INTERNAL_UNREF(cq, "shutting_down");
    return;
  }
  cqd->shutdown_called = true;
  /* Doing a full_fetch_add (i.e acq/release) here to match with
   * cq_begin_op_for_next and and cq_end_op_for_next functions which read/write
   * on this counter without necessarily holding a lock on cq */
  if (gpr_atm_full_fetch_add(&cqd->pending_events, -1) == 1) {
    cq_finish_shutdown_next(cq);
  }
  gpr_mu_unlock(cq->mu);
  GRPC_CQ_INTERNAL_UNREF(cq, "shutting_down");
}

grpc_event grpc_completion_queue_next(grpc_completion_queue* cq,
                                      gpr_timespec deadline, void* reserved) {
  return cq->vtable->next(cq, deadline, reserved);
}

static int add_plucker(grpc_completion_queue* cq, void* tag,
                       grpc_pollset_worker** worker) {
  cq_pluck_data* cqd = static_cast<cq_pluck_data*> DATA_FROM_CQ(cq);
  if (cqd->num_pluckers == GRPC_MAX_COMPLETION_QUEUE_PLUCKERS) {
    return 0;
  }
  cqd->pluckers[cqd->num_pluckers].tag = tag;
  cqd->pluckers[cqd->num_pluckers].worker = worker;
  cqd->num_pluckers++;
  return 1;
}

static void del_plucker(grpc_completion_queue* cq, void* tag,
                        grpc_pollset_worker** worker) {
  cq_pluck_data* cqd = static_cast<cq_pluck_data*> DATA_FROM_CQ(cq);
  for (int i = 0; i < cqd->num_pluckers; i++) {
    if (cqd->pluckers[i].tag == tag && cqd->pluckers[i].worker == worker) {
      cqd->num_pluckers--;
      GPR_SWAP(plucker, cqd->pluckers[i], cqd->pluckers[cqd->num_pluckers]);
      return;
    }
  }
  GPR_UNREACHABLE_CODE(return );
}

class ExecCtxPluck : public grpc_core::ExecCtx {
 public:
  ExecCtxPluck(void* arg) : ExecCtx(0), check_ready_to_finish_arg_(arg) {}

  bool CheckReadyToFinish() override {
    cq_is_finished_arg* a =
        static_cast<cq_is_finished_arg*>(check_ready_to_finish_arg_);
    grpc_completion_queue* cq = a->cq;
    cq_pluck_data* cqd = static_cast<cq_pluck_data*> DATA_FROM_CQ(cq);

    GPR_ASSERT(a->stolen_completion == nullptr);
    gpr_atm current_last_seen_things_queued_ever =
        gpr_atm_no_barrier_load(&cqd->things_queued_ever);
    if (current_last_seen_things_queued_ever !=
        a->last_seen_things_queued_ever) {
      gpr_mu_lock(cq->mu);
      a->last_seen_things_queued_ever =
          gpr_atm_no_barrier_load(&cqd->things_queued_ever);
      grpc_cq_completion* c;
      grpc_cq_completion* prev = &cqd->completed_head;
      while ((c = (grpc_cq_completion*)(prev->next &
                                        ~static_cast<uintptr_t>(1))) !=
             &cqd->completed_head) {
        if (c->tag == a->tag) {
          prev->next = (prev->next & static_cast<uintptr_t>(1)) |
                       (c->next & ~static_cast<uintptr_t>(1));
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
    return !a->first_loop && a->deadline < grpc_core::ExecCtx::Get()->Now();
  }

 private:
  void* check_ready_to_finish_arg_;
};

static grpc_event cq_pluck(grpc_completion_queue* cq, void* tag,
                           gpr_timespec deadline, void* reserved) {
  GPR_TIMER_SCOPE("grpc_completion_queue_pluck", 0);

  grpc_event ret;
  grpc_cq_completion* c;
  grpc_cq_completion* prev;
  grpc_pollset_worker* worker = nullptr;
  cq_pluck_data* cqd = static_cast<cq_pluck_data*> DATA_FROM_CQ(cq);

  if (grpc_cq_pluck_trace.enabled()) {
    GRPC_API_TRACE(
        "grpc_completion_queue_pluck("
        "cq=%p, tag=%p, "
        "deadline=gpr_timespec { tv_sec: %" PRId64
        ", tv_nsec: %d, clock_type: %d }, "
        "reserved=%p)",
        6,
        (cq, tag, deadline.tv_sec, deadline.tv_nsec, (int)deadline.clock_type,
         reserved));
  }
  GPR_ASSERT(!reserved);

  dump_pending_tags(cq);

  GRPC_CQ_INTERNAL_REF(cq, "pluck");
  gpr_mu_lock(cq->mu);
  grpc_millis deadline_millis = grpc_timespec_to_millis_round_up(deadline);
  cq_is_finished_arg is_finished_arg = {
      gpr_atm_no_barrier_load(&cqd->things_queued_ever),
      cq,
      deadline_millis,
      nullptr,
      tag,
      true};
  ExecCtxPluck exec_ctx(&is_finished_arg);
  for (;;) {
    if (is_finished_arg.stolen_completion != nullptr) {
      gpr_mu_unlock(cq->mu);
      c = is_finished_arg.stolen_completion;
      is_finished_arg.stolen_completion = nullptr;
      ret.type = GRPC_OP_COMPLETE;
      ret.success = c->next & 1u;
      ret.tag = c->tag;
      c->done(c->done_arg, c);
      break;
    }
    prev = &cqd->completed_head;
    while (
        (c = (grpc_cq_completion*)(prev->next & ~static_cast<uintptr_t>(1))) !=
        &cqd->completed_head) {
      if (c->tag == tag) {
        prev->next = (prev->next & static_cast<uintptr_t>(1)) |
                     (c->next & ~static_cast<uintptr_t>(1));
        if (c == cqd->completed_tail) {
          cqd->completed_tail = prev;
        }
        gpr_mu_unlock(cq->mu);
        ret.type = GRPC_OP_COMPLETE;
        ret.success = c->next & 1u;
        ret.tag = c->tag;
        c->done(c->done_arg, c);
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
    if (!is_finished_arg.first_loop &&
        grpc_core::ExecCtx::Get()->Now() >= deadline_millis) {
      del_plucker(cq, tag, &worker);
      gpr_mu_unlock(cq->mu);
      memset(&ret, 0, sizeof(ret));
      ret.type = GRPC_QUEUE_TIMEOUT;
      dump_pending_tags(cq);
      break;
    }
    cq->num_polls++;
    grpc_error* err =
        cq->poller_vtable->work(POLLSET_FROM_CQ(cq), &worker, deadline_millis);
    if (err != GRPC_ERROR_NONE) {
      del_plucker(cq, tag, &worker);
      gpr_mu_unlock(cq->mu);
      const char* msg = grpc_error_string(err);
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
  GRPC_CQ_INTERNAL_UNREF(cq, "pluck");

  GPR_ASSERT(is_finished_arg.stolen_completion == nullptr);

  return ret;
}

grpc_event grpc_completion_queue_pluck(grpc_completion_queue* cq, void* tag,
                                       gpr_timespec deadline, void* reserved) {
  return cq->vtable->pluck(cq, tag, deadline, reserved);
}

static void cq_finish_shutdown_pluck(grpc_completion_queue* cq) {
  cq_pluck_data* cqd = static_cast<cq_pluck_data*> DATA_FROM_CQ(cq);

  GPR_ASSERT(cqd->shutdown_called);
  GPR_ASSERT(!gpr_atm_no_barrier_load(&cqd->shutdown));
  gpr_atm_no_barrier_store(&cqd->shutdown, 1);

  cq->poller_vtable->shutdown(POLLSET_FROM_CQ(cq), &cq->pollset_shutdown_done);
}

/* NOTE: This function is almost exactly identical to cq_shutdown_next() but
 * merging them is a bit tricky and probably not worth it */
static void cq_shutdown_pluck(grpc_completion_queue* cq) {
  cq_pluck_data* cqd = static_cast<cq_pluck_data*> DATA_FROM_CQ(cq);

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
    GRPC_CQ_INTERNAL_UNREF(cq, "shutting_down (pluck cq)");
    return;
  }
  cqd->shutdown_called = true;
  if (gpr_atm_full_fetch_add(&cqd->pending_events, -1) == 1) {
    cq_finish_shutdown_pluck(cq);
  }
  gpr_mu_unlock(cq->mu);
  GRPC_CQ_INTERNAL_UNREF(cq, "shutting_down (pluck cq)");
}

static void cq_finish_shutdown_callback(grpc_completion_queue* cq) {
  cq_callback_data* cqd = static_cast<cq_callback_data*> DATA_FROM_CQ(cq);
  auto* callback = cqd->shutdown_callback;

  GPR_ASSERT(cqd->shutdown_called);

  cq->poller_vtable->shutdown(POLLSET_FROM_CQ(cq), &cq->pollset_shutdown_done);
  callback->Run(true);
}

static void cq_shutdown_callback(grpc_completion_queue* cq) {
  cq_callback_data* cqd = static_cast<cq_callback_data*> DATA_FROM_CQ(cq);

  /* Need an extra ref for cq here because:
   * We call cq_finish_shutdown_callback() below, which calls pollset shutdown.
   * Pollset shutdown decrements the cq ref count which can potentially destroy
   * the cq (if that happens to be the last ref).
   * Creating an extra ref here prevents the cq from getting destroyed while
   * this function is still active */
  GRPC_CQ_INTERNAL_REF(cq, "shutting_down (callback cq)");
  gpr_mu_lock(cq->mu);
  if (cqd->shutdown_called) {
    gpr_mu_unlock(cq->mu);
    GRPC_CQ_INTERNAL_UNREF(cq, "shutting_down (callback cq)");
    return;
  }
  cqd->shutdown_called = true;
  if (gpr_atm_full_fetch_add(&cqd->pending_events, -1) == 1) {
    cq_finish_shutdown_callback(cq);
  }
  gpr_mu_unlock(cq->mu);
  GRPC_CQ_INTERNAL_UNREF(cq, "shutting_down (callback cq)");
}

/* Shutdown simply drops a ref that we reserved at creation time; if we drop
   to zero here, then enter shutdown mode and wake up any waiters */
void grpc_completion_queue_shutdown(grpc_completion_queue* cq) {
  GPR_TIMER_SCOPE("grpc_completion_queue_shutdown", 0);
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE("grpc_completion_queue_shutdown(cq=%p)", 1, (cq));
  cq->vtable->shutdown(cq);
}

void grpc_completion_queue_destroy(grpc_completion_queue* cq) {
  GPR_TIMER_SCOPE("grpc_completion_queue_destroy", 0);
  GRPC_API_TRACE("grpc_completion_queue_destroy(cq=%p)", 1, (cq));
  grpc_completion_queue_shutdown(cq);

  grpc_core::ExecCtx exec_ctx;
  GRPC_CQ_INTERNAL_UNREF(cq, "destroy");
}

grpc_pollset* grpc_cq_pollset(grpc_completion_queue* cq) {
  return cq->poller_vtable->can_get_pollset ? POLLSET_FROM_CQ(cq) : nullptr;
}

bool grpc_cq_can_listen(grpc_completion_queue* cq) {
  return cq->poller_vtable->can_listen;
}
