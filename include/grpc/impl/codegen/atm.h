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

#ifndef GRPC_IMPL_CODEGEN_ATM_H
#define GRPC_IMPL_CODEGEN_ATM_H

/* This interface provides atomic operations and barriers.
   It is internal to gpr support code and should not be used outside it.

   If an operation with acquire semantics precedes another memory access by the
   same thread, the operation will precede that other access as seen by other
   threads.

   If an operation with release semantics follows another memory access by the
   same thread, the operation will follow that other access as seen by other
   threads.

   Routines with "acq" or "full" in the name have acquire semantics.  Routines
   with "rel" or "full" in the name have release semantics.  Routines with
   "no_barrier" in the name have neither acquire not release semantics.

   The routines may be implemented as macros.

   // Atomic operations act on an intergral_type gpr_atm that is guaranteed to
   // be the same size as a pointer.
   typedef intptr_t gpr_atm;

   // A memory barrier, providing both acquire and release semantics, but not
   // otherwise acting on memory.
   void gpr_atm_full_barrier(void);

   // Atomically return *p, with acquire semantics.
   gpr_atm gpr_atm_acq_load(gpr_atm *p);

   // Atomically set *p = value, with release semantics.
   void gpr_atm_rel_store(gpr_atm *p, gpr_atm value);

   // Atomically add delta to *p, and return the old value of *p, with
   // the barriers specified.
   gpr_atm gpr_atm_no_barrier_fetch_add(gpr_atm *p, gpr_atm delta);
   gpr_atm gpr_atm_full_fetch_add(gpr_atm *p, gpr_atm delta);

   // Atomically, if *p==o, set *p=n and return non-zero otherwise return 0,
   // with the barriers specified if the operation succeeds.
   int gpr_atm_no_barrier_cas(gpr_atm *p, gpr_atm o, gpr_atm n);
   int gpr_atm_acq_cas(gpr_atm *p, gpr_atm o, gpr_atm n);
   int gpr_atm_rel_cas(gpr_atm *p, gpr_atm o, gpr_atm n);
*/

#include <grpc/impl/codegen/port_platform.h>

#if defined(GPR_GCC_ATOMIC)
#include <grpc/impl/codegen/atm_gcc_atomic.h>
#elif defined(GPR_GCC_SYNC)
#include <grpc/impl/codegen/atm_gcc_sync.h>
#elif defined(GPR_WINDOWS_ATOMIC)
#include <grpc/impl/codegen/atm_windows.h>
#else
#error could not determine platform for atm
#endif

#endif /* GRPC_IMPL_CODEGEN_ATM_H */
