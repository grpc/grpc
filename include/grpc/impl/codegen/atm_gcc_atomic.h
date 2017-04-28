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

#ifndef GRPC_IMPL_CODEGEN_ATM_GCC_ATOMIC_H
#define GRPC_IMPL_CODEGEN_ATM_GCC_ATOMIC_H

/* atm_platform.h for gcc and gcc-like compilers with the
   __atomic_* interface.  */
#include <grpc/impl/codegen/port_platform.h>

typedef intptr_t gpr_atm;
#define GPR_ATM_MAX INTPTR_MAX

#ifdef GPR_LOW_LEVEL_COUNTERS
extern gpr_atm gpr_counter_atm_cas;
extern gpr_atm gpr_counter_atm_add;
#define GPR_ATM_INC_COUNTER(counter) \
  __atomic_fetch_add(&counter, 1, __ATOMIC_RELAXED)
#define GPR_ATM_INC_CAS_THEN(blah) \
  (GPR_ATM_INC_COUNTER(gpr_counter_atm_cas), blah)
#define GPR_ATM_INC_ADD_THEN(blah) \
  (GPR_ATM_INC_COUNTER(gpr_counter_atm_add), blah)
#else
#define GPR_ATM_INC_CAS_THEN(blah) blah
#define GPR_ATM_INC_ADD_THEN(blah) blah
#endif

#define gpr_atm_full_barrier() (__atomic_thread_fence(__ATOMIC_SEQ_CST))

#define gpr_atm_acq_load(p) (__atomic_load_n((p), __ATOMIC_ACQUIRE))
#define gpr_atm_no_barrier_load(p) (__atomic_load_n((p), __ATOMIC_RELAXED))
#define gpr_atm_rel_store(p, value) \
  (__atomic_store_n((p), (intptr_t)(value), __ATOMIC_RELEASE))
#define gpr_atm_no_barrier_store(p, value) \
  (__atomic_store_n((p), (intptr_t)(value), __ATOMIC_RELAXED))

#define gpr_atm_no_barrier_fetch_add(p, delta) \
  GPR_ATM_INC_ADD_THEN(                        \
      __atomic_fetch_add((p), (intptr_t)(delta), __ATOMIC_RELAXED))
#define gpr_atm_full_fetch_add(p, delta) \
  GPR_ATM_INC_ADD_THEN(                  \
      __atomic_fetch_add((p), (intptr_t)(delta), __ATOMIC_ACQ_REL))

static __inline int gpr_atm_no_barrier_cas(gpr_atm *p, gpr_atm o, gpr_atm n) {
  return GPR_ATM_INC_CAS_THEN(__atomic_compare_exchange_n(
      p, &o, n, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
}

static __inline int gpr_atm_acq_cas(gpr_atm *p, gpr_atm o, gpr_atm n) {
  return GPR_ATM_INC_CAS_THEN(__atomic_compare_exchange_n(
      p, &o, n, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED));
}

static __inline int gpr_atm_rel_cas(gpr_atm *p, gpr_atm o, gpr_atm n) {
  return GPR_ATM_INC_CAS_THEN(__atomic_compare_exchange_n(
      p, &o, n, 0, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
}

static __inline int gpr_atm_full_cas(gpr_atm *p, gpr_atm o, gpr_atm n) {
  return GPR_ATM_INC_CAS_THEN(__atomic_compare_exchange_n(
      p, &o, n, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
}

#define gpr_atm_full_xchg(p, n) \
  GPR_ATM_INC_CAS_THEN(__atomic_exchange_n((p), (n), __ATOMIC_ACQ_REL))

#endif /* GRPC_IMPL_CODEGEN_ATM_GCC_ATOMIC_H */
