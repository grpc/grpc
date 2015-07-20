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

#include "src/core/iomgr/alarm.h"

#include "src/core/iomgr/alarm_heap.h"
#include "src/core/iomgr/alarm_internal.h"
#include "src/core/iomgr/time_averaged_stats.h"
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/useful.h>

#define INVALID_HEAP_INDEX 0xffffffffu

#define LOG2_NUM_SHARDS 5
#define NUM_SHARDS (1 << LOG2_NUM_SHARDS)
#define MAX_ALARMS_PER_CHECK 128
#define ADD_DEADLINE_SCALE 0.33
#define MIN_QUEUE_WINDOW_DURATION 0.01
#define MAX_QUEUE_WINDOW_DURATION 1

typedef struct {
  gpr_mu mu;
  grpc_time_averaged_stats stats;
  /* All and only alarms with deadlines <= this will be in the heap. */
  gpr_timespec queue_deadline_cap;
  gpr_timespec min_deadline;
  /* Index in the g_shard_queue */
  gpr_uint32 shard_queue_index;
  /* This holds all alarms with deadlines < queue_deadline_cap.  Alarms in this
     list have the top bit of their deadline set to 0. */
  grpc_alarm_heap heap;
  /* This holds alarms whose deadline is >= queue_deadline_cap. */
  grpc_alarm list;
} shard_type;

/* Protects g_shard_queue */
static gpr_mu g_mu;
/* Allow only one run_some_expired_alarms at once */
static gpr_mu g_checker_mu;
static gpr_clock_type g_clock_type;
static shard_type g_shards[NUM_SHARDS];
/* Protected by g_mu */
static shard_type *g_shard_queue[NUM_SHARDS];

static int run_some_expired_alarms(gpr_mu *drop_mu, gpr_timespec now,
                                   gpr_timespec *next, int success);

static gpr_timespec compute_min_deadline(shard_type *shard) {
  return grpc_alarm_heap_is_empty(&shard->heap)
             ? shard->queue_deadline_cap
             : grpc_alarm_heap_top(&shard->heap)->deadline;
}

void grpc_alarm_list_init(gpr_timespec now) {
  int i;

  gpr_mu_init(&g_mu);
  gpr_mu_init(&g_checker_mu);
  g_clock_type = now.clock_type;

  for (i = 0; i < NUM_SHARDS; i++) {
    shard_type *shard = &g_shards[i];
    gpr_mu_init(&shard->mu);
    grpc_time_averaged_stats_init(&shard->stats, 1.0 / ADD_DEADLINE_SCALE, 0.1,
                                  0.5);
    shard->queue_deadline_cap = now;
    shard->shard_queue_index = i;
    grpc_alarm_heap_init(&shard->heap);
    shard->list.next = shard->list.prev = &shard->list;
    shard->min_deadline = compute_min_deadline(shard);
    g_shard_queue[i] = shard;
  }
}

void grpc_alarm_list_shutdown(void) {
  int i;
  while (run_some_expired_alarms(NULL, gpr_inf_future(g_clock_type), NULL,
                                 0))
    ;
  for (i = 0; i < NUM_SHARDS; i++) {
    shard_type *shard = &g_shards[i];
    gpr_mu_destroy(&shard->mu);
    grpc_alarm_heap_destroy(&shard->heap);
  }
  gpr_mu_destroy(&g_mu);
  gpr_mu_destroy(&g_checker_mu);
}

/* This is a cheap, but good enough, pointer hash for sharding the tasks: */
static size_t shard_idx(const grpc_alarm *info) {
  size_t x = (size_t)info;
  return ((x >> 4) ^ (x >> 9) ^ (x >> 14)) & (NUM_SHARDS - 1);
}

static double ts_to_dbl(gpr_timespec ts) {
  return ts.tv_sec + 1e-9 * ts.tv_nsec;
}

static gpr_timespec dbl_to_ts(double d) {
  gpr_timespec ts;
  ts.tv_sec = d;
  ts.tv_nsec = 1e9 * (d - ts.tv_sec);
  ts.clock_type = GPR_TIMESPAN;
  return ts;
}

static void list_join(grpc_alarm *head, grpc_alarm *alarm) {
  alarm->next = head;
  alarm->prev = head->prev;
  alarm->next->prev = alarm->prev->next = alarm;
}

static void list_remove(grpc_alarm *alarm) {
  alarm->next->prev = alarm->prev;
  alarm->prev->next = alarm->next;
}

static void swap_adjacent_shards_in_queue(size_t first_shard_queue_index) {
  shard_type *temp;
  temp = g_shard_queue[first_shard_queue_index];
  g_shard_queue[first_shard_queue_index] =
      g_shard_queue[first_shard_queue_index + 1];
  g_shard_queue[first_shard_queue_index + 1] = temp;
  g_shard_queue[first_shard_queue_index]->shard_queue_index =
      first_shard_queue_index;
  g_shard_queue[first_shard_queue_index + 1]->shard_queue_index =
      first_shard_queue_index + 1;
}

static void note_deadline_change(shard_type *shard) {
  while (shard->shard_queue_index > 0 &&
         gpr_time_cmp(
             shard->min_deadline,
             g_shard_queue[shard->shard_queue_index - 1]->min_deadline) < 0) {
    swap_adjacent_shards_in_queue(shard->shard_queue_index - 1);
  }
  while (shard->shard_queue_index < NUM_SHARDS - 1 &&
         gpr_time_cmp(
             shard->min_deadline,
             g_shard_queue[shard->shard_queue_index + 1]->min_deadline) > 0) {
    swap_adjacent_shards_in_queue(shard->shard_queue_index);
  }
}

void grpc_alarm_init(grpc_alarm *alarm, gpr_timespec deadline,
                     grpc_iomgr_cb_func alarm_cb, void *alarm_cb_arg,
                     gpr_timespec now) {
  int is_first_alarm = 0;
  shard_type *shard = &g_shards[shard_idx(alarm)];
  GPR_ASSERT(deadline.clock_type == g_clock_type);
  GPR_ASSERT(now.clock_type == g_clock_type);
  alarm->cb = alarm_cb;
  alarm->cb_arg = alarm_cb_arg;
  alarm->deadline = deadline;
  alarm->triggered = 0;

  /* TODO(ctiller): check deadline expired */

  gpr_mu_lock(&shard->mu);
  grpc_time_averaged_stats_add_sample(&shard->stats,
                                      ts_to_dbl(gpr_time_sub(deadline, now)));
  if (gpr_time_cmp(deadline, shard->queue_deadline_cap) < 0) {
    is_first_alarm = grpc_alarm_heap_add(&shard->heap, alarm);
  } else {
    alarm->heap_index = INVALID_HEAP_INDEX;
    list_join(&shard->list, alarm);
  }
  gpr_mu_unlock(&shard->mu);

  /* Deadline may have decreased, we need to adjust the master queue.  Note
     that there is a potential racy unlocked region here.  There could be a
     reordering of multiple grpc_alarm_init calls, at this point, but the < test
     below should ensure that we err on the side of caution.  There could
     also be a race with grpc_alarm_check, which might beat us to the lock.  In
     that case, it is possible that the alarm that we added will have already
     run by the time we hold the lock, but that too is a safe error.
     Finally, it's possible that the grpc_alarm_check that intervened failed to
     trigger the new alarm because the min_deadline hadn't yet been reduced.
     In that case, the alarm will simply have to wait for the next
     grpc_alarm_check. */
  if (is_first_alarm) {
    gpr_mu_lock(&g_mu);
    if (gpr_time_cmp(deadline, shard->min_deadline) < 0) {
      gpr_timespec old_min_deadline = g_shard_queue[0]->min_deadline;
      shard->min_deadline = deadline;
      note_deadline_change(shard);
      if (shard->shard_queue_index == 0 &&
          gpr_time_cmp(deadline, old_min_deadline) < 0) {
        grpc_kick_poller();
      }
    }
    gpr_mu_unlock(&g_mu);
  }
}

void grpc_alarm_cancel(grpc_alarm *alarm) {
  shard_type *shard = &g_shards[shard_idx(alarm)];
  int triggered = 0;
  gpr_mu_lock(&shard->mu);
  if (!alarm->triggered) {
    triggered = 1;
    alarm->triggered = 1;
    if (alarm->heap_index == INVALID_HEAP_INDEX) {
      list_remove(alarm);
    } else {
      grpc_alarm_heap_remove(&shard->heap, alarm);
    }
  }
  gpr_mu_unlock(&shard->mu);

  if (triggered) {
    alarm->cb(alarm->cb_arg, 0);
  }
}

/* This is called when the queue is empty and "now" has reached the
   queue_deadline_cap.  We compute a new queue deadline and then scan the map
   for alarms that fall at or under it.  Returns true if the queue is no
   longer empty.
   REQUIRES: shard->mu locked */
static int refill_queue(shard_type *shard, gpr_timespec now) {
  /* Compute the new queue window width and bound by the limits: */
  double computed_deadline_delta =
      grpc_time_averaged_stats_update_average(&shard->stats) *
      ADD_DEADLINE_SCALE;
  double deadline_delta =
      GPR_CLAMP(computed_deadline_delta, MIN_QUEUE_WINDOW_DURATION,
                MAX_QUEUE_WINDOW_DURATION);
  grpc_alarm *alarm, *next;

  /* Compute the new cap and put all alarms under it into the queue: */
  shard->queue_deadline_cap = gpr_time_add(
      gpr_time_max(now, shard->queue_deadline_cap), dbl_to_ts(deadline_delta));
  for (alarm = shard->list.next; alarm != &shard->list; alarm = next) {
    next = alarm->next;

    if (gpr_time_cmp(alarm->deadline, shard->queue_deadline_cap) < 0) {
      list_remove(alarm);
      grpc_alarm_heap_add(&shard->heap, alarm);
    }
  }
  return !grpc_alarm_heap_is_empty(&shard->heap);
}

/* This pops the next non-cancelled alarm with deadline <= now from the queue,
   or returns NULL if there isn't one.
   REQUIRES: shard->mu locked */
static grpc_alarm *pop_one(shard_type *shard, gpr_timespec now) {
  grpc_alarm *alarm;
  for (;;) {
    if (grpc_alarm_heap_is_empty(&shard->heap)) {
      if (gpr_time_cmp(now, shard->queue_deadline_cap) < 0) return NULL;
      if (!refill_queue(shard, now)) return NULL;
    }
    alarm = grpc_alarm_heap_top(&shard->heap);
    if (gpr_time_cmp(alarm->deadline, now) > 0) return NULL;
    alarm->triggered = 1;
    grpc_alarm_heap_pop(&shard->heap);
    return alarm;
  }
}

/* REQUIRES: shard->mu unlocked */
static size_t pop_alarms(shard_type *shard, gpr_timespec now,
                         grpc_alarm **alarms, size_t max_alarms,
                         gpr_timespec *new_min_deadline) {
  size_t n = 0;
  grpc_alarm *alarm;
  gpr_mu_lock(&shard->mu);
  while (n < max_alarms && (alarm = pop_one(shard, now))) {
    alarms[n++] = alarm;
  }
  *new_min_deadline = compute_min_deadline(shard);
  gpr_mu_unlock(&shard->mu);
  return n;
}

static int run_some_expired_alarms(gpr_mu *drop_mu, gpr_timespec now,
                                   gpr_timespec *next, int success) {
  size_t n = 0;
  size_t i;
  grpc_alarm *alarms[MAX_ALARMS_PER_CHECK];

  /* TODO(ctiller): verify that there are any alarms (atomically) here */

  if (gpr_mu_trylock(&g_checker_mu)) {
    gpr_mu_lock(&g_mu);

    while (n < MAX_ALARMS_PER_CHECK &&
           gpr_time_cmp(g_shard_queue[0]->min_deadline, now) < 0) {
      gpr_timespec new_min_deadline;

      /* For efficiency, we pop as many available alarms as we can from the
         shard.  This may violate perfect alarm deadline ordering, but that
         shouldn't be a big deal because we don't make ordering guarantees. */
      n += pop_alarms(g_shard_queue[0], now, alarms + n,
                      MAX_ALARMS_PER_CHECK - n, &new_min_deadline);

      /* An grpc_alarm_init() on the shard could intervene here, adding a new
         alarm that is earlier than new_min_deadline.  However,
         grpc_alarm_init() will block on the master_lock before it can call
         set_min_deadline, so this one will complete first and then the AddAlarm
         will reduce the min_deadline (perhaps unnecessarily). */
      g_shard_queue[0]->min_deadline = new_min_deadline;
      note_deadline_change(g_shard_queue[0]);
    }

    if (next) {
      *next = gpr_time_min(*next, g_shard_queue[0]->min_deadline);
    }

    gpr_mu_unlock(&g_mu);
    gpr_mu_unlock(&g_checker_mu);
  }

  if (n && drop_mu) {
    gpr_mu_unlock(drop_mu);
  }

  for (i = 0; i < n; i++) {
    alarms[i]->cb(alarms[i]->cb_arg, success);
  }

  if (n && drop_mu) {
    gpr_mu_lock(drop_mu);
  }

  return n;
}

int grpc_alarm_check(gpr_mu *drop_mu, gpr_timespec now, gpr_timespec *next) {
  GPR_ASSERT(now.clock_type == g_clock_type);
  return run_some_expired_alarms(drop_mu, now, next, 1);
}

gpr_timespec grpc_alarm_list_next_timeout(void) {
  gpr_timespec out;
  gpr_mu_lock(&g_mu);
  out = g_shard_queue[0]->min_deadline;
  gpr_mu_unlock(&g_mu);
  return out;
}
