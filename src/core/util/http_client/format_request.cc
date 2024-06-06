//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include "src/core/util/http_client/format_request.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

#include <grpc/slice.h>

#include "src/core/util/http_client/httpcli.h"

static void fill_common_header(const grpc_http_request* request,
                               const char* host, const char* path,
                               bool connection_close,
                               std::vector<std::string>* buf) {
  buf->push_back(path);
  buf->push_back(" HTTP/1.1\r\n");
  buf->push_back("Host: ");
  buf->push_back(host);
  buf->push_back("\r\n");
  if (connection_close) buf->push_back("Connection: close\r\n");
  buf->push_back("User-Agent: " GRPC_HTTPCLI_USER_AGENT "\r\n");
  // user supplied headers
  for (size_t i = 0; i < request->hdr_count; i++) {
    buf->push_back(request->hdrs[i].key);
    buf->push_back(": ");
    buf->push_back(request->hdrs[i].value);
    buf->push_back("\r\n");
  }
}

grpc_slice grpc_httpcli_format_get_request(const grpc_http_request* request,
                                           const char* host, const char* path) {
  std::vector<std::string> out;
  out.push_back("GET ");
  fill_common_header(request, host, path, true, &out);
  out.push_back("\r\n");
  std::string req = absl::StrJoin(out, "");
  return grpc_slice_from_copied_buffer(req.data(), req.size());
}

grpc_slice grpc_httpcli_format_post_request(const grpc_http_request* request,
                                            const char* host,
                                            const char* path) {
  std::vector<std::string> out;
  out.push_back("POST ");
  fill_common_header(request, host, path, true, &out);
  if (request->body != nullptr) {
    bool has_content_type = false;
    for (size_t i = 0; i < request->hdr_count; i++) {
      if (strcmp(request->hdrs[i].key, "Content-Type") == 0) {
        has_content_type = true;
        break;
      }
    }
    if (!has_content_type) {
      out.push_back("Content-Type: text/plain\r\n");
    }
    out.push_back(
        absl::StrFormat("Content-Length: %lu\r\n",
                        static_cast<unsigned long>(request->body_length)));
  }
  out.push_back("\r\n");
  std::string req = absl::StrJoin(out, "");
  if (request->body != nullptr) {
    absl::StrAppend(&req,
                    absl::string_view(request->body, request->body_length));
  }
  return grpc_slice_from_copied_buffer(req.data(), req.size());
}

grpc_slice grpc_httpcli_format_put_request(const grpc_http_request* request,
                                           const char* host, const char* path) {
  std::vector<std::string> out;
  out.push_back("PUT ");
  fill_common_header(request, host, path, true, &out);
  if (request->body != nullptr) {
    bool has_content_type = false;
    for (size_t i = 0; i < request->hdr_count; i++) {
      if (strcmp(request->hdrs[i].key, "Content-Type") == 0) {
        has_content_type = true;
        break;
      }
    }
    if (!has_content_type) {
      out.push_back("Content-Type: text/plain\r\n");
    }
    out.push_back(
        absl::StrFormat("Content-Length: %lu\r\n",
                        static_cast<unsigned long>(request->body_length)));
  }
  out.push_back("\r\n");
  std::string req = absl::StrJoin(out, "");
  if (request->body != nullptr) {
    absl::StrAppend(&req,
                    absl::string_view(request->body, request->body_length));
  }
  return grpc_slice_from_copied_buffer(req.data(), req.size());
}

grpc_slice grpc_httpcli_format_connect_request(const grpc_http_request* request,
                                               const char* host,
                                               const char* path) {
  std::vector<std::string> out;
  out.push_back("CONNECT ");
  fill_common_header(request, host, path, false, &out);
  out.push_back("\r\n");
  std::string req = absl::StrJoin(out, "");
  return grpc_slice_from_copied_buffer(req.data(), req.size());
}
