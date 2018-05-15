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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#include <inttypes.h>

#include "src/core/lib/iomgr/timer.h"

#include <grpc/support/alloc.h>
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/spinlock.h"
#include "src/core/lib/gpr/tls.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/time_averaged_stats.h"
#include "src/core/lib/iomgr/timer_heap.h"

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
  grpc_millis queue_deadline_cap;
  /* The deadline of the next timer due in this shard */
  grpc_millis min_deadline;
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

static void destroy_timer_ht() {
  for (int i = 0; i < NUM_HASH_BUCKETS; i++) {
    gpr_mu_destroy(&g_hash_mu[i]);
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
#define DESTROY_TIMER_HASH_TABLE() destroy_timer_ht()
#define ADD_TO_HASH_TABLE(t) add_to_ht((t))
#define REMOVE_FROM_HASH_TABLE(t) remove_from_ht((t))
#define VALIDATE_NON_PENDING_TIMER(t) validate_non_pending_timer((t))

#else

#define INIT_TIMER_HASH_TABLE()
#define DESTROY_TIMER_HASH_TABLE()
#define ADD_TO_HASH_TABLE(t)
#define REMOVE_FROM_HASH_TABLE(t)
#define VALIDATE_NON_PENDING_TIMER(t)

#endif

#if GPR_ARCH_64
/* NOTE: TODO(sreek) - Currently the thread local storage support in grpc is
   for intptr_t which means on 32-bit machines it is not wide enough to hold
   grpc_millis which is 64-bit. Adding thread local support for 64 bit values
   is a lot of work for very little gain. So we are currently restricting this
   optimization to only 64 bit machines */

/* Thread local variable that stores the deadline of the next timer the thread
 * has last-seen. This is an optimization to prevent the thread from checking
 * shared_mutables.min_timer (which requires acquiring shared_mutables.mu lock,
 * an expensive operation) */
GPR_TLS_DECL(g_last_seen_min_timer);
#endif

struct shared_mutables {
  /* The deadline of the next timer due across all timer shards */
  grpc_millis min_timer;
  /* Allow only one run_some_expired_timers at once */
  gpr_spinlock checker_mu;
  bool initialized;
  /* Protects g_shard_queue (and the shared_mutables struct itself) */
  gpr_mu mu;
} GPR_ALIGN_STRUCT(GPR_CACHELINE_SIZE);

static struct shared_mutables g_shared_mutables;

static grpc_millis saturating_add(grpc_millis a, grpc_millis b) {
  if (a > GRPC_MILLIS_INF_FUTURE - b) {
    return GRPC_MILLIS_INF_FUTURE;
  }
  return a + b;
}

static grpc_timer_check_result run_some_expired_timers(grpc_millis now,
                                                       grpc_millis* next,
                                                       grpc_error* error);

static grpc_millis compute_min_deadline(timer_shard* shard) {
  return grpc_timer_heap_is_empty(&shard->heap)
             ? saturating_add(shard->queue_deadline_cap, 1)
             : grpc_timer_heap_top(&shard->heap)->deadline;
}

static void timer_list_init() {
  uint32_t i;

  g_num_shards = GPR_MIN(1, 2 * gpr_cpu_num_cores());
  g_shards =
      static_cast<timer_shard*>(gpr_zalloc(g_num_shards * sizeof(*g_shards)));
  g_shard_queue = static_cast<timer_shard**>(
      gpr_zalloc(g_num_shards * sizeof(*g_shard_queue)));

  g_shared_mutables.initialized = true;
  g_shared_mutables.checker_mu = GPR_SPINLOCK_INITIALIZER;
  gpr_mu_init(&g_shared_mutables.mu);
  g_shared_mutables.min_timer = grpc_core::ExecCtx::Get()->Now();

#if GPR_ARCH_64
  gpr_tls_init(&g_last_seen_min_timer);
  gpr_tls_set(&g_last_seen_min_timer, 0);
#endif

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

static void timer_list_shutdown() {
  size_t i;
  run_some_expired_timers(
      GPR_ATM_MAX, nullptr,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Timer list shutdown"));
  for (i = 0; i < g_num_shards; i++) {
    timer_shard* shard = &g_shards[i];
    gpr_mu_destroy(&shard->mu);
    grpc_timer_heap_destroy(&shard->heap);
  }
  gpr_mu_destroy(&g_shared_mutables.mu);

#if GPR_ARCH_64
  gpr_tls_destroy(&g_last_seen_min_timer);
#endif

  gpr_free(g_shards);
  gpr_free(g_shard_queue);
  g_shared_mutables.initialized = false;

  DESTROY_TIMER_HASH_TABLE();
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

static void timer_init(grpc_timer* timer, grpc_millis deadline,
                       grpc_closure* closure) {
  int is_first_timer = 0;
  timer_shard* shard = &g_shards[GPR_HASH_POINTER(timer, g_num_shards)];
  timer->closure = closure;
  timer->deadline = deadline;

#ifndef NDEBUG
  timer->hash_table_next = nullptr;
#endif

  if (grpc_timer_trace.enabled()) {
    gpr_log(GPR_INFO, "TIMER %p: SET %" PRId64 " now %" PRId64 " call %p[%p]",
            timer, deadline, grpc_core::ExecCtx::Get()->Now(), closure,
            closure->cb);
  }

  if (!g_shared_mutables.initialized) {
    timer->pending = false;
    GRPC_CLOSURE_SCHED(timer->closure,
                       GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                           "Attempt to create timer before initialization"));
    return;
  }

  gpr_mu_lock(&shard->mu);
  timer->pending = true;
  grpc_millis now = grpc_core::ExecCtx::Get()->Now();
  if (deadline <= now) {
    timer->pending = false;
    GRPC_CLOSURE_SCHED(timer->closure, GRPC_ERROR_NONE);
    gpr_mu_unlock(&shard->mu);
    /* early out */
    return;
  }

  grpc_time_averaged_stats_add_sample(
      &shard->stats, static_cast<double>(deadline - now) / 1000.0);

  ADD_TO_HASH_TABLE(timer);

  if (deadline < shard->queue_deadline_cap) {
    is_first_timer = grpc_timer_heap_add(&shard->heap, timer);
  } else {
    timer->heap_index = INVALID_HEAP_INDEX;
    list_join(&shard->list, timer);
  }
  if (grpc_timer_trace.enabled()) {
    gpr_log(GPR_INFO,
            "  .. add to shard %d with queue_deadline_cap=%" PRId64
            " => is_first_timer=%s",
            static_cast<int>(shard - g_shards), shard->queue_deadline_cap,
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
      gpr_log(GPR_INFO, "  .. old shard min_deadline=%" PRId64,
              shard->min_deadline);
    }
    if (deadline < shard->min_deadline) {
      grpc_millis old_min_deadline = g_shard_queue[0]->min_deadline;
      shard->min_deadline = deadline;
      note_deadline_change(shard);
      if (shard->shard_queue_index == 0 && deadline < old_min_deadline) {
#if GPR_ARCH_64
        // TODO: sreek - Using c-style cast here. static_cast<> gives an error
        // (on mac platforms complaining that gpr_atm* is (long *) while
        // (&g_shared_mutables.min_timer) is a (long long *). The cast should be
        // safe since we know that both are pointer types and 64-bit wide.
        gpr_atm_no_barrier_store((gpr_atm*)(&g_shared_mutables.min_timer),
                                 deadline);
#else
        // On 32-bit systems, gpr_atm_no_barrier_store does not work on 64-bit
        // types (like grpc_millis). So all reads and writes to
        // g_shared_mutables.min_timer varialbe under g_shared_mutables.mu
        g_shared_mutables.min_timer = deadline;
#endif
        grpc_kick_poller();
      }
    }
    gpr_mu_unlock(&g_shared_mutables.mu);
  }
}

static void timer_consume_kick(void) {
#if GPR_ARCH_64
  /* Force re-evaluation of last seen min */
  gpr_tls_set(&g_last_seen_min_timer, 0);
#endif
}

static void timer_cancel(grpc_timer* timer) {
  if (!g_shared_mutables.initialized) {
    /* must have already been cancelled, also the shard mutex is invalid */
    return;
  }

  timer_shard* shard = &g_shards[GPR_HASH_POINTER(timer, g_num_shards)];
  gpr_mu_lock(&shard->mu);
  if (grpc_timer_trace.enabled()) {
    gpr_log(GPR_INFO, "TIMER %p: CANCEL pending=%s", timer,
            timer->pending ? "true" : "false");
  }

  if (timer->pending) {
    REMOVE_FROM_HASH_TABLE(timer);

    GRPC_CLOSURE_SCHED(timer->closure, GRPC_ERROR_CANCELLED);
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
static int refill_heap(timer_shard* shard, grpc_millis now) {
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
                     static_cast<grpc_millis>(deadline_delta * 1000.0));

  if (grpc_timer_check_trace.enabled()) {
    gpr_log(GPR_INFO, "  .. shard[%d]->queue_deadline_cap --> %" PRId64,
            static_cast<int>(shard - g_shards), shard->queue_deadline_cap);
  }
  for (timer = shard->list.next; timer != &shard->list; timer = next) {
    next = timer->next;

    if (timer->deadline < shard->queue_deadline_cap) {
      if (grpc_timer_check_trace.enabled()) {
        gpr_log(GPR_INFO, "  .. add timer with deadline %" PRId64 " to heap",
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
static grpc_timer* pop_one(timer_shard* shard, grpc_millis now) {
  grpc_timer* timer;
  for (;;) {
    if (grpc_timer_check_trace.enabled()) {
      gpr_log(GPR_INFO, "  .. shard[%d]: heap_empty=%s",
              static_cast<int>(shard - g_shards),
              grpc_timer_heap_is_empty(&shard->heap) ? "true" : "false");
    }
    if (grpc_timer_heap_is_empty(&shard->heap)) {
      if (now < shard->queue_deadline_cap) return nullptr;
      if (!refill_heap(shard, now)) return nullptr;
    }
    timer = grpc_timer_heap_top(&shard->heap);
    if (grpc_timer_check_trace.enabled()) {
      gpr_log(GPR_INFO,
              "  .. check top timer deadline=%" PRId64 " now=%" PRId64,
              timer->deadline, now);
    }
    if (timer->deadline > now) return nullptr;
    if (grpc_timer_trace.enabled()) {
      gpr_log(GPR_INFO, "TIMER %p: FIRE %" PRId64 "ms late via %s scheduler",
              timer, now - timer->deadline,
              timer->closure->scheduler->vtable->name);
    }
    timer->pending = false;
    grpc_timer_heap_pop(&shard->heap);
    return timer;
  }
}

/* REQUIRES: shard->mu unlocked */
static size_t pop_timers(timer_shard* shard, grpc_millis now,
                         grpc_millis* new_min_deadline, grpc_error* error) {
  size_t n = 0;
  grpc_timer* timer;
  gpr_mu_lock(&shard->mu);
  while ((timer = pop_one(shard, now))) {
    REMOVE_FROM_HASH_TABLE(timer);
    GRPC_CLOSURE_SCHED(timer->closure, GRPC_ERROR_REF(error));
    n++;
  }
  *new_min_deadline = compute_min_deadline(shard);
  gpr_mu_unlock(&shard->mu);
  if (grpc_timer_check_trace.enabled()) {
    gpr_log(GPR_INFO, "  .. shard[%d] popped %" PRIdPTR,
            static_cast<int>(shard - g_shards), n);
  }
  return n;
}

static grpc_timer_check_result run_some_expired_timers(grpc_millis now,
                                                       grpc_millis* next,
                                                       grpc_error* error) {
  grpc_timer_check_result result = GRPC_TIMERS_NOT_CHECKED;

#if GPR_ARCH_64
  // TODO: sreek - Using c-style cast here. static_cast<> gives an error (on
  // mac platforms complaining that gpr_atm* is (long *) while
  // (&g_shared_mutables.min_timer) is a (long long *). The cast should be
  // safe since we know that both are pointer types and 64-bit wide
  grpc_millis min_timer = static_cast<grpc_millis>(
      gpr_atm_no_barrier_load((gpr_atm*)(&g_shared_mutables.min_timer)));
  gpr_tls_set(&g_last_seen_min_timer, min_timer);
#else
  // On 32-bit systems, gpr_atm_no_barrier_load does not work on 64-bit types
  // (like grpc_millis). So all reads and writes to g_shared_mutables.min_timer
  // are done under g_shared_mutables.mu
  gpr_mu_lock(&g_shared_mutables.mu);
  grpc_millis min_timer = g_shared_mutables.min_timer;
  gpr_mu_unlock(&g_shared_mutables.mu);
#endif
  if (now < min_timer) {
    if (next != nullptr) *next = GPR_MIN(*next, min_timer);
    return GRPC_TIMERS_CHECKED_AND_EMPTY;
  }

  if (gpr_spinlock_trylock(&g_shared_mutables.checker_mu)) {
    gpr_mu_lock(&g_shared_mutables.mu);
    result = GRPC_TIMERS_CHECKED_AND_EMPTY;

    if (grpc_timer_check_trace.enabled()) {
      gpr_log(GPR_INFO, "  .. shard[%d]->min_deadline = %" PRId64,
              static_cast<int>(g_shard_queue[0] - g_shards),
              g_shard_queue[0]->min_deadline);
    }

    while (g_shard_queue[0]->min_deadline < now ||
           (now != GRPC_MILLIS_INF_FUTURE &&
            g_shard_queue[0]->min_deadline == now)) {
      grpc_millis new_min_deadline;

      /* For efficiency, we pop as many available timers as we can from the
         shard.  This may violate perfect timer deadline ordering, but that
         shouldn't be a big deal because we don't make ordering guarantees. */
      if (pop_timers(g_shard_queue[0], now, &new_min_deadline, error) > 0) {
        result = GRPC_TIMERS_FIRED;
      }

      if (grpc_timer_check_trace.enabled()) {
        gpr_log(GPR_INFO,
                "  .. result --> %d"
                ", shard[%d]->min_deadline %" PRId64 " --> %" PRId64
                ", now=%" PRId64,
                result, static_cast<int>(g_shard_queue[0] - g_shards),
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

#if GPR_ARCH_64
    // TODO: sreek - Using c-style cast here. static_cast<> gives an error (on
    // mac platforms complaining that gpr_atm* is (long *) while
    // (&g_shared_mutables.min_timer) is a (long long *). The cast should be
    // safe since we know that both are pointer types and 64-bit wide
    gpr_atm_no_barrier_store((gpr_atm*)(&g_shared_mutables.min_timer),
                             g_shard_queue[0]->min_deadline);
#else
    // On 32-bit systems, gpr_atm_no_barrier_store does not work on 64-bit
    // types (like grpc_millis). So all reads and writes to
    // g_shared_mutables.min_timer are done under g_shared_mutables.mu
    g_shared_mutables.min_timer = g_shard_queue[0]->min_deadline;
#endif
    gpr_mu_unlock(&g_shared_mutables.mu);
    gpr_spinlock_unlock(&g_shared_mutables.checker_mu);
  }

  GRPC_ERROR_UNREF(error);

  return result;
}

static grpc_timer_check_result timer_check(grpc_millis* next) {
  // prelude
  grpc_millis now = grpc_core::ExecCtx::Get()->Now();

#if GPR_ARCH_64
  /* fetch from a thread-local first: this avoids contention on a globally
     mutable cacheline in the common case */
  grpc_millis min_timer = gpr_tls_get(&g_last_seen_min_timer);
#else
  // On 32-bit systems, we currently do not have thread local support for 64-bit
  // types. In this case, directly read from g_shared_mutables.min_timer.
  // Also, note that on 32-bit systems, gpr_atm_no_barrier_store does not work
  // on 64-bit types (like grpc_millis). So all reads and writes to
  // g_shared_mutables.min_timer are done under g_shared_mutables.mu
  gpr_mu_lock(&g_shared_mutables.mu);
  grpc_millis min_timer = g_shared_mutables.min_timer;
  gpr_mu_unlock(&g_shared_mutables.mu);
#endif

  if (now < min_timer) {
    if (next != nullptr) {
      *next = GPR_MIN(*next, min_timer);
    }
    if (grpc_timer_check_trace.enabled()) {
      gpr_log(GPR_INFO, "TIMER CHECK SKIP: now=%" PRId64 " min_timer=%" PRId64,
              now, min_timer);
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
      gpr_asprintf(&next_str, "%" PRId64, *next);
    }
#if GPR_ARCH_64
    gpr_log(GPR_INFO,
            "TIMER CHECK BEGIN: now=%" PRId64 " next=%s tls_min=%" PRId64
            " glob_min=%" PRIdPTR,
            now, next_str, min_timer,
            gpr_atm_no_barrier_load((gpr_atm*)(&g_shared_mutables.min_timer)));
#else
    gpr_log(GPR_INFO, "TIMER CHECK BEGIN: now=%" PRId64 " next=%s min=%" PRId64,
            now, next_str, min_timer);
#endif
    gpr_free(next_str);
  }
  // actual code
  grpc_timer_check_result r =
      run_some_expired_timers(now, next, shutdown_error);
  // tracing
  if (grpc_timer_check_trace.enabled()) {
    char* next_str;
    if (next == nullptr) {
      next_str = gpr_strdup("NULL");
    } else {
      gpr_asprintf(&next_str, "%" PRId64, *next);
    }
    gpr_log(GPR_INFO, "TIMER CHECK END: r=%d; next=%s", r, next_str);
    gpr_free(next_str);
  }
  return r;
}

grpc_timer_vtable grpc_generic_timer_vtable = {
    timer_init,      timer_cancel,        timer_check,
    timer_list_init, timer_list_shutdown, timer_consume_kick};
