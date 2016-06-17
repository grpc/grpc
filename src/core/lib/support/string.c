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

#include "src/core/lib/support/string.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/useful.h>

char *gpr_strdup(const char *src) {
  char *dst;
  size_t len;

  if (!src) {
    return NULL;
  }

  len = strlen(src) + 1;
  dst = gpr_malloc(len);

  memcpy(dst, src, len);

  return dst;
}

typedef struct {
  size_t capacity;
  size_t length;
  char *data;
} dump_out;

static dump_out dump_out_create(void) {
  dump_out r = {0, 0, NULL};
  return r;
}

static void dump_out_append(dump_out *out, char c) {
  if (out->length == out->capacity) {
    out->capacity = GPR_MAX(8, 2 * out->capacity);
    out->data = gpr_realloc(out->data, out->capacity);
  }
  out->data[out->length++] = c;
}

static void hexdump(dump_out *out, const char *buf, size_t len) {
  static const char hex[16] = "0123456789abcdef";

  const uint8_t *const beg = (const uint8_t *)buf;
  const uint8_t *const end = beg + len;
  const uint8_t *cur;

  for (cur = beg; cur != end; ++cur) {
    if (cur != beg) dump_out_append(out, ' ');
    dump_out_append(out, hex[*cur >> 4]);
    dump_out_append(out, hex[*cur & 0xf]);
  }
}

static void asciidump(dump_out *out, const char *buf, size_t len) {
  const uint8_t *const beg = (const uint8_t *)buf;
  const uint8_t *const end = beg + len;
  const uint8_t *cur;
  int out_was_empty = (out->length == 0);
  if (!out_was_empty) {
    dump_out_append(out, ' ');
    dump_out_append(out, '\'');
  }
  for (cur = beg; cur != end; ++cur) {
    dump_out_append(out, (char)(isprint(*cur) ? *(char *)cur : '.'));
  }
  if (!out_was_empty) {
    dump_out_append(out, '\'');
  }
}

char *gpr_dump(const char *buf, size_t len, uint32_t flags) {
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

char *gpr_dump_slice(gpr_slice s, uint32_t flags) {
  return gpr_dump((const char *)GPR_SLICE_START_PTR(s), GPR_SLICE_LENGTH(s),
                  flags);
}

int gpr_parse_bytes_to_uint32(const char *buf, size_t len, uint32_t *result) {
  uint32_t out = 0;
  uint32_t new;
  size_t i;

  if (len == 0) return 0; /* must have some bytes */

  for (i = 0; i < len; i++) {
    if (buf[i] < '0' || buf[i] > '9') return 0; /* bad char */
    new = 10 * out + (uint32_t)(buf[i] - '0');
    if (new < out) return 0; /* overflow */
    out = new;
  }

  *result = out;
  return 1;
}

void gpr_reverse_bytes(char *str, int len) {
  char *p1, *p2;
  for (p1 = str, p2 = str + len - 1; p2 > p1; ++p1, --p2) {
    char temp = *p1;
    *p1 = *p2;
    *p2 = temp;
  }
}

int gpr_ltoa(long value, char *string) {
  long sign;
  int i = 0;

  if (value == 0) {
    string[0] = '0';
    string[1] = 0;
    return 1;
  }

  sign = value < 0 ? -1 : 1;
  while (value) {
    string[i++] = (char)('0' + sign * (value % 10));
    value /= 10;
  }
  if (sign < 0) string[i++] = '-';
  gpr_reverse_bytes(string, i);
  string[i] = 0;
  return i;
}

int int64_ttoa(int64_t value, char *string) {
  int64_t sign;
  int i = 0;

  if (value == 0) {
    string[0] = '0';
    string[1] = 0;
    return 1;
  }

  sign = value < 0 ? -1 : 1;
  while (value) {
    string[i++] = (char)('0' + sign * (value % 10));
    value /= 10;
  }
  if (sign < 0) string[i++] = '-';
  gpr_reverse_bytes(string, i);
  string[i] = 0;
  return i;
}

char *gpr_leftpad(const char *str, char flag, size_t length) {
  const size_t str_length = strlen(str);
  const size_t out_length = str_length > length ? str_length : length;
  char *out = gpr_malloc(out_length + 1);
  memset(out, flag, out_length - str_length);
  memcpy(out + out_length - str_length, str, str_length);
  out[out_length] = 0;
  return out;
}

char *gpr_strjoin(const char **strs, size_t nstrs, size_t *final_length) {
  return gpr_strjoin_sep(strs, nstrs, "", final_length);
}

char *gpr_strjoin_sep(const char **strs, size_t nstrs, const char *sep,
                      size_t *final_length) {
  const size_t sep_len = strlen(sep);
  size_t out_length = 0;
  size_t i;
  char *out;
  for (i = 0; i < nstrs; i++) {
    out_length += strlen(strs[i]);
  }
  out_length += 1; /* null terminator */
  if (nstrs > 0) {
    out_length += sep_len * (nstrs - 1); /* separators */
  }
  out = gpr_malloc(out_length);
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
  if (final_length != NULL) {
    *final_length = out_length;
  }
  return out;
}

/** Finds the initial (\a begin) and final (\a end) offsets of the next
 * substring from \a str + \a read_offset until the next \a sep or the end of \a
 * str.
 *
 * Returns 1 and updates \a begin and \a end. Returns 0 otherwise. */
static int slice_find_separator_offset(const gpr_slice str, const char *sep,
                                       const size_t read_offset, size_t *begin,
                                       size_t *end) {
  size_t i;
  const uint8_t *str_ptr = GPR_SLICE_START_PTR(str) + read_offset;
  const size_t str_len = GPR_SLICE_LENGTH(str) - read_offset;
  const size_t sep_len = strlen(sep);
  if (str_len < sep_len) {
    return 0;
  }

  for (i = 0; i <= str_len - sep_len; i++) {
    if (memcmp(str_ptr + i, sep, sep_len) == 0) {
      *begin = read_offset;
      *end = read_offset + i;
      return 1;
    }
  }
  return 0;
}

void gpr_slice_split(gpr_slice str, const char *sep, gpr_slice_buffer *dst) {
  const size_t sep_len = strlen(sep);
  size_t begin, end;

  GPR_ASSERT(sep_len > 0);

  if (slice_find_separator_offset(str, sep, 0, &begin, &end) != 0) {
    do {
      gpr_slice_buffer_add_indexed(dst, gpr_slice_sub(str, begin, end));
    } while (slice_find_separator_offset(str, sep, end + sep_len, &begin,
                                         &end) != 0);
    gpr_slice_buffer_add_indexed(
        dst, gpr_slice_sub(str, end + sep_len, GPR_SLICE_LENGTH(str)));
  } else { /* no sep found, add whole input */
    gpr_slice_buffer_add_indexed(dst, gpr_slice_ref(str));
  }
}

void gpr_strvec_init(gpr_strvec *sv) { memset(sv, 0, sizeof(*sv)); }

void gpr_strvec_destroy(gpr_strvec *sv) {
  size_t i;
  for (i = 0; i < sv->count; i++) {
    gpr_free(sv->strs[i]);
  }
  gpr_free(sv->strs);
}

void gpr_strvec_add(gpr_strvec *sv, char *str) {
  if (sv->count == sv->capacity) {
    sv->capacity = GPR_MAX(sv->capacity + 8, sv->capacity * 2);
    sv->strs = gpr_realloc(sv->strs, sizeof(char *) * sv->capacity);
  }
  sv->strs[sv->count++] = str;
}

char *gpr_strvec_flatten(gpr_strvec *sv, size_t *final_length) {
  return gpr_strjoin((const char **)sv->strs, sv->count, final_length);
}
