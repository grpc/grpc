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

#include "src/core/lib/transport/timeout_encoding.h"

#include <stdio.h>
#include <string.h>

#include <grpc/support/port_platform.h>
#include "src/core/lib/support/string.h"

static int64_t round_up(int64_t x, int64_t divisor) {
  return (x / divisor + (x % divisor != 0)) * divisor;
}

/* round an integer up to the next value with three significant figures */
static int64_t round_up_to_three_sig_figs(int64_t x) {
  if (x < 1000) return x;
  if (x < 10000) return round_up(x, 10);
  if (x < 100000) return round_up(x, 100);
  if (x < 1000000) return round_up(x, 1000);
  if (x < 10000000) return round_up(x, 10000);
  if (x < 100000000) return round_up(x, 100000);
  if (x < 1000000000) return round_up(x, 1000000);
  return round_up(x, 10000000);
}

/* encode our minimum viable timeout value */
static void enc_tiny(char *buffer) { memcpy(buffer, "1n", 3); }

static void enc_ext(char *buffer, int64_t value, char ext) {
  int n = int64_ttoa(value, buffer);
  buffer[n] = ext;
  buffer[n + 1] = 0;
}

static void enc_seconds(char *buffer, int64_t sec) {
  if (sec % 3600 == 0) {
    enc_ext(buffer, sec / 3600, 'H');
  } else if (sec % 60 == 0) {
    enc_ext(buffer, sec / 60, 'M');
  } else {
    enc_ext(buffer, sec, 'S');
  }
}

static void enc_nanos(char *buffer, int64_t x) {
  x = round_up_to_three_sig_figs(x);
  if (x < 100000) {
    if (x % 1000 == 0) {
      enc_ext(buffer, x / 1000, 'u');
    } else {
      enc_ext(buffer, x, 'n');
    }
  } else if (x < 100000000) {
    if (x % 1000000 == 0) {
      enc_ext(buffer, x / 1000000, 'm');
    } else {
      enc_ext(buffer, x / 1000, 'u');
    }
  } else if (x < 1000000000) {
    enc_ext(buffer, x / 1000000, 'm');
  } else {
    /* note that this is only ever called with times of less than one second,
       so if we reach here the time must have been rounded up to a whole second
       (and no more) */
    memcpy(buffer, "1S", 3);
  }
}

static void enc_micros(char *buffer, int64_t x) {
  x = round_up_to_three_sig_figs(x);
  if (x < 100000) {
    if (x % 1000 == 0) {
      enc_ext(buffer, x / 1000, 'm');
    } else {
      enc_ext(buffer, x, 'u');
    }
  } else if (x < 100000000) {
    if (x % 1000000 == 0) {
      enc_ext(buffer, x / 1000000, 'S');
    } else {
      enc_ext(buffer, x / 1000, 'm');
    }
  } else {
    enc_ext(buffer, x / 1000000, 'S');
  }
}

void grpc_http2_encode_timeout(gpr_timespec timeout, char *buffer) {
  if (timeout.tv_sec < 0) {
    enc_tiny(buffer);
  } else if (timeout.tv_sec == 0) {
    enc_nanos(buffer, timeout.tv_nsec);
  } else if (timeout.tv_sec < 1000 && timeout.tv_nsec != 0) {
    enc_micros(buffer,
               (int64_t)(timeout.tv_sec * 1000000) +
                   (timeout.tv_nsec / 1000 + (timeout.tv_nsec % 1000 != 0)));
  } else {
    enc_seconds(buffer, timeout.tv_sec + (timeout.tv_nsec != 0));
  }
}

static int is_all_whitespace(const char *p, const char *end) {
  while (p != end && *p == ' ') p++;
  return p == end;
}

int grpc_http2_decode_timeout(grpc_slice text, gpr_timespec *timeout) {
  int32_t x = 0;
  const uint8_t *p = GRPC_SLICE_START_PTR(text);
  const uint8_t *end = GRPC_SLICE_END_PTR(text);
  int have_digit = 0;
  /* skip whitespace */
  for (; p != end && *p == ' '; p++)
    ;
  /* decode numeric part */
  for (; p != end && *p >= '0' && *p <= '9'; p++) {
    int32_t digit = (int32_t)(*p - (uint8_t)'0');
    have_digit = 1;
    /* spec allows max. 8 digits, but we allow values up to 1,000,000,000 */
    if (x >= (100 * 1000 * 1000)) {
      if (x != (100 * 1000 * 1000) || digit != 0) {
        *timeout = gpr_inf_future(GPR_TIMESPAN);
        return 1;
      }
    }
    x = x * 10 + digit;
  }
  if (!have_digit) return 0;
  /* skip whitespace */
  for (; p != end && *p == ' '; p++)
    ;
  if (p == end) return 0;
  /* decode unit specifier */
  switch (*p) {
    case 'n':
      *timeout = gpr_time_from_nanos(x, GPR_TIMESPAN);
      break;
    case 'u':
      *timeout = gpr_time_from_micros(x, GPR_TIMESPAN);
      break;
    case 'm':
      *timeout = gpr_time_from_millis(x, GPR_TIMESPAN);
      break;
    case 'S':
      *timeout = gpr_time_from_seconds(x, GPR_TIMESPAN);
      break;
    case 'M':
      *timeout = gpr_time_from_minutes(x, GPR_TIMESPAN);
      break;
    case 'H':
      *timeout = gpr_time_from_hours(x, GPR_TIMESPAN);
      break;
    default:
      return 0;
  }
  p++;
  return is_all_whitespace((const char *)p, (const char *)end);
}
