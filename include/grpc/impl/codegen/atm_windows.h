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

#ifndef GRPC_IMPL_CODEGEN_ATM_WINDOWS_H
#define GRPC_IMPL_CODEGEN_ATM_WINDOWS_H

/* Win32 variant of atm_platform.h */
#include <grpc/impl/codegen/port_platform.h>

typedef intptr_t gpr_atm;

#define gpr_atm_full_barrier MemoryBarrier

static __inline gpr_atm gpr_atm_acq_load(const gpr_atm *p) {
  gpr_atm result = *p;
  gpr_atm_full_barrier();
  return result;
}

static __inline gpr_atm gpr_atm_no_barrier_load(const gpr_atm *p) {
  /* TODO(dklempner): Can we implement something better here? */
  return gpr_atm_acq_load(p);
}

static __inline void gpr_atm_rel_store(gpr_atm *p, gpr_atm value) {
  gpr_atm_full_barrier();
  *p = value;
}

static __inline void gpr_atm_no_barrier_store(gpr_atm *p, gpr_atm value) {
  /* TODO(ctiller): Can we implement something better here? */
  gpr_atm_rel_store(p, value);
}

static __inline int gpr_atm_no_barrier_cas(gpr_atm *p, gpr_atm o, gpr_atm n) {
/* InterlockedCompareExchangePointerNoFence() not available on vista or
   windows7 */
#ifdef GPR_ARCH_64
  return o == (gpr_atm)InterlockedCompareExchangeAcquire64(
                  (volatile LONGLONG *)p, (LONGLONG)n, (LONGLONG)o);
#else
  return o == (gpr_atm)InterlockedCompareExchangeAcquire((volatile LONG *)p,
                                                         (LONG)n, (LONG)o);
#endif
}

static __inline int gpr_atm_acq_cas(gpr_atm *p, gpr_atm o, gpr_atm n) {
#ifdef GPR_ARCH_64
  return o == (gpr_atm)InterlockedCompareExchangeAcquire64(
                  (volatile LONGLONG *)p, (LONGLONG)n, (LONGLONG)o);
#else
  return o == (gpr_atm)InterlockedCompareExchangeAcquire((volatile LONG *)p,
                                                         (LONG)n, (LONG)o);
#endif
}

static __inline int gpr_atm_rel_cas(gpr_atm *p, gpr_atm o, gpr_atm n) {
#ifdef GPR_ARCH_64
  return o == (gpr_atm)InterlockedCompareExchangeRelease64(
                  (volatile LONGLONG *)p, (LONGLONG)n, (LONGLONG)o);
#else
  return o == (gpr_atm)InterlockedCompareExchangeRelease((volatile LONG *)p,
                                                         (LONG)n, (LONG)o);
#endif
}

static __inline gpr_atm gpr_atm_no_barrier_fetch_add(gpr_atm *p,
                                                     gpr_atm delta) {
  /* Use the CAS operation to get pointer-sized fetch and add */
  gpr_atm old;
  do {
    old = *p;
  } while (!gpr_atm_no_barrier_cas(p, old, old + delta));
  return old;
}

static __inline gpr_atm gpr_atm_full_fetch_add(gpr_atm *p, gpr_atm delta) {
  /* Use a CAS operation to get pointer-sized fetch and add */
  gpr_atm old;
#ifdef GPR_ARCH_64
  do {
    old = *p;
  } while (old != (gpr_atm)InterlockedCompareExchange64((volatile LONGLONG *)p,
                                                        (LONGLONG)old + delta,
                                                        (LONGLONG)old));
#else
  do {
    old = *p;
  } while (old != (gpr_atm)InterlockedCompareExchange(
                      (volatile LONG *)p, (LONG)old + delta, (LONG)old));
#endif
  return old;
}

#endif /* GRPC_IMPL_CODEGEN_ATM_WINDOWS_H */
