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

#include "src/core/lib/http/format_request.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include "src/core/lib/gpr/string.h"

static void fill_common_header(const grpc_httpcli_request* request,
                               gpr_strvec* buf, bool connection_close) {
  size_t i;
  gpr_strvec_add(buf, gpr_strdup(request->http.path));
  gpr_strvec_add(buf, gpr_strdup(" HTTP/1.0\r\n"));
  /* just in case some crazy server really expects HTTP/1.1 */
  gpr_strvec_add(buf, gpr_strdup("Host: "));
  gpr_strvec_add(buf, gpr_strdup(request->host));
  gpr_strvec_add(buf, gpr_strdup("\r\n"));
  if (connection_close)
    gpr_strvec_add(buf, gpr_strdup("Connection: close\r\n"));
  gpr_strvec_add(buf,
                 gpr_strdup("User-Agent: " GRPC_HTTPCLI_USER_AGENT "\r\n"));
  /* user supplied headers */
  for (i = 0; i < request->http.hdr_count; i++) {
    gpr_strvec_add(buf, gpr_strdup(request->http.hdrs[i].key));
    gpr_strvec_add(buf, gpr_strdup(": "));
    gpr_strvec_add(buf, gpr_strdup(request->http.hdrs[i].value));
    gpr_strvec_add(buf, gpr_strdup("\r\n"));
  }
}

grpc_slice grpc_httpcli_format_get_request(
    const grpc_httpcli_request* request) {
  gpr_strvec out;
  char* flat;
  size_t flat_len;

  gpr_strvec_init(&out);
  gpr_strvec_add(&out, gpr_strdup("GET "));
  fill_common_header(request, &out, true);
  gpr_strvec_add(&out, gpr_strdup("\r\n"));

  flat = gpr_strvec_flatten(&out, &flat_len);
  gpr_strvec_destroy(&out);

  return grpc_slice_new(flat, flat_len, gpr_free);
}

grpc_slice grpc_httpcli_format_post_request(const grpc_httpcli_request* request,
                                            const char* body_bytes,
                                            size_t body_size) {
  gpr_strvec out;
  char* tmp;
  size_t out_len;
  size_t i;

  gpr_strvec_init(&out);

  gpr_strvec_add(&out, gpr_strdup("POST "));
  fill_common_header(request, &out, true);
  if (body_bytes) {
    uint8_t has_content_type = 0;
    for (i = 0; i < request->http.hdr_count; i++) {
      if (strcmp(request->http.hdrs[i].key, "Content-Type") == 0) {
        has_content_type = 1;
        break;
      }
    }
    if (!has_content_type) {
      gpr_strvec_add(&out, gpr_strdup("Content-Type: text/plain\r\n"));
    }
    gpr_asprintf(&tmp, "Content-Length: %lu\r\n",
                 static_cast<unsigned long>(body_size));
    gpr_strvec_add(&out, tmp);
  }
  gpr_strvec_add(&out, gpr_strdup("\r\n"));
  tmp = gpr_strvec_flatten(&out, &out_len);
  gpr_strvec_destroy(&out);

  if (body_bytes) {
    tmp = static_cast<char*>(gpr_realloc(tmp, out_len + body_size));
    memcpy(tmp + out_len, body_bytes, body_size);
    out_len += body_size;
  }

  return grpc_slice_new(tmp, out_len, gpr_free);
}

grpc_slice grpc_httpcli_format_connect_request(
    const grpc_httpcli_request* request) {
  gpr_strvec out;
  gpr_strvec_init(&out);
  gpr_strvec_add(&out, gpr_strdup("CONNECT "));
  fill_common_header(request, &out, false);
  gpr_strvec_add(&out, gpr_strdup("\r\n"));
  size_t flat_len;
  char* flat = gpr_strvec_flatten(&out, &flat_len);
  gpr_strvec_destroy(&out);
  return grpc_slice_new(flat, flat_len, gpr_free);
}
