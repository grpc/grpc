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

/* Generic implementation of synchronization primitives. */

#include <grpc/support/port_platform.h>

#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include <assert.h>

#include "src/core/lib/gprpp/sync.h"

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
static struct sync_array_s* hash(gpr_event* ev) {
  return &sync_array[((uintptr_t)ev) % event_sync_partitions];
}

void gpr_event_init(gpr_event* ev) {
  gpr_once_init(&event_once, &event_initialize);
  ev->state = 0;
}

void gpr_event_set(gpr_event* ev, void* value) {
  struct sync_array_s* s = hash(ev);
  gpr_mu_lock(&s->mu);
  GPR_ASSERT(gpr_atm_acq_load(&ev->state) == 0);
  gpr_atm_rel_store(&ev->state, (gpr_atm)value);
  gpr_cv_broadcast(&s->cv);
  gpr_mu_unlock(&s->mu);
  GPR_ASSERT(value != nullptr);
}

void* gpr_event_get(gpr_event* ev) {
  return (void*)gpr_atm_acq_load(&ev->state);
}

void* gpr_event_wait(gpr_event* ev, gpr_timespec abs_deadline) {
  void* result = (void*)gpr_atm_acq_load(&ev->state);
  if (result == nullptr) {
    struct sync_array_s* s = hash(ev);
    gpr_mu_lock(&s->mu);
    do {
      result = (void*)gpr_atm_acq_load(&ev->state);
    } while (result == nullptr && !gpr_cv_wait(&s->cv, &s->mu, abs_deadline));
    gpr_mu_unlock(&s->mu);
  }
  return result;
}

void gpr_ref_init(gpr_refcount* r, int n) { grpc_core::RefInit(r, n); }

void gpr_ref(gpr_refcount* r) { grpc_core::Ref(r); }

void gpr_ref_non_zero(gpr_refcount* r) { grpc_core::RefNonZero(r); }

void gpr_refn(gpr_refcount* r, int n) { grpc_core::RefN(r, n); }

int gpr_unref(gpr_refcount* r) { return grpc_core::Unref(r); }

int gpr_ref_is_unique(gpr_refcount* r) { return grpc_core::RefIsUnique(r); }

void gpr_stats_init(gpr_stats_counter* c, intptr_t n) {
  grpc_core::StatsInit(c, n);
}

void gpr_stats_inc(gpr_stats_counter* c, intptr_t inc) {
  grpc_core::StatsInc(c, inc);
}

intptr_t gpr_stats_read(const gpr_stats_counter* c) {
  return grpc_core::StatsRead(c);
}
