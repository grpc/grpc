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

#ifndef GRPC_IMPL_CODEGEN_GPR_TYPES_H
#define GRPC_IMPL_CODEGEN_GPR_TYPES_H

#include <grpc/impl/codegen/port_platform.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The clocks we support. */
typedef enum {
  /* Monotonic clock. Epoch undefined. Always moves forwards. */
  GPR_CLOCK_MONOTONIC = 0,
  /* Realtime clock. May jump forwards or backwards. Settable by
     the system administrator. Has its epoch at 0:00:00 UTC 1 Jan 1970. */
  GPR_CLOCK_REALTIME,
  /* CPU cycle time obtained by rdtsc instruction on x86 platforms. Epoch
     undefined. Degrades to GPR_CLOCK_REALTIME on other platforms. */
  GPR_CLOCK_PRECISE,
  /* Unmeasurable clock type: no base, created by taking the difference
     between two times */
  GPR_TIMESPAN
} gpr_clock_type;

/* Analogous to struct timespec. On some machines, absolute times may be in
 * local time. */
typedef struct gpr_timespec {
  int64_t tv_sec;
  int32_t tv_nsec;
  /** Against which clock was this time measured? (or GPR_TIMESPAN if
      this is a relative time meaure) */
  gpr_clock_type clock_type;
} gpr_timespec;

#ifdef __cplusplus
}
#endif

#endif /* GRPC_IMPL_CODEGEN_GPR_TYPES_H */
