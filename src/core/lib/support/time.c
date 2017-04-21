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

/* Generic implementation of time calls. */

#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

int gpr_time_cmp(gpr_timespec a, gpr_timespec b) {
  int cmp = (a.tv_sec > b.tv_sec) - (a.tv_sec < b.tv_sec);
  GPR_ASSERT(a.clock_type == b.clock_type);
  if (cmp == 0 && a.tv_sec != INT64_MAX && a.tv_sec != INT64_MIN) {
    cmp = (a.tv_nsec > b.tv_nsec) - (a.tv_nsec < b.tv_nsec);
  }
  return cmp;
}

gpr_timespec gpr_time_min(gpr_timespec a, gpr_timespec b) {
  return gpr_time_cmp(a, b) < 0 ? a : b;
}

gpr_timespec gpr_time_max(gpr_timespec a, gpr_timespec b) {
  return gpr_time_cmp(a, b) > 0 ? a : b;
}

gpr_timespec gpr_time_0(gpr_clock_type type) {
  gpr_timespec out;
  out.tv_sec = 0;
  out.tv_nsec = 0;
  out.clock_type = type;
  return out;
}

gpr_timespec gpr_inf_future(gpr_clock_type type) {
  gpr_timespec out;
  out.tv_sec = INT64_MAX;
  out.tv_nsec = 0;
  out.clock_type = type;
  return out;
}

gpr_timespec gpr_inf_past(gpr_clock_type type) {
  gpr_timespec out;
  out.tv_sec = INT64_MIN;
  out.tv_nsec = 0;
  out.clock_type = type;
  return out;
}

static gpr_timespec to_seconds_from_sub_second_time(int64_t time_in_units,
                                                    int64_t units_per_sec,
                                                    gpr_clock_type type) {
  gpr_timespec out;
  if (time_in_units == INT64_MAX) {
    out = gpr_inf_future(type);
  } else if (time_in_units == INT64_MIN) {
    out = gpr_inf_past(type);
  } else {
    if (time_in_units >= 0) {
      out.tv_sec = time_in_units / units_per_sec;
    } else {
      out.tv_sec = (-((units_per_sec - 1) - (time_in_units + units_per_sec)) /
                    units_per_sec) -
                   1;
    }
    out.tv_nsec = (int32_t)((time_in_units - out.tv_sec * units_per_sec) *
                            GPR_NS_PER_SEC / units_per_sec);
    out.clock_type = type;
  }
  return out;
}

static gpr_timespec to_seconds_from_above_second_time(int64_t time_in_units,
                                                      int64_t secs_per_unit,
                                                      gpr_clock_type type) {
  gpr_timespec out;
  if (time_in_units >= INT64_MAX / secs_per_unit) {
    out = gpr_inf_future(type);
  } else if (time_in_units <= INT64_MIN / secs_per_unit) {
    out = gpr_inf_past(type);
  } else {
    out.tv_sec = time_in_units * secs_per_unit;
    out.tv_nsec = 0;
    out.clock_type = type;
  }
  return out;
}

gpr_timespec gpr_time_from_nanos(int64_t ns, gpr_clock_type type) {
  return to_seconds_from_sub_second_time(ns, GPR_NS_PER_SEC, type);
}

gpr_timespec gpr_time_from_micros(int64_t us, gpr_clock_type type) {
  return to_seconds_from_sub_second_time(us, GPR_US_PER_SEC, type);
}

gpr_timespec gpr_time_from_millis(int64_t ms, gpr_clock_type type) {
  return to_seconds_from_sub_second_time(ms, GPR_MS_PER_SEC, type);
}

gpr_timespec gpr_time_from_seconds(int64_t s, gpr_clock_type type) {
  return to_seconds_from_sub_second_time(s, 1, type);
}

gpr_timespec gpr_time_from_minutes(int64_t m, gpr_clock_type type) {
  return to_seconds_from_above_second_time(m, 60, type);
}

gpr_timespec gpr_time_from_hours(int64_t h, gpr_clock_type type) {
  return to_seconds_from_above_second_time(h, 3600, type);
}

gpr_timespec gpr_time_add(gpr_timespec a, gpr_timespec b) {
  gpr_timespec sum;
  int64_t inc = 0;
  GPR_ASSERT(b.clock_type == GPR_TIMESPAN);
  sum.clock_type = a.clock_type;
  sum.tv_nsec = a.tv_nsec + b.tv_nsec;
  if (sum.tv_nsec >= GPR_NS_PER_SEC) {
    sum.tv_nsec -= GPR_NS_PER_SEC;
    inc++;
  }
  if (a.tv_sec == INT64_MAX || a.tv_sec == INT64_MIN) {
    sum = a;
  } else if (b.tv_sec == INT64_MAX ||
             (b.tv_sec >= 0 && a.tv_sec >= INT64_MAX - b.tv_sec)) {
    sum = gpr_inf_future(sum.clock_type);
  } else if (b.tv_sec == INT64_MIN ||
             (b.tv_sec <= 0 && a.tv_sec <= INT64_MIN - b.tv_sec)) {
    sum = gpr_inf_past(sum.clock_type);
  } else {
    sum.tv_sec = a.tv_sec + b.tv_sec;
    if (inc != 0 && sum.tv_sec == INT64_MAX - 1) {
      sum = gpr_inf_future(sum.clock_type);
    } else {
      sum.tv_sec += inc;
    }
  }
  return sum;
}

gpr_timespec gpr_time_sub(gpr_timespec a, gpr_timespec b) {
  gpr_timespec diff;
  int64_t dec = 0;
  if (b.clock_type == GPR_TIMESPAN) {
    diff.clock_type = a.clock_type;
  } else {
    GPR_ASSERT(a.clock_type == b.clock_type);
    diff.clock_type = GPR_TIMESPAN;
  }
  diff.tv_nsec = a.tv_nsec - b.tv_nsec;
  if (diff.tv_nsec < 0) {
    diff.tv_nsec += GPR_NS_PER_SEC;
    dec++;
  }
  if (a.tv_sec == INT64_MAX || a.tv_sec == INT64_MIN) {
    diff = a;
  } else if (b.tv_sec == INT64_MIN ||
             (b.tv_sec <= 0 && a.tv_sec >= INT64_MAX + b.tv_sec)) {
    diff = gpr_inf_future(GPR_CLOCK_REALTIME);
  } else if (b.tv_sec == INT64_MAX ||
             (b.tv_sec >= 0 && a.tv_sec <= INT64_MIN + b.tv_sec)) {
    diff = gpr_inf_past(GPR_CLOCK_REALTIME);
  } else {
    diff.tv_sec = a.tv_sec - b.tv_sec;
    if (dec != 0 && diff.tv_sec == INT64_MIN + 1) {
      diff = gpr_inf_past(GPR_CLOCK_REALTIME);
    } else {
      diff.tv_sec -= dec;
    }
  }
  return diff;
}

int gpr_time_similar(gpr_timespec a, gpr_timespec b, gpr_timespec threshold) {
  int cmp_ab;

  GPR_ASSERT(a.clock_type == b.clock_type);
  GPR_ASSERT(threshold.clock_type == GPR_TIMESPAN);

  cmp_ab = gpr_time_cmp(a, b);
  if (cmp_ab == 0) return 1;
  if (cmp_ab < 0) {
    return gpr_time_cmp(gpr_time_sub(b, a), threshold) <= 0;
  } else {
    return gpr_time_cmp(gpr_time_sub(a, b), threshold) <= 0;
  }
}

int32_t gpr_time_to_millis(gpr_timespec t) {
  if (t.tv_sec >= 2147483) {
    if (t.tv_sec == 2147483 && t.tv_nsec < 648 * GPR_NS_PER_MS) {
      return 2147483 * GPR_MS_PER_SEC + t.tv_nsec / GPR_NS_PER_MS;
    }
    return 2147483647;
  } else if (t.tv_sec <= -2147483) {
    /* TODO(ctiller): correct handling here (it's so far in the past do we
       care?) */
    return -2147483647;
  } else {
    return (int32_t)(t.tv_sec * GPR_MS_PER_SEC + t.tv_nsec / GPR_NS_PER_MS);
  }
}

double gpr_timespec_to_micros(gpr_timespec t) {
  return (double)t.tv_sec * GPR_US_PER_SEC + t.tv_nsec * 1e-3;
}

gpr_timespec gpr_convert_clock_type(gpr_timespec t, gpr_clock_type clock_type) {
  if (t.clock_type == clock_type) {
    return t;
  }

  if (t.tv_sec == INT64_MAX || t.tv_sec == INT64_MIN) {
    t.clock_type = clock_type;
    return t;
  }

  if (clock_type == GPR_TIMESPAN) {
    return gpr_time_sub(t, gpr_now(t.clock_type));
  }

  if (t.clock_type == GPR_TIMESPAN) {
    return gpr_time_add(gpr_now(clock_type), t);
  }

  return gpr_time_add(gpr_now(clock_type),
                      gpr_time_sub(t, gpr_now(t.clock_type)));
}
