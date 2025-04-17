//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

// Generic implementation of synchronization primitives.

#include <assert.h>
#include <grpc/support/atm.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>

#include "absl/log/check.h"

// Number of mutexes to allocate for events, to avoid lock contention.
// Should be a prime.
enum { event_sync_partitions = 31 };

// Events are partitioned by address to avoid lock contention.
static struct sync_array_s {
  gpr_mu mu;
  gpr_cv cv;
} sync_array[event_sync_partitions];

// This routine is executed once on first use, via event_once
static gpr_once event_once = GPR_ONCE_INIT;
static void event_initialize(void) {
  int i;
  for (i = 0; i != event_sync_partitions; i++) {
    gpr_mu_init(&sync_array[i].mu);
    gpr_cv_init(&sync_array[i].cv);
  }
}

// Hash ev into an element of sync_array[].
static struct sync_array_s* hash(gpr_event* ev) {
  return &sync_array[reinterpret_cast<uintptr_t>(ev) % event_sync_partitions];
}

void gpr_event_init(gpr_event* ev) {
  gpr_once_init(&event_once, &event_initialize);
  ev->state = 0;
}

void gpr_event_set(gpr_event* ev, void* value) {
  struct sync_array_s* s = hash(ev);
  gpr_mu_lock(&s->mu);
  CHECK_EQ(gpr_atm_acq_load(&ev->state), 0);
  gpr_atm_rel_store(&ev->state, (gpr_atm)value);
  gpr_cv_broadcast(&s->cv);
  gpr_mu_unlock(&s->mu);
  CHECK_NE(value, nullptr);
}

void* gpr_event_get(gpr_event* ev) {
  return reinterpret_cast<void*>(gpr_atm_acq_load(&ev->state));
}

void* gpr_event_wait(gpr_event* ev, gpr_timespec abs_deadline) {
  void* result = reinterpret_cast<void*>(gpr_atm_acq_load(&ev->state));
  if (result == nullptr) {
    struct sync_array_s* s = hash(ev);
    gpr_mu_lock(&s->mu);
    do {
      result = reinterpret_cast<void*>(gpr_atm_acq_load(&ev->state));
    } while (result == nullptr && !gpr_cv_wait(&s->cv, &s->mu, abs_deadline));
    gpr_mu_unlock(&s->mu);
  }
  return result;
}

void gpr_ref_init(gpr_refcount* r, int n) { gpr_atm_rel_store(&r->count, n); }

void gpr_ref(gpr_refcount* r) { gpr_atm_no_barrier_fetch_add(&r->count, 1); }

void gpr_ref_non_zero(gpr_refcount* r) {
#ifndef NDEBUG
  gpr_atm prior = gpr_atm_no_barrier_fetch_add(&r->count, 1);
  assert(prior > 0);
#else
  gpr_ref(r);
#endif
}

void gpr_refn(gpr_refcount* r, int n) {
  gpr_atm_no_barrier_fetch_add(&r->count, n);
}

int gpr_unref(gpr_refcount* r) {
  gpr_atm prior = gpr_atm_full_fetch_add(&r->count, -1);
  CHECK_GT(prior, 0);
  return prior == 1;
}

int gpr_ref_is_unique(gpr_refcount* r) {
  return gpr_atm_acq_load(&r->count) == 1;
}

void gpr_stats_init(gpr_stats_counter* c, intptr_t n) {
  gpr_atm_rel_store(&c->value, n);
}

void gpr_stats_inc(gpr_stats_counter* c, intptr_t inc) {
  gpr_atm_no_barrier_fetch_add(&c->value, inc);
}

intptr_t gpr_stats_read(const gpr_stats_counter* c) {
  // don't need acquire-load, but we have no no-barrier load yet
  return gpr_atm_acq_load(&c->value);
}
