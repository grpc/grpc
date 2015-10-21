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

#include "src/core/transport/chttp2/timeout_encoding.h"

#include <stdio.h>
#include <string.h>

#include "src/core/support/string.h"

static int round_up(int x, int divisor) {
  return (x / divisor + (x % divisor != 0)) * divisor;
}

/* round an integer up to the next value with three significant figures */
static int round_up_to_three_sig_figs(int x) {
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

static void enc_ext(char *buffer, long value, char ext) {
  int n = gpr_ltoa(value, buffer);
  buffer[n] = ext;
  buffer[n + 1] = 0;
}

static void enc_seconds(char *buffer, long sec) {
  if (sec % 3600 == 0) {
    enc_ext(buffer, sec / 3600, 'H');
  } else if (sec % 60 == 0) {
    enc_ext(buffer, sec / 60, 'M');
  } else {
    enc_ext(buffer, sec, 'S');
  }
}

static void enc_nanos(char *buffer, int x) {
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

static void enc_micros(char *buffer, int x) {
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

void grpc_chttp2_encode_timeout(gpr_timespec timeout, char *buffer) {
  if (timeout.tv_sec < 0) {
    enc_tiny(buffer);
  } else if (timeout.tv_sec == 0) {
    enc_nanos(buffer, timeout.tv_nsec);
  } else if (timeout.tv_sec < 1000 && timeout.tv_nsec != 0) {
    enc_micros(buffer,
               (int)(timeout.tv_sec * 1000000) +
                   (timeout.tv_nsec / 1000 + (timeout.tv_nsec % 1000 != 0)));
  } else {
    enc_seconds(buffer, timeout.tv_sec + (timeout.tv_nsec != 0));
  }
}

static int is_all_whitespace(const char *p) {
  while (*p == ' ') p++;
  return *p == 0;
}

int grpc_chttp2_decode_timeout(const char *buffer, gpr_timespec *timeout) {
  gpr_uint32 x = 0;
  const gpr_uint8 *p = (const gpr_uint8 *)buffer;
  int have_digit = 0;
  /* skip whitespace */
  for (; *p == ' '; p++)
    ;
  /* decode numeric part */
  for (; *p >= '0' && *p <= '9'; p++) {
    gpr_uint32 xp = x * 10u + (gpr_uint32)*p - (gpr_uint32)'0';
    have_digit = 1;
    if (xp < x) {
      *timeout = gpr_inf_future(GPR_CLOCK_REALTIME);
      return 1;
    }
    x = xp;
  }
  if (!have_digit) return 0;
  /* skip whitespace */
  for (; *p == ' '; p++)
    ;
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
  return is_all_whitespace((const char *)p);
}
