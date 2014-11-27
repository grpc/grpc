/*
 *
 * Copyright 2014, Google Inc.
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

#include "src/core/httpcli/format_request.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/slice.h>
#include <grpc/support/useful.h>

typedef struct {
  size_t length;
  size_t capacity;
  char *data;
} sbuf;

static void sbuf_append(sbuf *buf, const char *bytes, size_t len) {
  if (buf->length + len > buf->capacity) {
    buf->capacity = GPR_MAX(buf->length + len, buf->capacity * 3 / 2);
    buf->data = gpr_realloc(buf->data, buf->capacity);
  }
  memcpy(buf->data + buf->length, bytes, len);
  buf->length += len;
}

static void sbprintf(sbuf *buf, const char *fmt, ...) {
  char temp[GRPC_HTTPCLI_MAX_HEADER_LENGTH];
  size_t len;
  va_list args;

  va_start(args, fmt);
  len = vsprintf(temp, fmt, args);
  va_end(args);

  sbuf_append(buf, temp, len);
}

static void fill_common_header(const grpc_httpcli_request *request, sbuf *buf) {
  size_t i;
  sbprintf(buf, "%s HTTP/1.0\r\n", request->path);
  /* just in case some crazy server really expects HTTP/1.1 */
  sbprintf(buf, "Host: %s\r\n", request->host);
  sbprintf(buf, "Connection: close\r\n");
  sbprintf(buf, "User-Agent: %s\r\n", GRPC_HTTPCLI_USER_AGENT);
  /* user supplied headers */
  for (i = 0; i < request->hdr_count; i++) {
    sbprintf(buf, "%s: %s\r\n", request->hdrs[i].key, request->hdrs[i].value);
  }
}

gpr_slice grpc_httpcli_format_get_request(const grpc_httpcli_request *request) {
  sbuf out = {0, 0, NULL};

  sbprintf(&out, "GET ");
  fill_common_header(request, &out);
  sbprintf(&out, "\r\n");

  return gpr_slice_new(out.data, out.length, gpr_free);
}

gpr_slice grpc_httpcli_format_post_request(const grpc_httpcli_request *request,
                                           const char *body_bytes,
                                           size_t body_size) {
  sbuf out = {0, 0, NULL};
  size_t i;

  sbprintf(&out, "POST ");
  fill_common_header(request, &out);
  if (body_bytes) {
    gpr_uint8 has_content_type = 0;
    for (i = 0; i < request->hdr_count; i++) {
      if (strcmp(request->hdrs[i].key, "Content-Type") == 0) {
        has_content_type = 1;
        break;
      }
    }
    if (!has_content_type) {
      sbprintf(&out, "Content-Type: text/plain\r\n");
    }
    sbprintf(&out, "Content-Length: %lu\r\n", (unsigned long)body_size);
  }
  sbprintf(&out, "\r\n");
  if (body_bytes) {
    sbuf_append(&out, body_bytes, body_size);
  }

  return gpr_slice_new(out.data, out.length, gpr_free);
}
