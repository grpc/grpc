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

#if GPR_LINUX
#include <fcntl.h>
#include <unistd.h>
#endif

#include <algorithm>

#include <grpc/impl/codegen/gpr_types.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/time_precise.h"

#if GPR_CYCLE_COUNTER_RDTSC_32 or GPR_CYCLE_COUNTER_RDTSC_64
#if GPR_LINUX
static bool read_freq_from_kernel(double* freq) {
  // Google production kernel export the frequency for us in kHz.
  int fd = open("/sys/devices/system/cpu/cpu0/tsc_freq_khz", O_RDONLY);
  if (fd == -1) {
    return false;
  }
  char line[1024] = {};
  char* err;
  bool ret = false;
  int len = read(fd, line, sizeof(line) - 1);
  if (len > 0) {
    const long val = strtol(line, &err, 10);
    if (line[0] != '\0' && (*err == '\n' || *err == '\0')) {
      *freq = val * 1e3;  // Value is kHz.
      ret = true;
    }
  }
  close(fd);
  return ret;
}
#endif /* GPR_LINUX */

static double cycles_per_second = 0;
static gpr_cycle_counter start_cycle;

static bool is_fake_clock() {
  gpr_timespec start = gpr_now(GPR_CLOCK_MONOTONIC);
  int64_t sum = 0;
  for (int i = 0; i < 8; ++i) {
    gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
    gpr_timespec delta = gpr_time_sub(now, start);
    sum += delta.tv_sec * GPR_NS_PER_SEC + delta.tv_nsec;
  }
  // If the clock doesn't move even a nano after 8 tries, it's a fake one.
  return sum == 0;
}

void gpr_precise_clock_init(void) {
  gpr_log(GPR_DEBUG, "Calibrating timers");

#if GPR_LINUX
  if (read_freq_from_kernel(&cycles_per_second)) {
    start_cycle = gpr_get_cycle_counter();
    return;
  }
#endif /* GPR_LINUX */

  if (is_fake_clock()) {
    cycles_per_second = 1;
    start_cycle = 0;
    return;
  }
  // Start from a loop of 1ms, and gradually increase the loop duration
  // until we either converge or we have passed 255ms (1ms+2ms+...+128ms).
  int64_t measurement_ns = GPR_NS_PER_MS;
  double last_freq = -1;
  bool converged = false;
  for (int i = 0; i < 8 && !converged; ++i, measurement_ns *= 2) {
    start_cycle = gpr_get_cycle_counter();
    int64_t loop_ns;
    gpr_timespec start = gpr_now(GPR_CLOCK_MONOTONIC);
    do {
      // TODO(soheil): Maybe sleep instead of busy polling.
      gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
      gpr_timespec delta = gpr_time_sub(now, start);
      loop_ns = delta.tv_sec * GPR_NS_PER_SEC + delta.tv_nsec;
    } while (loop_ns < measurement_ns);
    gpr_cycle_counter end_cycle = gpr_get_cycle_counter();
    // Frequency should be in Hz.
    const double freq =
        static_cast<double>(end_cycle - start_cycle) / loop_ns * GPR_NS_PER_SEC;
    converged =
        last_freq != -1 && (freq * 0.99 < last_freq && last_freq < freq * 1.01);
    last_freq = freq;
  }
  cycles_per_second = last_freq;
  gpr_log(GPR_DEBUG, "... cycles_per_second = %f\n", cycles_per_second);
}

gpr_timespec gpr_cycle_counter_to_time(gpr_cycle_counter cycles) {
  const double secs =
      static_cast<double>(cycles - start_cycle) / cycles_per_second;
  gpr_timespec ts;
  ts.tv_sec = static_cast<int64_t>(secs);
  ts.tv_nsec = static_cast<int32_t>(GPR_NS_PER_SEC *
                                    (secs - static_cast<double>(ts.tv_sec)));
  ts.clock_type = GPR_CLOCK_PRECISE;
  return ts;
}

gpr_timespec gpr_cycle_counter_sub(gpr_cycle_counter a, gpr_cycle_counter b) {
  const double secs = static_cast<double>(a - b) / cycles_per_second;
  gpr_timespec ts;
  ts.tv_sec = static_cast<int64_t>(secs);
  ts.tv_nsec = static_cast<int32_t>(GPR_NS_PER_SEC *
                                    (secs - static_cast<double>(ts.tv_sec)));
  ts.clock_type = GPR_TIMESPAN;
  return ts;
}

void gpr_precise_clock_now(gpr_timespec* clk) {
  int64_t counter = gpr_get_cycle_counter();
  *clk = gpr_cycle_counter_to_time(counter);
}
#elif GPR_CYCLE_COUNTER_FALLBACK
void gpr_precise_clock_init(void) {}

gpr_cycle_counter gpr_get_cycle_counter() {
  gpr_timespec ts = gpr_now(GPR_CLOCK_REALTIME);
  return gpr_timespec_to_micros(ts);
}

gpr_timespec gpr_cycle_counter_to_time(gpr_cycle_counter cycles) {
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

gpr_timespec gpr_cycle_counter_sub(gpr_cycle_counter a, gpr_cycle_counter b) {
  return gpr_time_sub(gpr_cycle_counter_to_time(a),
                      gpr_cycle_counter_to_time(b));
}
#endif /* GPR_CYCLE_COUNTER_FALLBACK */
