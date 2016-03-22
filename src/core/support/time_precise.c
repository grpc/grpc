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

#include <grpc/support/log.h>
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
  *clk = (long long)(high << 32) | (long long)low;
}
#endif

static double cycles_per_second = 0;
static long long int start_cycle;
void gpr_precise_clock_init(void) {
  time_t start;
  long long end_cycle;
  gpr_log(GPR_DEBUG, "Calibrating timers");
  start = time(NULL);
  while (time(NULL) == start)
    ;
  gpr_get_cycle_counter(&start_cycle);
  while (time(NULL) <= start + 10)
    ;
  gpr_get_cycle_counter(&end_cycle);
  cycles_per_second = (double)(end_cycle - start_cycle) / 10.0;
  gpr_log(GPR_DEBUG, "... cycles_per_second = %f\n", cycles_per_second);
}

void gpr_precise_clock_now(gpr_timespec *clk) {
  long long int counter;
  double secs;
  gpr_get_cycle_counter(&counter);
  secs = (double)(counter - start_cycle) / cycles_per_second;
  clk->clock_type = GPR_CLOCK_PRECISE;
  clk->tv_sec = (int64_t)secs;
  clk->tv_nsec = (int32_t)(1e9 * (secs - (double)clk->tv_sec));
}

#else  /* GRPC_TIMERS_RDTSC */
void gpr_precise_clock_init(void) {}

void gpr_precise_clock_now(gpr_timespec *clk) {
  *clk = gpr_now(GPR_CLOCK_REALTIME);
  clk->clock_type = GPR_CLOCK_PRECISE;
}
#endif /* GRPC_TIMERS_RDTSC */
