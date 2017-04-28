/*
 *
 * Copyright 2016, Google Inc.
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

#ifndef GRPC_IMPL_CODEGEN_SYNC_H
#define GRPC_IMPL_CODEGEN_SYNC_H
/* Synchronization primitives for GPR.

   The type  gpr_mu              provides a non-reentrant mutex (lock).

   The type  gpr_cv              provides a condition variable.

   The type  gpr_once            provides for one-time initialization.

   The type gpr_event            provides one-time-setting, reading, and
                                 waiting of a void*, with memory barriers.

   The type gpr_refcount         provides an object reference counter,
                                 with memory barriers suitable to control
                                 object lifetimes.

   The type gpr_stats_counter    provides an atomic statistics counter. It
                                 provides no memory barriers.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Platform-specific type declarations of gpr_mu and gpr_cv.   */
#include <grpc/impl/codegen/port_platform.h>
#include <grpc/impl/codegen/sync_generic.h>

#if defined(GPR_POSIX_SYNC)
#include <grpc/impl/codegen/sync_posix.h>
#elif defined(GPR_WINDOWS)
#include <grpc/impl/codegen/sync_windows.h>
#elif !defined(GPR_CUSTOM_SYNC)
#error Unable to determine platform for sync
#endif

#ifdef __cplusplus
}
#endif

#endif /* GRPC_IMPL_CODEGEN_SYNC_H */
