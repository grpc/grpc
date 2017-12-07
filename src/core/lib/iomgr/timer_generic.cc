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

#include "src/core/lib/iomgr/port.h"

#include <inttypes.h>

#ifdef GRPC_TIMER_USE_GENERIC

#include "src/core/lib/iomgr/timer.h"

#include <grpc/support/alloc.h>
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/tls.h>
#include <grpc/support/useful.h>
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/time_averaged_stats.h"
#include "src/core/lib/iomgr/timer_heap.h"
#include "src/core/lib/support/spinlock.h"

#define INVALID_HEAP_INDEX 0xffffffffu

#define ADD_DEADLINE_SCALE 0.33
#define MIN_QUEUE_WINDOW_DURATION 0.01
#define MAX_QUEUE_WINDOW_DURATION 1

grpc_core::TraceFlag grpc_timer_trace(false, "timer");
grpc_core::TraceFlag grpc_timer_check_trace(false, "timer_check");

/* A "timer shard". Contains a 'heap' and a 'list' of timers. All timers with
 * deadlines earlier than 'queue_deadline" cap are maintained in the heap and
 * others are maintained in the list (unordered). This helps to keep the number
 * of elements in the heap low.
 *
 * The 'queue_deadline_cap' gets recomputed periodically based on the timer
 * stats maintained in 'stats' and the relevant timers are then moved from the
 * 'list' to 'heap'
 */
typedef struct {
  gpr_mu mu;
  grpc_time_averaged_stats stats;
  /* All and only timers with deadlines <= this will be in the heap. */
  gpr_atm queue_deadline_cap;
  /* The deadline of the next timer due in this shard */
  gpr_atm min_deadline;
  /* Index of this timer_shard in the g_shard_queue */
  uint32_t shard_queue_index;
  /* This holds all timers with deadlines < queue_deadline_cap. Timers in this
     list have the top bit of their deadline set to 0. */
  grpc_timer_heap heap;
  /* This holds timers whose deadline is >= queue_deadline_cap. */
  grpc_timer list;
} timer_shard;

static size_t g_num_shards;

/* Array of timer shards. Whenever a timer (grpc_timer *) is added, its address
 * is hashed to select the timer shard to add the timer to */
static timer_shard* g_shards;

/* Maintains a sorted list of timer shards (sorted by their min_deadline, i.e
 * the deadline of the next timer in each shard).
 * Access to this is protected by g_shared_mutables.mu */
static timer_shard** g_shard_queue;

#ifndef NDEBUG

/* == Hash table for duplicate timer detection == */

#define NUM_HASH_BUCKETS 1009 /* Prime number close to 1000 */

static gpr_mu g_hash_mu[NUM_HASH_BUCKETS]; /* One mutex per bucket */
static grpc_timer* g_timer_ht[NUM_HASH_BUCKETS] = {nullptr};

static void init_timer_ht() {
  for (int i = 0; i < NUM_HASH_BUCKETS; i++) {
    gpr_mu_init(&g_hash_mu[i]);
  }
}

static bool is_in_ht(grpc_timer* t) {
  size_t i = GPR_HASH_POINTER(t, NUM_HASH_BUCKETS);

  gpr_mu_lock(&g_hash_mu[i]);
  grpc_timer* p = g_timer_ht[i];
  while (p != nullptr && p != t) {
    p = p->hash_table_next;
  }
  gpr_mu_unlock(&g_hash_mu[i]);

  return (p == t);
}

static void add_to_ht(grpc_timer* t) {
  GPR_ASSERT(!t->hash_table_next);
  size_t i = GPR_HASH_POINTER(t, NUM_HASH_BUCKETS);

  gpr_mu_lock(&g_hash_mu[i]);
  grpc_timer* p = g_timer_ht[i];
  while (p != nullptr && p != t) {
    p = p->hash_table_next;
  }

  if (p == t) {
    grpc_closure* c = t->closure;
    gpr_log(GPR_ERROR,
            "** Duplicate timer (%p) being added. Closure: (%p), created at: "
            "(%s:%d), scheduled at: (%s:%d) **",
            t, c, c->file_created, c->line_created, c->file_initiated,
            c->line_initiated);
    abort();
  }

  /* Timer not present in the bucket. Insert at head of the list */
  t->hash_table_next = g_timer_ht[i];
  g_timer_ht[i] = t;
  gpr_mu_unlock(&g_hash_mu[i]);
}

static void remove_from_ht(grpc_timer* t) {
  size_t i = GPR_HASH_POINTER(t, NUM_HASH_BUCKETS);
  bool removed = false;

  gpr_mu_lock(&g_hash_mu[i]);
  if (g_timer_ht[i] == t) {
    g_timer_ht[i] = g_timer_ht[i]->hash_table_next;
    removed = true;
  } else if (g_timer_ht[i] != nullptr) {
    grpc_timer* p = g_timer_ht[i];
    while (p->hash_table_next != nullptr && p->hash_table_next != t) {
      p = p->hash_table_next;
    }

    if (p->hash_table_next == t) {
      p->hash_table_next = t->hash_table_next;
      removed = true;
    }
  }
  gpr_mu_unlock(&g_hash_mu[i]);

  if (!removed) {
    grpc_closure* c = t->closure;
    gpr_log(GPR_ERROR,
            "** Removing timer (%p) that is not added to hash table. Closure "
            "(%p), created at: (%s:%d), scheduled at: (%s:%d) **",
            t, c, c->file_created, c->line_created, c->file_initiated,
            c->line_initiated);
    abort();
  }

  t->hash_table_next = nullptr;
}

/* If a timer is added to a timer shard (either heap or a list), it cannot
 * be pending. A timer is added to hash table only-if it is added to the
 * timer shard.
 * Therefore, if timer->pending is false, it cannot be in hash table */
static void validate_non_pending_timer(grpc_timer* t) {
  if (!t->pending && is_in_ht(t)) {
    grpc_closure* c = t->closure;
    gpr_log(GPR_ERROR,
            "** gpr_timer_cancel() called on a non-pending timer (%p) which "
            "is in the hash table. Closure: (%p), created at: (%s:%d), "
            "scheduled at: (%s:%d) **",
            t, c, c->file_created, c->line_created, c->file_initiated,
            c->line_initiated);
    abort();
  }
}

#define INIT_TIMER_HASH_TABLE() init_timer_ht()
#define ADD_TO_HASH_TABLE(t) add_to_ht((t))
#define REMOVE_FROM_HASH_TABLE(t) remove_from_ht((t))
#define VALIDATE_NON_PENDING_TIMER(t) validate_non_pending_timer((t))

#else

#define INIT_TIMER_HASH_TABLE()
#define ADD_TO_HASH_TABLE(t)
#define REMOVE_FROM_HASH_TABLE(t)
#define VALIDATE_NON_PENDING_TIMER(t)

#endif

/* Thread local variable that stores the deadline of the next timer the thread
 * has last-seen. This is an optimization to prevent the thread from checking
 * shared_mutables.min_timer (which requires acquiring shared_mutables.mu lock,
 * an expensive operation) */
GPR_TLS_DECL(g_last_seen_min_timer);

struct shared_mutables {
  /* The deadline of the next timer due across all timer shards */
  gpr_atm min_timer;
  /* Allow only one run_some_expired_timers at once */
  gpr_spinlock checker_mu;
  bool initialized;
  /* Protects g_shard_queue (and the shared_mutables struct itself) */
  gpr_mu mu;
} GPR_ALIGN_STRUCT(GPR_CACHELINE_SIZE);

static struct shared_mutables g_shared_mutables;

static gpr_atm saturating_add(gpr_atm a, gpr_atm b) {
  if (a > GPR_ATM_MAX - b) {
    return GPR_ATM_MAX;
  }
  return a + b;
}

static grpc_timer_check_result run_some_expired_timers(grpc_exec_ctx* exec_ctx,
                                                       gpr_atm now,
                                                       gpr_atm* next,
                                                       grpc_error* error);

static gpr_atm compute_min_deadline(timer_shard* shard) {
  return grpc_timer_heap_is_empty(&shard->heap)
             ? saturating_add(shard->queue_deadline_cap, 1)
             : grpc_timer_heap_top(&shard->heap)->deadline;
}

void grpc_timer_list_init(grpc_exec_ctx* exec_ctx) {
  uint32_t i;

  g_num_shards = GPR_MIN(1, 2 * gpr_cpu_num_cores());
  g_shards = (timer_shard*)gpr_zalloc(g_num_shards * sizeof(*g_shards));
  g_shard_queue =
      (timer_shard**)gpr_zalloc(g_num_shards * sizeof(*g_shard_queue));

  g_shared_mutables.initialized = true;
  g_shared_mutables.checker_mu = GPR_SPINLOCK_INITIALIZER;
  gpr_mu_init(&g_shared_mutables.mu);
  g_shared_mutables.min_timer = grpc_exec_ctx_now(exec_ctx);
  gpr_tls_init(&g_last_seen_min_timer);
  gpr_tls_set(&g_last_seen_min_timer, 0);

  for (i = 0; i < g_num_shards; i++) {
    timer_shard* shard = &g_shards[i];
    gpr_mu_init(&shard->mu);
    grpc_time_averaged_stats_init(&shard->stats, 1.0 / ADD_DEADLINE_SCALE, 0.1,
                                  0.5);
    shard->queue_deadline_cap = g_shared_mutables.min_timer;
    shard->shard_queue_index = i;
    grpc_timer_heap_init(&shard->heap);
    shard->list.next = shard->list.prev = &shard->list;
    shard->min_deadline = compute_min_deadline(shard);
    g_shard_queue[i] = shard;
  }

  INIT_TIMER_HASH_TABLE();
}

void grpc_timer_list_shutdown(grpc_exec_ctx* exec_ctx) {
  size_t i;
  run_some_expired_timers(
      exec_ctx, GPR_ATM_MAX, nullptr,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Timer list shutdown"));
  for (i = 0; i < g_num_shards; i++) {
    timer_shard* shard = &g_shards[i];
    gpr_mu_destroy(&shard->mu);
    grpc_timer_heap_destroy(&shard->heap);
  }
  gpr_mu_destroy(&g_shared_mutables.mu);
  gpr_tls_destroy(&g_last_seen_min_timer);
  gpr_free(g_shards);
  gpr_free(g_shard_queue);
  g_shared_mutables.initialized = false;
}

/* returns true if the first element in the list */
static void list_join(grpc_timer* head, grpc_timer* timer) {
  timer->next = head;
  timer->prev = head->prev;
  timer->next->prev = timer->prev->next = timer;
}

static void list_remove(grpc_timer* timer) {
  timer->next->prev = timer->prev;
  timer->prev->next = timer->next;
}

static void swap_adjacent_shards_in_queue(uint32_t first_shard_queue_index) {
  timer_shard* temp;
  temp = g_shard_queue[first_shard_queue_index];
  g_shard_queue[first_shard_queue_index] =
      g_shard_queue[first_shard_queue_index + 1];
  g_shard_queue[first_shard_queue_index + 1] = temp;
  g_shard_queue[first_shard_queue_index]->shard_queue_index =
      first_shard_queue_index;
  g_shard_queue[first_shard_queue_index + 1]->shard_queue_index =
      first_shard_queue_index + 1;
}

static void note_deadline_change(timer_shard* shard) {
  while (shard->shard_queue_index > 0 &&
         shard->min_deadline <
             g_shard_queue[shard->shard_queue_index - 1]->min_deadline) {
    swap_adjacent_shards_in_queue(shard->shard_queue_index - 1);
  }
  while (shard->shard_queue_index < g_num_shards - 1 &&
         shard->min_deadline >
             g_shard_queue[shard->shard_queue_index + 1]->min_deadline) {
    swap_adjacent_shards_in_queue(shard->shard_queue_index);
  }
}

void grpc_timer_init_unset(grpc_timer* timer) { timer->pending = false; }

void grpc_timer_init(grpc_exec_ctx* exec_ctx, grpc_timer* timer,
                     grpc_millis deadline, grpc_closure* closure) {
  int is_first_timer = 0;
  timer_shard* shard = &g_shards[GPR_HASH_POINTER(timer, g_num_shards)];
  timer->closure = closure;
  timer->deadline = deadline;

#ifndef NDEBUG
  timer->hash_table_next = nullptr;
#endif

  if (grpc_timer_trace.enabled()) {
    gpr_log(GPR_DEBUG,
            "TIMER %p: SET %" PRIdPTR " now %" PRIdPTR " call %p[%p]", timer,
            deadline, grpc_exec_ctx_now(exec_ctx), closure, closure->cb);
  }

  if (!g_shared_mutables.initialized) {
    timer->pending = false;
    GRPC_CLOSURE_SCHED(exec_ctx, timer->closure,
                       GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                           "Attempt to create timer before initialization"));
    return;
  }

  gpr_mu_lock(&shard->mu);
  timer->pending = true;
  grpc_millis now = grpc_exec_ctx_now(exec_ctx);
  if (deadline <= now) {
    timer->pending = false;
    GRPC_CLOSURE_SCHED(exec_ctx, timer->closure, GRPC_ERROR_NONE);
    gpr_mu_unlock(&shard->mu);
    /* early out */
    return;
  }

  grpc_time_averaged_stats_add_sample(&shard->stats,
                                      (double)(deadline - now) / 1000.0);

  ADD_TO_HASH_TABLE(timer);

  if (deadline < shard->queue_deadline_cap) {
    is_first_timer = grpc_timer_heap_add(&shard->heap, timer);
  } else {
    timer->heap_index = INVALID_HEAP_INDEX;
    list_join(&shard->list, timer);
  }
  if (grpc_timer_trace.enabled()) {
    gpr_log(GPR_DEBUG,
            "  .. add to shard %d with queue_deadline_cap=%" PRIdPTR
            " => is_first_timer=%s",
            (int)(shard - g_shards), shard->queue_deadline_cap,
            is_first_timer ? "true" : "false");
  }
  gpr_mu_unlock(&shard->mu);

  /* Deadline may have decreased, we need to adjust the master queue.  Note
     that there is a potential racy unlocked region here.  There could be a
     reordering of multiple grpc_timer_init calls, at this point, but the < test
     below should ensure that we err on the side of caution.  There could
     also be a race with grpc_timer_check, which might beat us to the lock.  In
     that case, it is possible that the timer that we added will have already
     run by the time we hold the lock, but that too is a safe error.
     Finally, it's possible that the grpc_timer_check that intervened failed to
     trigger the new timer because the min_deadline hadn't yet been reduced.
     In that case, the timer will simply have to wait for the next
     grpc_timer_check. */
  if (is_first_timer) {
    gpr_mu_lock(&g_shared_mutables.mu);
    if (grpc_timer_trace.enabled()) {
      gpr_log(GPR_DEBUG, "  .. old shard min_deadline=%" PRIdPTR,
              shard->min_deadline);
    }
    if (deadline < shard->min_deadline) {
      gpr_atm old_min_deadline = g_shard_queue[0]->min_deadline;
      shard->min_deadline = deadline;
      note_deadline_change(shard);
      if (shard->shard_queue_index == 0 && deadline < old_min_deadline) {
        gpr_atm_no_barrier_store(&g_shared_mutables.min_timer, deadline);
        grpc_kick_poller();
      }
    }
    gpr_mu_unlock(&g_shared_mutables.mu);
  }
}

void grpc_timer_consume_kick(void) {
  /* force re-evaluation of last seeen min */
  gpr_tls_set(&g_last_seen_min_timer, 0);
}

void grpc_timer_cancel(grpc_exec_ctx* exec_ctx, grpc_timer* timer) {
  if (!g_shared_mutables.initialized) {
    /* must have already been cancelled, also the shard mutex is invalid */
    return;
  }

  timer_shard* shard = &g_shards[GPR_HASH_POINTER(timer, g_num_shards)];
  gpr_mu_lock(&shard->mu);
  if (grpc_timer_trace.enabled()) {
    gpr_log(GPR_DEBUG, "TIMER %p: CANCEL pending=%s", timer,
            timer->pending ? "true" : "false");
  }

  if (timer->pending) {
    REMOVE_FROM_HASH_TABLE(timer);

    GRPC_CLOSURE_SCHED(exec_ctx, timer->closure, GRPC_ERROR_CANCELLED);
    timer->pending = false;
    if (timer->heap_index == INVALID_HEAP_INDEX) {
      list_remove(timer);
    } else {
      grpc_timer_heap_remove(&shard->heap, timer);
    }
  } else {
    VALIDATE_NON_PENDING_TIMER(timer);
  }
  gpr_mu_unlock(&shard->mu);
}

/* Rebalances the timer shard by computing a new 'queue_deadline_cap' and moving
   all relevant timers in shard->list (i.e timers with deadlines earlier than
   'queue_deadline_cap') into into shard->heap.
   Returns 'true' if shard->heap has atleast ONE element
   REQUIRES: shard->mu locked */
static int refill_heap(timer_shard* shard, gpr_atm now) {
  /* Compute the new queue window width and bound by the limits: */
  double computed_deadline_delta =
      grpc_time_averaged_stats_update_average(&shard->stats) *
      ADD_DEADLINE_SCALE;
  double deadline_delta =
      GPR_CLAMP(computed_deadline_delta, MIN_QUEUE_WINDOW_DURATION,
                MAX_QUEUE_WINDOW_DURATION);
  grpc_timer *timer, *next;

  /* Compute the new cap and put all timers under it into the queue: */
  shard->queue_deadline_cap =
      saturating_add(GPR_MAX(now, shard->queue_deadline_cap),
                     (gpr_atm)(deadline_delta * 1000.0));

  if (grpc_timer_check_trace.enabled()) {
    gpr_log(GPR_DEBUG, "  .. shard[%d]->queue_deadline_cap --> %" PRIdPTR,
            (int)(shard - g_shards), shard->queue_deadline_cap);
  }
  for (timer = shard->list.next; timer != &shard->list; timer = next) {
    next = timer->next;

    if (timer->deadline < shard->queue_deadline_cap) {
      if (grpc_timer_check_trace.enabled()) {
        gpr_log(GPR_DEBUG, "  .. add timer with deadline %" PRIdPTR " to heap",
                timer->deadline);
      }
      list_remove(timer);
      grpc_timer_heap_add(&shard->heap, timer);
    }
  }
  return !grpc_timer_heap_is_empty(&shard->heap);
}

/* This pops the next non-cancelled timer with deadline <= now from the
   queue, or returns NULL if there isn't one.
   REQUIRES: shard->mu locked */
static grpc_timer* pop_one(timer_shard* shard, gpr_atm now) {
  grpc_timer* timer;
  for (;;) {
    if (grpc_timer_check_trace.enabled()) {
      gpr_log(GPR_DEBUG, "  .. shard[%d]: heap_empty=%s",
              (int)(shard - g_shards),
              grpc_timer_heap_is_empty(&shard->heap) ? "true" : "false");
    }
    if (grpc_timer_heap_is_empty(&shard->heap)) {
      if (now < shard->queue_deadline_cap) return nullptr;
      if (!refill_heap(shard, now)) return nullptr;
    }
    timer = grpc_timer_heap_top(&shard->heap);
    if (grpc_timer_check_trace.enabled()) {
      gpr_log(GPR_DEBUG,
              "  .. check top timer deadline=%" PRIdPTR " now=%" PRIdPTR,
              timer->deadline, now);
    }
    if (timer->deadline > now) return nullptr;
    if (grpc_timer_trace.enabled()) {
      gpr_log(GPR_DEBUG, "TIMER %p: FIRE %" PRIdPTR "ms late via %s scheduler",
              timer, now - timer->deadline,
              timer->closure->scheduler->vtable->name);
    }
    timer->pending = false;
    grpc_timer_heap_pop(&shard->heap);
    return timer;
  }
}

/* REQUIRES: shard->mu unlocked */
static size_t pop_timers(grpc_exec_ctx* exec_ctx, timer_shard* shard,
                         gpr_atm now, gpr_atm* new_min_deadline,
                         grpc_error* error) {
  size_t n = 0;
  grpc_timer* timer;
  gpr_mu_lock(&shard->mu);
  while ((timer = pop_one(shard, now))) {
    REMOVE_FROM_HASH_TABLE(timer);
    GRPC_CLOSURE_SCHED(exec_ctx, timer->closure, GRPC_ERROR_REF(error));
    n++;
  }
  *new_min_deadline = compute_min_deadline(shard);
  gpr_mu_unlock(&shard->mu);
  if (grpc_timer_check_trace.enabled()) {
    gpr_log(GPR_DEBUG, "  .. shard[%d] popped %" PRIdPTR,
            (int)(shard - g_shards), n);
  }
  return n;
}

static grpc_timer_check_result run_some_expired_timers(grpc_exec_ctx* exec_ctx,
                                                       gpr_atm now,
                                                       gpr_atm* next,
                                                       grpc_error* error) {
  grpc_timer_check_result result = GRPC_TIMERS_NOT_CHECKED;

  gpr_atm min_timer = gpr_atm_no_barrier_load(&g_shared_mutables.min_timer);
  gpr_tls_set(&g_last_seen_min_timer, min_timer);
  if (now < min_timer) {
    if (next != nullptr) *next = GPR_MIN(*next, min_timer);
    return GRPC_TIMERS_CHECKED_AND_EMPTY;
  }

  if (gpr_spinlock_trylock(&g_shared_mutables.checker_mu)) {
    gpr_mu_lock(&g_shared_mutables.mu);
    result = GRPC_TIMERS_CHECKED_AND_EMPTY;

    if (grpc_timer_check_trace.enabled()) {
      gpr_log(GPR_DEBUG, "  .. shard[%d]->min_deadline = %" PRIdPTR,
              (int)(g_shard_queue[0] - g_shards),
              g_shard_queue[0]->min_deadline);
    }

    while (g_shard_queue[0]->min_deadline < now ||
           (now != GPR_ATM_MAX && g_shard_queue[0]->min_deadline == now)) {
      gpr_atm new_min_deadline;

      /* For efficiency, we pop as many available timers as we can from the
         shard.  This may violate perfect timer deadline ordering, but that
         shouldn't be a big deal because we don't make ordering guarantees. */
      if (pop_timers(exec_ctx, g_shard_queue[0], now, &new_min_deadline,
                     error) > 0) {
        result = GRPC_TIMERS_FIRED;
      }

      if (grpc_timer_check_trace.enabled()) {
        gpr_log(GPR_DEBUG,
                "  .. result --> %d"
                ", shard[%d]->min_deadline %" PRIdPTR " --> %" PRIdPTR
                ", now=%" PRIdPTR,
                result, (int)(g_shard_queue[0] - g_shards),
                g_shard_queue[0]->min_deadline, new_min_deadline, now);
      }

      /* An grpc_timer_init() on the shard could intervene here, adding a new
         timer that is earlier than new_min_deadline.  However,
         grpc_timer_init() will block on the master_lock before it can call
         set_min_deadline, so this one will complete first and then the Addtimer
         will reduce the min_deadline (perhaps unnecessarily). */
      g_shard_queue[0]->min_deadline = new_min_deadline;
      note_deadline_change(g_shard_queue[0]);
    }

    if (next) {
      *next = GPR_MIN(*next, g_shard_queue[0]->min_deadline);
    }

    gpr_atm_no_barrier_store(&g_shared_mutables.min_timer,
                             g_shard_queue[0]->min_deadline);
    gpr_mu_unlock(&g_shared_mutables.mu);
    gpr_spinlock_unlock(&g_shared_mutables.checker_mu);
  }

  GRPC_ERROR_UNREF(error);

  return result;
}

grpc_timer_check_result grpc_timer_check(grpc_exec_ctx* exec_ctx,
                                         grpc_millis* next) {
  // prelude
  grpc_millis now = grpc_exec_ctx_now(exec_ctx);

  /* fetch from a thread-local first: this avoids contention on a globally
     mutable cacheline in the common case */
  grpc_millis min_timer = gpr_tls_get(&g_last_seen_min_timer);
  if (now < min_timer) {
    if (next != nullptr) {
      *next = GPR_MIN(*next, min_timer);
    }
    if (grpc_timer_check_trace.enabled()) {
      gpr_log(GPR_DEBUG,
              "TIMER CHECK SKIP: now=%" PRIdPTR " min_timer=%" PRIdPTR, now,
              min_timer);
    }
    return GRPC_TIMERS_CHECKED_AND_EMPTY;
  }

  grpc_error* shutdown_error =
      now != GRPC_MILLIS_INF_FUTURE
          ? GRPC_ERROR_NONE
          : GRPC_ERROR_CREATE_FROM_STATIC_STRING("Shutting down timer system");

  // tracing
  if (grpc_timer_check_trace.enabled()) {
    char* next_str;
    if (next == nullptr) {
      next_str = gpr_strdup("NULL");
    } else {
      gpr_asprintf(&next_str, "%" PRIdPTR, *next);
    }
    gpr_log(GPR_DEBUG,
            "TIMER CHECK BEGIN: now=%" PRIdPTR " next=%s tls_min=%" PRIdPTR
            " glob_min=%" PRIdPTR,
            now, next_str, gpr_tls_get(&g_last_seen_min_timer),
            gpr_atm_no_barrier_load(&g_shared_mutables.min_timer));
    gpr_free(next_str);
  }
  // actual code
  grpc_timer_check_result r =
      run_some_expired_timers(exec_ctx, now, next, shutdown_error);
  // tracing
  if (grpc_timer_check_trace.enabled()) {
    char* next_str;
    if (next == nullptr) {
      next_str = gpr_strdup("NULL");
    } else {
      gpr_asprintf(&next_str, "%" PRIdPTR, *next);
    }
    gpr_log(GPR_DEBUG, "TIMER CHECK END: r=%d; next=%s", r, next_str);
    gpr_free(next_str);
  }
  return r;
}

#endif /* GRPC_TIMER_USE_GENERIC */
