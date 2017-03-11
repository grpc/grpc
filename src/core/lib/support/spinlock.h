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

#ifndef GRPC_CORE_LIB_SUPPORT_SPINLOCK_H
#define GRPC_CORE_LIB_SUPPORT_SPINLOCK_H

#include <grpc/support/atm.h>

/* Simple spinlock. No backoff strategy, gpr_spinlock_lock is almost always
   a concurrency code smell. */
typedef struct { gpr_atm atm; } gpr_spinlock;

#define GPR_SPINLOCK_INITIALIZER ((gpr_spinlock){0})
#define GPR_SPINLOCK_STATIC_INITIALIZER \
  { 0 }
#define gpr_spinlock_trylock(lock) (gpr_atm_acq_cas(&(lock)->atm, 0, 1))
#define gpr_spinlock_unlock(lock) (gpr_atm_rel_store(&(lock)->atm, 0))
#define gpr_spinlock_lock(lock) \
  do {                          \
  } while (!gpr_spinlock_trylock((lock)))

#endif /* GRPC_CORE_LIB_SUPPORT_SPINLOCK_H */
