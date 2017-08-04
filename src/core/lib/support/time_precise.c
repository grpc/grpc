/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <stdio.h>

#ifdef GRPC_TIMERS_RDTSC
#if defined(__i386__)
static void gpr_get_cycle_counter(int64_t int *clk) {
  int64_t int ret;
  __asm__ volatile("rdtsc" : "=A"(ret));
  *clk = ret;
}

// ----------------------------------------------------------------
#elif defined(__x86_64__) || defined(__amd64__)
static void gpr_get_cycle_counter(int64_t *clk) {
  uint64_t low, high;
  __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
  *clk = (int64_t)(high << 32) | (int64_t)low;
}
#endif

static double cycles_per_second = 0;
static int64_t start_cycle;
void gpr_precise_clock_init(void) {
  time_t start;
  int64_t end_cycle;
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
  int64_t counter;
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
