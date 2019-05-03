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

#include <grpc/support/port_platform.h>

#include <algorithm>

#include <grpc/impl/codegen/gpr_types.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <stdio.h>

#include "src/core/lib/gpr/time_precise.h"

#ifdef GRPC_TIMERS_RDTSC
#ifndef GRPC_CUSTOME_CYCLE_CLOCK
static double cycles_per_second = 0;
static gpr_cycle_counter start_cycle;
void gpr_precise_clock_init(void) {
  gpr_log(GPR_DEBUG, "Calibrating timers");

  // Start from a loop of 1ms, and gradually increase the loop duration
  // until we either converge or we have passed 255ms (1ms+2ms+...+128ms).
  int64_t measurement_dur_ns = GPR_NS_PER_MS;
  double last_freq = -1;
  bool converged = false;
  for (int i = 0; i < 8 && !converged; ++i, measurement_dur_ns *= 2) {
    start_cycle = gpr_get_cycle_counter();
    int64_t loop_dur_ns;
    gpr_timespec start = gpr_now(GPR_CLOCK_MONOTONIC);
    do {
      // TODO(soheil): Maybe sleep instead of busy polling.
      gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
      gpr_timespec delta = gpr_time_sub(now, start);
      loop_dur_ns = delta.tv_sec * GPR_NS_PER_SEC + delta.tv_nsec;
      // On fake test, loop_dur_ns will be 0. If that happens just end the loop,
      // because there is no point in finding the frequency.
    } while (loop_dur_ns != 0 && loop_dur_ns < measurement_dur_ns);
    gpr_cycle_counter end_cycle = gpr_get_cycle_counter();

    // Frequency should be in Hz.
    const double freq = loop_dur_ns == 0  // Is it a test fake clock?
                            ? 1
                            : static_cast<double>(end_cycle - start_cycle) /
                                  loop_dur_ns * GPR_NS_PER_SEC;
    converged =
        last_freq != -1 && (freq * 0.99 < last_freq && last_freq < freq * 1.01);
    last_freq = freq;
  }
  cycles_per_second = std::max<double>(1, last_freq);
  gpr_log(GPR_DEBUG, "... cycles_per_second = %f\n", cycles_per_second);
}

gpr_timespec gpr_cycle_counter_to_timestamp(gpr_cycle_counter cycles) {
  double secs = static_cast<double>(cycles - start_cycle) / cycles_per_second;
  gpr_timespec ts;
  ts.tv_sec = static_cast<int64_t>(secs);
  ts.tv_nsec =
      static_cast<int32_t>(1e9 * (secs - static_cast<double>(ts.tv_sec)));
  ts.clock_type = GPR_CLOCK_PRECISE;
  return ts;
}

void gpr_precise_clock_now(gpr_timespec* clk) {
  int64_t counter = gpr_get_cycle_counter();
  *clk = gpr_cycle_counter_to_timestamp(counter);
}

#endif /* GRPC_CUSTOME_CYCLE_CLOCK */
#else  /* GRPC_TIMERS_RDTSC */
void gpr_precise_clock_init(void) {}

gpr_cycle_counter gpr_get_cycle_counter() {
  gpr_timespec ts = gpr_now(GPR_CLOCK_REALTIME);
  return gpr_timespec_to_micros(ts);
}

gpr_timespec gpr_cycle_counter_to_timestamp(gpr_cycle_counter cycles) {
  gpr_timespec ts;
  ts.tv_sec = cycles / GPR_US_PER_SEC;
  ts.tv_nsec = (cycles - ts.tv_sec * GPR_US_PER_SEC) * GPR_NS_PER_US;
  ts.clock_type = GPR_CLOCK_PRECISE;
  return ts;
}

void gpr_precise_clock_now(gpr_timespec* clk) {
  *clk = gpr_now(GPR_CLOCK_REALTIME);
  clk->clock_type = GPR_CLOCK_PRECISE;
}
#endif /* GRPC_TIMERS_RDTSC */
