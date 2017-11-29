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

#include <string.h>

#include <grpc/support/log.h>
#include "test/core/util/test_config.h"

static void test_format_get_request(void) {
  grpc_http_header hdr = {const_cast<char*>("x-yz"), const_cast<char*>("abc")};
  grpc_httpcli_request req;
  grpc_slice slice;

  memset(&req, 0, sizeof(req));
  req.host = const_cast<char*>("example.com");
  req.http.path = const_cast<char*>("/index.html");
  req.http.hdr_count = 1;
  req.http.hdrs = &hdr;

  slice = grpc_httpcli_format_get_request(&req);

  GPR_ASSERT(0 == grpc_slice_str_cmp(slice,
                                     "GET /index.html HTTP/1.0\r\n"
                                     "Host: example.com\r\n"
                                     "Connection: close\r\n"
                                     "User-Agent: " GRPC_HTTPCLI_USER_AGENT
                                     "\r\n"
                                     "x-yz: abc\r\n"
                                     "\r\n"));

  grpc_slice_unref(slice);
}

static void test_format_post_request(void) {
  grpc_http_header hdr = {const_cast<char*>("x-yz"), const_cast<char*>("abc")};
  grpc_httpcli_request req;
  grpc_slice slice;
  char body_bytes[] = "fake body";
  size_t body_len = 9;

  memset(&req, 0, sizeof(req));
  req.host = const_cast<char*>("example.com");
  req.http.path = const_cast<char*>("/index.html");
  req.http.hdr_count = 1;
  req.http.hdrs = &hdr;

  slice = grpc_httpcli_format_post_request(&req, body_bytes, body_len);

  GPR_ASSERT(0 == grpc_slice_str_cmp(slice,
                                     "POST /index.html HTTP/1.0\r\n"
                                     "Host: example.com\r\n"
                                     "Connection: close\r\n"
                                     "User-Agent: " GRPC_HTTPCLI_USER_AGENT
                                     "\r\n"
                                     "x-yz: abc\r\n"
                                     "Content-Type: text/plain\r\n"
                                     "Content-Length: 9\r\n"
                                     "\r\n"
                                     "fake body"));

  grpc_slice_unref(slice);
}

static void test_format_post_request_no_body(void) {
  grpc_http_header hdr = {const_cast<char*>("x-yz"), const_cast<char*>("abc")};
  grpc_httpcli_request req;
  grpc_slice slice;

  memset(&req, 0, sizeof(req));
  req.host = const_cast<char*>("example.com");
  req.http.path = const_cast<char*>("/index.html");
  req.http.hdr_count = 1;
  req.http.hdrs = &hdr;

  slice = grpc_httpcli_format_post_request(&req, nullptr, 0);

  GPR_ASSERT(0 == grpc_slice_str_cmp(slice,
                                     "POST /index.html HTTP/1.0\r\n"
                                     "Host: example.com\r\n"
                                     "Connection: close\r\n"
                                     "User-Agent: " GRPC_HTTPCLI_USER_AGENT
                                     "\r\n"
                                     "x-yz: abc\r\n"
                                     "\r\n"));

  grpc_slice_unref(slice);
}

static void test_format_post_request_content_type_override(void) {
  grpc_http_header hdrs[2];
  grpc_httpcli_request req;
  grpc_slice slice;
  char body_bytes[] = "fake%20body";
  size_t body_len = 11;

  hdrs[0].key = const_cast<char*>("x-yz");
  hdrs[0].value = const_cast<char*>("abc");
  hdrs[1].key = const_cast<char*>("Content-Type");
  hdrs[1].value = const_cast<char*>("application/x-www-form-urlencoded");
  memset(&req, 0, sizeof(req));
  req.host = const_cast<char*>("example.com");
  req.http.path = const_cast<char*>("/index.html");
  req.http.hdr_count = 2;
  req.http.hdrs = hdrs;

  slice = grpc_httpcli_format_post_request(&req, body_bytes, body_len);

  GPR_ASSERT(0 == grpc_slice_str_cmp(
                      slice,
                      "POST /index.html HTTP/1.0\r\n"
                      "Host: example.com\r\n"
                      "Connection: close\r\n"
                      "User-Agent: " GRPC_HTTPCLI_USER_AGENT "\r\n"
                      "x-yz: abc\r\n"
                      "Content-Type: application/x-www-form-urlencoded\r\n"
                      "Content-Length: 11\r\n"
                      "\r\n"
                      "fake%20body"));

  grpc_slice_unref(slice);
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);

  test_format_get_request();
  test_format_post_request();
  test_format_post_request_no_body();
  test_format_post_request_content_type_override();

  return 0;
}
