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

#ifndef GRPC_SUPPORT_TIME_H
#define GRPC_SUPPORT_TIME_H

#include <grpc/impl/codegen/gpr_types.h>

#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Time constants. */
GPRAPI gpr_timespec
gpr_time_0(gpr_clock_type type); /** The zero time interval. */
GPRAPI gpr_timespec gpr_inf_future(gpr_clock_type type); /** The far future */
GPRAPI gpr_timespec gpr_inf_past(gpr_clock_type type);   /** The far past. */

#define GPR_MS_PER_SEC 1000
#define GPR_US_PER_SEC 1000000
#define GPR_NS_PER_SEC 1000000000
#define GPR_NS_PER_MS 1000000
#define GPR_NS_PER_US 1000
#define GPR_US_PER_MS 1000

/** initialize time subsystem */
GPRAPI void gpr_time_init(void);

/** Return the current time measured from the given clocks epoch. */
GPRAPI gpr_timespec gpr_now(gpr_clock_type clock);

/** Convert a timespec from one clock to another */
GPRAPI gpr_timespec gpr_convert_clock_type(gpr_timespec t,
                                           gpr_clock_type target_clock);

/** Return -ve, 0, or +ve according to whether a < b, a == b, or a > b
   respectively.  */
GPRAPI int gpr_time_cmp(gpr_timespec a, gpr_timespec b);

GPRAPI gpr_timespec gpr_time_max(gpr_timespec a, gpr_timespec b);
GPRAPI gpr_timespec gpr_time_min(gpr_timespec a, gpr_timespec b);

/** Add and subtract times.  Calculations saturate at infinities. */
GPRAPI gpr_timespec gpr_time_add(gpr_timespec a, gpr_timespec b);
GPRAPI gpr_timespec gpr_time_sub(gpr_timespec a, gpr_timespec b);

/** Return a timespec representing a given number of time units. INT64_MIN is
   interpreted as gpr_inf_past, and INT64_MAX as gpr_inf_future.  */
GPRAPI gpr_timespec gpr_time_from_micros(int64_t x, gpr_clock_type clock_type);
GPRAPI gpr_timespec gpr_time_from_nanos(int64_t x, gpr_clock_type clock_type);
GPRAPI gpr_timespec gpr_time_from_millis(int64_t x, gpr_clock_type clock_type);
GPRAPI gpr_timespec gpr_time_from_seconds(int64_t x, gpr_clock_type clock_type);
GPRAPI gpr_timespec gpr_time_from_minutes(int64_t x, gpr_clock_type clock_type);
GPRAPI gpr_timespec gpr_time_from_hours(int64_t x, gpr_clock_type clock_type);

GPRAPI int32_t gpr_time_to_millis(gpr_timespec timespec);

/** Return 1 if two times are equal or within threshold of each other,
   0 otherwise */
GPRAPI int gpr_time_similar(gpr_timespec a, gpr_timespec b,
                            gpr_timespec threshold);

/** Sleep until at least 'until' - an absolute timeout */
GPRAPI void gpr_sleep_until(gpr_timespec until);

GPRAPI double gpr_timespec_to_micros(gpr_timespec t);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_SUPPORT_TIME_H */
