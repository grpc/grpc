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

#ifndef GRPC_CORE_PROFILING_TIMERS_PRECISECLOCK_H
#define GRPC_CORE_PROFILING_TIMERS_PRECISECLOCK_H

#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <stdio.h>

#ifdef GRPC_TIMERS_RDTSC
typedef long long int grpc_precise_clock;
#if defined(__i386__)
static void grpc_precise_clock_now(grpc_precise_clock *clk) {
  grpc_precise_clock ret;
  __asm__ volatile("rdtsc" : "=A"(ret));
  *clk = ret;
}

// ----------------------------------------------------------------
#elif defined(__x86_64__) || defined(__amd64__)
static void grpc_precise_clock_now(grpc_precise_clock *clk) {
  unsigned long long low, high;
  __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
  *clk = (high << 32) | low;
}
#endif
static gpr_once precise_clock_init = GPR_ONCE_INIT;
static double cycles_per_second = 0.0;
static void grpc_precise_clock_init() {
  time_t start = time(NULL);
  grpc_precise_clock start_time;
  grpc_precise_clock end_time;
  while (time(NULL) == start)
    ;
  grpc_precise_clock_now(&start_time);
  while (time(NULL) == start + 1)
    ;
  grpc_precise_clock_now(&end_time);
  cycles_per_second = end_time - start_time;
}
static double grpc_precise_clock_scaling_factor() {
  gpr_once_init(&precise_clock_init, grpc_precise_clock_init);
  return 1e6 / cycles_per_second;
}
#define GRPC_PRECISE_CLOCK_FORMAT "%f"
#define GRPC_PRECISE_CLOCK_PRINTF_ARGS(clk) \
  (*(clk)*grpc_precise_clock_scaling_factor())
#else
typedef struct grpc_precise_clock grpc_precise_clock;
struct grpc_precise_clock {
  gpr_timespec clock;
};
static void grpc_precise_clock_now(grpc_precise_clock* clk) {
  clk->clock = gpr_now(GPR_CLOCK_REALTIME);
}
#define GRPC_PRECISE_CLOCK_FORMAT "%ld.%09d"
#define GRPC_PRECISE_CLOCK_PRINTF_ARGS(clk) \
  (clk)->clock.tv_sec, (clk)->clock.tv_nsec
static void grpc_precise_clock_print(const grpc_precise_clock* clk, FILE* fp) {
  fprintf(fp, "%ld.%09d", clk->clock.tv_sec, clk->clock.tv_nsec);
}
#endif /* GRPC_TIMERS_RDTSC */

#endif /* GRPC_CORE_PROFILING_TIMERS_PRECISECLOCK_H */
