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

/* Generic implementation of synchronization primitives. */

#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include <assert.h>

/* Number of mutexes to allocate for events, to avoid lock contention.
   Should be a prime. */
enum { event_sync_partitions = 31 };

/* Events are partitioned by address to avoid lock contention. */
static struct sync_array_s {
  gpr_mu mu;
  gpr_cv cv;
} sync_array[event_sync_partitions];

/* This routine is executed once on first use, via event_once */
static gpr_once event_once = GPR_ONCE_INIT;
static void event_initialize(void) {
  int i;
  for (i = 0; i != event_sync_partitions; i++) {
    gpr_mu_init(&sync_array[i].mu);
    gpr_cv_init(&sync_array[i].cv);
  }
}

/* Hash ev into an element of sync_array[]. */
static struct sync_array_s *hash(gpr_event *ev) {
  return &sync_array[((uintptr_t)ev) % event_sync_partitions];
}

void gpr_event_init(gpr_event *ev) {
  gpr_once_init(&event_once, &event_initialize);
  ev->state = 0;
}

void gpr_event_set(gpr_event *ev, void *value) {
  struct sync_array_s *s = hash(ev);
  gpr_mu_lock(&s->mu);
  GPR_ASSERT(gpr_atm_acq_load(&ev->state) == 0);
  gpr_atm_rel_store(&ev->state, (gpr_atm)value);
  gpr_cv_broadcast(&s->cv);
  gpr_mu_unlock(&s->mu);
  GPR_ASSERT(value != NULL);
}

void *gpr_event_get(gpr_event *ev) {
  return (void *)gpr_atm_acq_load(&ev->state);
}

void *gpr_event_wait(gpr_event *ev, gpr_timespec abs_deadline) {
  void *result = (void *)gpr_atm_acq_load(&ev->state);
  if (result == NULL) {
    struct sync_array_s *s = hash(ev);
    gpr_mu_lock(&s->mu);
    do {
      result = (void *)gpr_atm_acq_load(&ev->state);
    } while (result == NULL && !gpr_cv_wait(&s->cv, &s->mu, abs_deadline));
    gpr_mu_unlock(&s->mu);
  }
  return result;
}

void gpr_ref_init(gpr_refcount *r, int n) { gpr_atm_rel_store(&r->count, n); }

void gpr_ref(gpr_refcount *r) { gpr_atm_no_barrier_fetch_add(&r->count, 1); }

void gpr_ref_non_zero(gpr_refcount *r) {
#ifndef NDEBUG
  gpr_atm prior = gpr_atm_no_barrier_fetch_add(&r->count, 1);
  assert(prior > 0);
#else
  gpr_ref(r);
#endif
}

void gpr_refn(gpr_refcount *r, int n) {
  gpr_atm_no_barrier_fetch_add(&r->count, n);
}

int gpr_unref(gpr_refcount *r) {
  gpr_atm prior = gpr_atm_full_fetch_add(&r->count, -1);
  GPR_ASSERT(prior > 0);
  return prior == 1;
}

void gpr_stats_init(gpr_stats_counter *c, intptr_t n) {
  gpr_atm_rel_store(&c->value, n);
}

void gpr_stats_inc(gpr_stats_counter *c, intptr_t inc) {
  gpr_atm_no_barrier_fetch_add(&c->value, inc);
}

intptr_t gpr_stats_read(const gpr_stats_counter *c) {
  /* don't need acquire-load, but we have no no-barrier load yet */
  return gpr_atm_acq_load(&c->value);
}
