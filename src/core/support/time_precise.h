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

#ifndef GRPC_CORE_SUPPORT_TIME_PRECISE_H_
#define GRPC_CORE_SUPPORT_TIME_PRECISE_H_

#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <stdio.h>

#ifdef GRPC_TIMERS_RDTSC
#if defined(__i386__)
static void gpr_get_cycle_counter(long long int *clk) {
  long long int ret;
  __asm__ volatile("rdtsc" : "=A"(ret));
  *clk = ret;
}

// ----------------------------------------------------------------
#elif defined(__x86_64__) || defined(__amd64__)
static void gpr_get_cycle_counter(long long int *clk) {
  unsigned long long low, high;
  __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
  *clk = (high << 32) | low;
}
#endif

static gpr_once precise_clock_init = GPR_ONCE_INIT;
static long long cycles_per_second = 0;
static void gpr_precise_clock_init() {
  time_t start = time(NULL);
  gpr_precise_clock start_cycle;
  gpr_precise_clock end_cycle;
  while (time(NULL) == start)
    ;
  gpr_get_cycle_counter(&start_cycle);
  while (time(NULL) == start + 1)
    ;
  gpr_get_cycle_counter(&end_cycle);
  cycles_per_second = end_cycle - start_cycle;
}

static double grpc_precise_clock_scaling_factor() {
  gpr_once_init(&precise_clock_init, grpc_precise_clock_init);
  return 1e6 / cycles_per_second;
}

static void gpr_precise_clock_now(gpr_timespec *clk) {
  long long int counter;
  gpr_get_cycle_counter(&counter);
  clk->clock = GPR_CLOCK_REALTIME;
  clk->tv_sec = counter / cycles_per_second;
  clk->tv_nsec = counter % cycles_per_second;
}

#else /* GRPC_TIMERS_RDTSC */
static void gpr_precise_clock_now(gpr_timespec *clk) {
  *clk = gpr_now(GPR_CLOCK_REALTIME);
  clk->clock_type = GPR_CLOCK_PRECISE;
}
#endif /* GRPC_TIMERS_RDTSC */

#endif /* GRPC_CORE_SUPPORT_TIME_PRECISE_ */
