/*
 *
 * Copyright 2018 gRPC authors.
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

/* Inline implementation of synchronization primitives, preferred over the C
   public API for C++ code. */

#ifndef GRPC_CORE_LIB_GPRPP_SYNC_H
#define GRPC_CORE_LIB_GPRPP_SYNC_H

#include <grpc/support/port_platform.h>

#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include <assert.h>

namespace grpc_core {

inline void RefInit(gpr_refcount* r, int n) {
  gpr_atm_no_barrier_store(&r->count, n);
}

inline void Ref(gpr_refcount* r) { gpr_atm_no_barrier_fetch_add(&r->count, 1); }

inline void RefNonZero(gpr_refcount* r) {
#ifndef NDEBUG
  gpr_atm prior = gpr_atm_no_barrier_fetch_add(&r->count, 1);
  assert(prior > 0);
#else
  Ref(r);
#endif
}

inline void RefN(gpr_refcount* r, int n) {
  gpr_atm_no_barrier_fetch_add(&r->count, n);
}

inline int Unref(gpr_refcount* r) {
  gpr_atm prior = gpr_atm_full_fetch_add(&r->count, -1);
  GPR_DEBUG_ASSERT(prior > 0);
  return prior == 1;
}

inline int RefIsUnique(gpr_refcount* r) {
  return gpr_atm_acq_load(&r->count) == 1;
}

inline void StatsInit(gpr_stats_counter* c, intptr_t n) {
  gpr_atm_rel_store(&c->value, n);
}

inline void StatsInc(gpr_stats_counter* c, intptr_t inc) {
  gpr_atm_no_barrier_fetch_add(&c->value, inc);
}

inline intptr_t StatsRead(const gpr_stats_counter* c) {
  /* don't need acquire-load, but we have no no-barrier load yet */
  return gpr_atm_acq_load(&c->value);
}

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_GPRPP_SYNC_H */
