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

#include "src/core/lib/transport/timeout_encoding.h"

#include <stdio.h>
#include <string.h>

#include "src/core/lib/gpr/string.h"

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
static void enc_tiny(char* buffer) { memcpy(buffer, "1n", 3); }

/* encode our maximum timeout value, about 1157 days */
static void enc_huge(char* buffer) { memcpy(buffer, "99999999S", 10); }

static void enc_ext(char* buffer, int64_t value, char ext) {
  int n = int64_ttoa(value, buffer);
  buffer[n] = ext;
  buffer[n + 1] = 0;
}

static void enc_seconds(char* buffer, int64_t sec) {
  sec = round_up_to_three_sig_figs(sec);
  if (sec % 3600 == 0) {
    enc_ext(buffer, sec / 3600, 'H');
  } else if (sec % 60 == 0) {
    enc_ext(buffer, sec / 60, 'M');
  } else {
    enc_ext(buffer, sec, 'S');
  }
}

static void enc_millis(char* buffer, int64_t x) {
  x = round_up_to_three_sig_figs(x);
  if (x < GPR_MS_PER_SEC) {
    enc_ext(buffer, x, 'm');
  } else {
    if (x % GPR_MS_PER_SEC == 0) {
      enc_seconds(buffer, x / GPR_MS_PER_SEC);
    } else {
      enc_ext(buffer, x, 'm');
    }
  }
}

void grpc_http2_encode_timeout(grpc_millis timeout, char* buffer) {
  const grpc_millis kMaxTimeout = 99999999000;
  if (timeout <= 0) {
    enc_tiny(buffer);
  } else if (timeout < 1000 * GPR_MS_PER_SEC) {
    enc_millis(buffer, timeout);
  } else if (timeout >= kMaxTimeout) {
    enc_huge(buffer);
  } else {
    enc_seconds(buffer,
                timeout / GPR_MS_PER_SEC + (timeout % GPR_MS_PER_SEC != 0));
  }
}

static int is_all_whitespace(const char* p, const char* end) {
  while (p != end && *p == ' ') p++;
  return p == end;
}

int grpc_http2_decode_timeout(const grpc_slice& text, grpc_millis* timeout) {
  grpc_millis x = 0;
  const uint8_t* p = GRPC_SLICE_START_PTR(text);
  const uint8_t* end = GRPC_SLICE_END_PTR(text);
  int have_digit = 0;
  /* skip whitespace */
  for (; p != end && *p == ' '; p++)
    ;
  /* decode numeric part */
  for (; p != end && *p >= '0' && *p <= '9'; p++) {
    int32_t digit = static_cast<int32_t>(*p - static_cast<uint8_t>('0'));
    have_digit = 1;
    /* spec allows max. 8 digits, but we allow values up to 1,000,000,000 */
    if (x >= (100 * 1000 * 1000)) {
      if (x != (100 * 1000 * 1000) || digit != 0) {
        *timeout = GRPC_MILLIS_INF_FUTURE;
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
      *timeout = x / GPR_NS_PER_MS + (x % GPR_NS_PER_MS != 0);
      break;
    case 'u':
      *timeout = x / GPR_US_PER_MS + (x % GPR_US_PER_MS != 0);
      break;
    case 'm':
      *timeout = x;
      break;
    case 'S':
      *timeout = x * GPR_MS_PER_SEC;
      break;
    case 'M':
      *timeout = x * 60 * GPR_MS_PER_SEC;
      break;
    case 'H':
      *timeout = x * 60 * 60 * GPR_MS_PER_SEC;
      break;
    default:
      return 0;
  }
  p++;
  return is_all_whitespace(reinterpret_cast<const char*>(p),
                           reinterpret_cast<const char*>(end));
}
