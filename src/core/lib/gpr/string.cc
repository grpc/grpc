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

#include "src/core/lib/gpr/string.h"

#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/useful.h"

char* gpr_strdup(const char* src) {
  char* dst;
  size_t len;

  if (!src) {
    return nullptr;
  }

  len = strlen(src) + 1;
  dst = static_cast<char*>(gpr_malloc(len));

  memcpy(dst, src, len);

  return dst;
}

typedef struct {
  size_t capacity;
  size_t length;
  char* data;
} dump_out;

char* gpr_format_timespec(gpr_timespec tm) {
  char time_buffer[35];
  char ns_buffer[11];  // '.' + 9 digits of precision
  struct tm* tm_info = localtime((const time_t*)&tm.tv_sec);
  strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%dT%H:%M:%S", tm_info);
  snprintf(ns_buffer, 11, ".%09d", tm.tv_nsec);
  // This loop trims off trailing zeros by inserting a null character that the
  // right point. We iterate in chunks of three because we want 0, 3, 6, or 9
  // fractional digits.
  for (int i = 7; i >= 1; i -= 3) {
    if (ns_buffer[i] == '0' && ns_buffer[i + 1] == '0' &&
        ns_buffer[i + 2] == '0') {
      ns_buffer[i] = '\0';
      // Edge case in which all fractional digits were 0.
      if (i == 1) {
        ns_buffer[0] = '\0';
      }
    } else {
      break;
    }
  }
  char* full_time_str;
  gpr_asprintf(&full_time_str, "%s%sZ", time_buffer, ns_buffer);
  return full_time_str;
}

static dump_out dump_out_create(void) {
  dump_out r = {0, 0, nullptr};
  return r;
}

static void dump_out_append(dump_out* out, char c) {
  if (out->length == out->capacity) {
    out->capacity = GPR_MAX(8, 2 * out->capacity);
    out->data = static_cast<char*>(gpr_realloc(out->data, out->capacity));
  }
  out->data[out->length++] = c;
}

static void hexdump(dump_out* out, const char* buf, size_t len) {
  static const char* hex = "0123456789abcdef";

  const uint8_t* const beg = reinterpret_cast<const uint8_t*>(buf);
  const uint8_t* const end = beg + len;
  const uint8_t* cur;

  for (cur = beg; cur != end; ++cur) {
    if (cur != beg) dump_out_append(out, ' ');
    dump_out_append(out, hex[*cur >> 4]);
    dump_out_append(out, hex[*cur & 0xf]);
  }
}

static void asciidump(dump_out* out, const char* buf, size_t len) {
  const uint8_t* const beg = reinterpret_cast<const uint8_t*>(buf);
  const uint8_t* const end = beg + len;
  const uint8_t* cur;
  int out_was_empty = (out->length == 0);
  if (!out_was_empty) {
    dump_out_append(out, ' ');
    dump_out_append(out, '\'');
  }
  for (cur = beg; cur != end; ++cur) {
    dump_out_append(out, (isprint(*cur) ? *(char*)cur : '.'));
  }
  if (!out_was_empty) {
    dump_out_append(out, '\'');
  }
}

char* gpr_dump(const char* buf, size_t len, uint32_t flags) {
  dump_out out = dump_out_create();
  if (flags & GPR_DUMP_HEX) {
    hexdump(&out, buf, len);
  }
  if (flags & GPR_DUMP_ASCII) {
    asciidump(&out, buf, len);
  }
  dump_out_append(&out, 0);
  return out.data;
}

int gpr_parse_bytes_to_uint32(const char* buf, size_t len, uint32_t* result) {
  uint32_t out = 0;
  uint32_t new_val;
  size_t i;

  if (len == 0) return 0; /* must have some bytes */

  for (i = 0; i < len; i++) {
    if (buf[i] < '0' || buf[i] > '9') return 0; /* bad char */
    new_val = 10 * out + static_cast<uint32_t>(buf[i] - '0');
    if (new_val < out) return 0; /* overflow */
    out = new_val;
  }

  *result = out;
  return 1;
}

void gpr_reverse_bytes(char* str, int len) {
  char *p1, *p2;
  for (p1 = str, p2 = str + len - 1; p2 > p1; ++p1, --p2) {
    char temp = *p1;
    *p1 = *p2;
    *p2 = temp;
  }
}

int gpr_ltoa(long value, char* string) {
  long sign;
  int i = 0;

  if (value == 0) {
    string[0] = '0';
    string[1] = 0;
    return 1;
  }

  sign = value < 0 ? -1 : 1;
  while (value) {
    string[i++] = static_cast<char>('0' + sign * (value % 10));
    value /= 10;
  }
  if (sign < 0) string[i++] = '-';
  gpr_reverse_bytes(string, i);
  string[i] = 0;
  return i;
}

int int64_ttoa(int64_t value, char* string) {
  int64_t sign;
  int i = 0;

  if (value == 0) {
    string[0] = '0';
    string[1] = 0;
    return 1;
  }

  sign = value < 0 ? -1 : 1;
  while (value) {
    string[i++] = static_cast<char>('0' + sign * (value % 10));
    value /= 10;
  }
  if (sign < 0) string[i++] = '-';
  gpr_reverse_bytes(string, i);
  string[i] = 0;
  return i;
}

int gpr_parse_nonnegative_int(const char* value) {
  char* end;
  long result = strtol(value, &end, 0);
  if (*end != '\0' || result < 0 || result > INT_MAX) return -1;
  return static_cast<int>(result);
}

char* gpr_leftpad(const char* str, char flag, size_t length) {
  const size_t str_length = strlen(str);
  const size_t out_length = str_length > length ? str_length : length;
  char* out = static_cast<char*>(gpr_malloc(out_length + 1));
  memset(out, flag, out_length - str_length);
  memcpy(out + out_length - str_length, str, str_length);
  out[out_length] = 0;
  return out;
}

char* gpr_strjoin(const char** strs, size_t nstrs, size_t* final_length) {
  return gpr_strjoin_sep(strs, nstrs, "", final_length);
}

char* gpr_strjoin_sep(const char** strs, size_t nstrs, const char* sep,
                      size_t* final_length) {
  const size_t sep_len = strlen(sep);
  size_t out_length = 0;
  size_t i;
  char* out;
  for (i = 0; i < nstrs; i++) {
    out_length += strlen(strs[i]);
  }
  out_length += 1; /* null terminator */
  if (nstrs > 0) {
    out_length += sep_len * (nstrs - 1); /* separators */
  }
  out = static_cast<char*>(gpr_malloc(out_length));
  out_length = 0;
  for (i = 0; i < nstrs; i++) {
    const size_t slen = strlen(strs[i]);
    if (i != 0) {
      memcpy(out + out_length, sep, sep_len);
      out_length += sep_len;
    }
    memcpy(out + out_length, strs[i], slen);
    out_length += slen;
  }
  out[out_length] = 0;
  if (final_length != nullptr) {
    *final_length = out_length;
  }
  return out;
}

void gpr_strvec_init(gpr_strvec* sv) { memset(sv, 0, sizeof(*sv)); }

void gpr_strvec_destroy(gpr_strvec* sv) {
  size_t i;
  for (i = 0; i < sv->count; i++) {
    gpr_free(sv->strs[i]);
  }
  gpr_free(sv->strs);
}

void gpr_strvec_add(gpr_strvec* sv, char* str) {
  if (sv->count == sv->capacity) {
    sv->capacity = GPR_MAX(sv->capacity + 8, sv->capacity * 2);
    sv->strs = static_cast<char**>(
        gpr_realloc(sv->strs, sizeof(char*) * sv->capacity));
  }
  sv->strs[sv->count++] = str;
}

char* gpr_strvec_flatten(gpr_strvec* sv, size_t* final_length) {
  return gpr_strjoin((const char**)sv->strs, sv->count, final_length);
}

int gpr_stricmp(const char* a, const char* b) {
  int ca, cb;
  do {
    ca = tolower(*a);
    cb = tolower(*b);
    ++a;
    ++b;
  } while (ca == cb && ca && cb);
  return ca - cb;
}

static void add_string_to_split(const char* beg, const char* end, char*** strs,
                                size_t* nstrs, size_t* capstrs) {
  char* out =
      static_cast<char*>(gpr_malloc(static_cast<size_t>(end - beg) + 1));
  memcpy(out, beg, static_cast<size_t>(end - beg));
  out[end - beg] = 0;
  if (*nstrs == *capstrs) {
    *capstrs = GPR_MAX(8, 2 * *capstrs);
    *strs = static_cast<char**>(gpr_realloc(*strs, sizeof(*strs) * *capstrs));
  }
  (*strs)[*nstrs] = out;
  ++*nstrs;
}

void gpr_string_split(const char* input, const char* sep, char*** strs,
                      size_t* nstrs) {
  const char* next;
  *strs = nullptr;
  *nstrs = 0;
  size_t capstrs = 0;
  while ((next = strstr(input, sep))) {
    add_string_to_split(input, next, strs, nstrs, &capstrs);
    input = next + strlen(sep);
  }
  add_string_to_split(input, input + strlen(input), strs, nstrs, &capstrs);
}

void* gpr_memrchr(const void* s, int c, size_t n) {
  if (s == nullptr) return nullptr;
  char* b = (char*)s;
  size_t i;
  for (i = 0; i < n; i++) {
    if (b[n - i - 1] == c) {
      return &b[n - i - 1];
    }
  }
  return nullptr;
}

bool gpr_is_true(const char* s) {
  size_t i;
  if (s == nullptr) {
    return false;
  }
  static const char* truthy[] = {"yes", "true", "1"};
  for (i = 0; i < GPR_ARRAY_SIZE(truthy); i++) {
    if (0 == gpr_stricmp(s, truthy[i])) {
      return true;
    }
  }
  return false;
}
