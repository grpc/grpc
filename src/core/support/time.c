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

#include <grpc/support/time.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <grpc/support/log.h>

int gpr_time_cmp(gpr_timespec a, gpr_timespec b) {
  int cmp = (a.tv_sec > b.tv_sec) - (a.tv_sec < b.tv_sec);
  if (cmp == 0) {
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

/* There's no standard TIME_T_MIN and TIME_T_MAX, so we construct them.  The
   following assumes that signed types are two's-complement and that bytes are
   8 bits.  */

/* The top bit of integral type t. */
#define TOP_BIT_OF_TYPE(t) (((gpr_uintmax)1) << ((8 * sizeof(t)) - 1))

/* Return whether integral type t is signed. */
#define TYPE_IS_SIGNED(t) (((t)1) > (t) ~(t)0)

/* The minimum and maximum value of integral type t. */
#define TYPE_MIN(t) ((t)(TYPE_IS_SIGNED(t) ? TOP_BIT_OF_TYPE(t) : 0))
#define TYPE_MAX(t)                                 \
  ((t)(TYPE_IS_SIGNED(t) ? (TOP_BIT_OF_TYPE(t) - 1) \
                         : ((TOP_BIT_OF_TYPE(t) - 1) << 1) + 1))

const gpr_timespec gpr_time_0 = {0, 0};
const gpr_timespec gpr_inf_future = {TYPE_MAX(time_t), 0};
const gpr_timespec gpr_inf_past = {TYPE_MIN(time_t), 0};

/* TODO(ctiller): consider merging _nanos, _micros, _millis into a single
   function for maintainability. Similarly for _seconds, _minutes, and _hours */

gpr_timespec gpr_time_from_nanos(long ns) {
  gpr_timespec result;
  if (ns == LONG_MAX) {
    result = gpr_inf_future;
  } else if (ns == LONG_MIN) {
    result = gpr_inf_past;
  } else if (ns >= 0) {
    result.tv_sec = ns / 1000000000;
    result.tv_nsec = ns - result.tv_sec * 1000000000;
  } else {
    /* Calculation carefully formulated to avoid any possible under/overflow. */
    result.tv_sec = (-(999999999 - (ns + 1000000000)) / 1000000000) - 1;
    result.tv_nsec = ns - result.tv_sec * 1000000000;
  }
  return result;
}

gpr_timespec gpr_time_from_micros(long us) {
  gpr_timespec result;
  if (us == LONG_MAX) {
    result = gpr_inf_future;
  } else if (us == LONG_MIN) {
    result = gpr_inf_past;
  } else if (us >= 0) {
    result.tv_sec = us / 1000000;
    result.tv_nsec = (us - result.tv_sec * 1000000) * 1000;
  } else {
    /* Calculation carefully formulated to avoid any possible under/overflow. */
    result.tv_sec = (-(999999 - (us + 1000000)) / 1000000) - 1;
    result.tv_nsec = (us - result.tv_sec * 1000000) * 1000;
  }
  return result;
}

gpr_timespec gpr_time_from_millis(long ms) {
  gpr_timespec result;
  if (ms == LONG_MAX) {
    result = gpr_inf_future;
  } else if (ms == LONG_MIN) {
    result = gpr_inf_past;
  } else if (ms >= 0) {
    result.tv_sec = ms / 1000;
    result.tv_nsec = (ms - result.tv_sec * 1000) * 1000000;
  } else {
    /* Calculation carefully formulated to avoid any possible under/overflow. */
    result.tv_sec = (-(999 - (ms + 1000)) / 1000) - 1;
    result.tv_nsec = (ms - result.tv_sec * 1000) * 1000000;
  }
  return result;
}

gpr_timespec gpr_time_from_seconds(long s) {
  gpr_timespec result;
  if (s == LONG_MAX) {
    result = gpr_inf_future;
  } else if (s == LONG_MIN) {
    result = gpr_inf_past;
  } else {
    result.tv_sec = s;
    result.tv_nsec = 0;
  }
  return result;
}

gpr_timespec gpr_time_from_minutes(long m) {
  gpr_timespec result;
  if (m >= LONG_MAX / 60) {
    result = gpr_inf_future;
  } else if (m <= LONG_MIN / 60) {
    result = gpr_inf_past;
  } else {
    result.tv_sec = m * 60;
    result.tv_nsec = 0;
  }
  return result;
}

gpr_timespec gpr_time_from_hours(long h) {
  gpr_timespec result;
  if (h >= LONG_MAX / 3600) {
    result = gpr_inf_future;
  } else if (h <= LONG_MIN / 3600) {
    result = gpr_inf_past;
  } else {
    result.tv_sec = h * 3600;
    result.tv_nsec = 0;
  }
  return result;
}

gpr_timespec gpr_time_add(gpr_timespec a, gpr_timespec b) {
  gpr_timespec sum;
  int inc = 0;
  sum.tv_nsec = a.tv_nsec + b.tv_nsec;
  if (sum.tv_nsec >= 1000000000) {
    sum.tv_nsec -= 1000000000;
    inc++;
  }
  if (a.tv_sec == TYPE_MAX(time_t) || a.tv_sec == TYPE_MIN(time_t)) {
    sum = a;
  } else if (b.tv_sec == TYPE_MAX(time_t) ||
             (b.tv_sec >= 0 && a.tv_sec >= TYPE_MAX(time_t) - b.tv_sec)) {
    sum = gpr_inf_future;
  } else if (b.tv_sec == TYPE_MIN(time_t) ||
             (b.tv_sec <= 0 && a.tv_sec <= TYPE_MIN(time_t) - b.tv_sec)) {
    sum = gpr_inf_past;
  } else {
    sum.tv_sec = a.tv_sec + b.tv_sec;
    if (inc != 0 && sum.tv_sec == TYPE_MAX(time_t) - 1) {
      sum = gpr_inf_future;
    } else {
      sum.tv_sec += inc;
    }
  }
  return sum;
}

gpr_timespec gpr_time_sub(gpr_timespec a, gpr_timespec b) {
  gpr_timespec diff;
  int dec = 0;
  diff.tv_nsec = a.tv_nsec - b.tv_nsec;
  if (diff.tv_nsec < 0) {
    diff.tv_nsec += 1000000000;
    dec++;
  }
  if (a.tv_sec == TYPE_MAX(time_t) || a.tv_sec == TYPE_MIN(time_t)) {
    diff = a;
  } else if (b.tv_sec == TYPE_MIN(time_t) ||
             (b.tv_sec <= 0 && a.tv_sec >= TYPE_MAX(time_t) + b.tv_sec)) {
    diff = gpr_inf_future;
  } else if (b.tv_sec == TYPE_MAX(time_t) ||
             (b.tv_sec >= 0 && a.tv_sec <= TYPE_MIN(time_t) + b.tv_sec)) {
    diff = gpr_inf_past;
  } else {
    diff.tv_sec = a.tv_sec - b.tv_sec;
    if (dec != 0 && diff.tv_sec == TYPE_MIN(time_t) + 1) {
      diff = gpr_inf_past;
    } else {
      diff.tv_sec -= dec;
    }
  }
  return diff;
}

int gpr_time_similar(gpr_timespec a, gpr_timespec b, gpr_timespec threshold) {
  int cmp_ab;

  cmp_ab = gpr_time_cmp(a, b);
  if (cmp_ab == 0) return 1;
  if (cmp_ab < 0) {
    return gpr_time_cmp(gpr_time_sub(b, a), threshold) <= 0;
  } else {
    return gpr_time_cmp(gpr_time_sub(a, b), threshold) <= 0;
  }
}

gpr_int32 gpr_time_to_millis(gpr_timespec t) {
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
    return t.tv_sec * GPR_MS_PER_SEC + t.tv_nsec / GPR_NS_PER_MS;
  }
}

double gpr_timespec_to_micros(gpr_timespec t) {
  return t.tv_sec * GPR_US_PER_SEC + t.tv_nsec * 1e-3;
}
