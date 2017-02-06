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

#ifndef GRPC_IMPL_CODEGEN_ATM_GCC_SYNC_H
#define GRPC_IMPL_CODEGEN_ATM_GCC_SYNC_H

/* variant of atm_platform.h for gcc and gcc-like compiers with __sync_*
   interface */
#include <grpc/impl/codegen/port_platform.h>

typedef intptr_t gpr_atm;

#define GPR_ATM_COMPILE_BARRIER_() __asm__ __volatile__("" : : : "memory")

#if defined(__i386) || defined(__x86_64__)
/* All loads are acquire loads and all stores are release stores.  */
#define GPR_ATM_LS_BARRIER_() GPR_ATM_COMPILE_BARRIER_()
#else
#define GPR_ATM_LS_BARRIER_() gpr_atm_full_barrier()
#endif

#define gpr_atm_full_barrier() (__sync_synchronize())

static __inline gpr_atm gpr_atm_acq_load(const gpr_atm *p) {
  gpr_atm value = *p;
  GPR_ATM_LS_BARRIER_();
  return value;
}

static __inline gpr_atm gpr_atm_no_barrier_load(const gpr_atm *p) {
  gpr_atm value = *p;
  GPR_ATM_COMPILE_BARRIER_();
  return value;
}

static __inline void gpr_atm_rel_store(gpr_atm *p, gpr_atm value) {
  GPR_ATM_LS_BARRIER_();
  *p = value;
}

static __inline void gpr_atm_no_barrier_store(gpr_atm *p, gpr_atm value) {
  GPR_ATM_COMPILE_BARRIER_();
  *p = value;
}

#undef GPR_ATM_LS_BARRIER_
#undef GPR_ATM_COMPILE_BARRIER_

#define gpr_atm_no_barrier_fetch_add(p, delta) \
  gpr_atm_full_fetch_add((p), (delta))
#define gpr_atm_full_fetch_add(p, delta) (__sync_fetch_and_add((p), (delta)))

#define gpr_atm_no_barrier_cas(p, o, n) gpr_atm_acq_cas((p), (o), (n))
#define gpr_atm_acq_cas(p, o, n) (__sync_bool_compare_and_swap((p), (o), (n)))
#define gpr_atm_rel_cas(p, o, n) gpr_atm_acq_cas((p), (o), (n))

static __inline gpr_atm gpr_atm_full_xchg(gpr_atm *p, gpr_atm n) {
  gpr_atm cur;
  do {
    cur = gpr_atm_acq_load(p);
  } while (!gpr_atm_rel_cas(p, cur, n));
  return cur;
}

#endif /* GRPC_IMPL_CODEGEN_ATM_GCC_SYNC_H */
