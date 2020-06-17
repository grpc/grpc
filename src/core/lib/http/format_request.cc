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

#include "src/core/lib/http/format_request.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include "src/core/lib/gpr/string.h"

static void fill_common_header(const grpc_httpcli_request* request,
                               bool connection_close,
                               std::vector<std::string>* buf) {
  buf->push_back(request->http.path);
  buf->push_back(" HTTP/1.0\r\n");
  /* just in case some crazy server really expects HTTP/1.1 */
  buf->push_back("Host: ");
  buf->push_back(request->host);
  buf->push_back("\r\n");
  if (connection_close) buf->push_back("Connection: close\r\n");
  buf->push_back("User-Agent: " GRPC_HTTPCLI_USER_AGENT "\r\n");
  /* user supplied headers */
  for (size_t i = 0; i < request->http.hdr_count; i++) {
    buf->push_back(request->http.hdrs[i].key);
    buf->push_back(": ");
    buf->push_back(request->http.hdrs[i].value);
    buf->push_back("\r\n");
  }
}

grpc_slice grpc_httpcli_format_get_request(
    const grpc_httpcli_request* request) {
  std::vector<std::string> out;
  out.push_back("GET ");
  fill_common_header(request, true, &out);
  out.push_back("\r\n");
  std::string req = absl::StrJoin(out, "");
  return grpc_slice_from_copied_buffer(req.data(), req.size());
}

grpc_slice grpc_httpcli_format_post_request(const grpc_httpcli_request* request,
                                            const char* body_bytes,
                                            size_t body_size) {
  std::vector<std::string> out;
  out.push_back("POST ");
  fill_common_header(request, true, &out);
  if (body_bytes != nullptr) {
    bool has_content_type = false;
    for (size_t i = 0; i < request->http.hdr_count; i++) {
      if (strcmp(request->http.hdrs[i].key, "Content-Type") == 0) {
        has_content_type = true;
        break;
      }
    }
    if (!has_content_type) {
      out.push_back("Content-Type: text/plain\r\n");
    }
    out.push_back(absl::StrFormat("Content-Length: %lu\r\n",
                                  static_cast<unsigned long>(body_size)));
  }
  out.push_back("\r\n");
  std::string req = absl::StrJoin(out, "");
  if (body_bytes != nullptr) {
    absl::StrAppend(&req, absl::string_view(body_bytes, body_size));
  }
  return grpc_slice_from_copied_buffer(req.data(), req.size());
}

grpc_slice grpc_httpcli_format_connect_request(
    const grpc_httpcli_request* request) {
  std::vector<std::string> out;
  out.push_back("CONNECT ");
  fill_common_header(request, false, &out);
  out.push_back("\r\n");
  std::string req = absl::StrJoin(out, "");
  return grpc_slice_from_copied_buffer(req.data(), req.size());
}
