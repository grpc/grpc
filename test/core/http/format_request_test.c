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

#include "src/core/lib/http/format_request.h"

#include <string.h>

#include <grpc/support/log.h>
#include "test/core/util/test_config.h"

static void test_format_get_request(void) {
  grpc_http_header hdr = {"x-yz", "abc"};
  grpc_httpcli_request req;
  grpc_slice slice;

  memset(&req, 0, sizeof(req));
  req.host = "example.com";
  req.http.path = "/index.html";
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
  grpc_http_header hdr = {"x-yz", "abc"};
  grpc_httpcli_request req;
  grpc_slice slice;
  char body_bytes[] = "fake body";
  size_t body_len = 9;

  memset(&req, 0, sizeof(req));
  req.host = "example.com";
  req.http.path = "/index.html";
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
  grpc_http_header hdr = {"x-yz", "abc"};
  grpc_httpcli_request req;
  grpc_slice slice;

  memset(&req, 0, sizeof(req));
  req.host = "example.com";
  req.http.path = "/index.html";
  req.http.hdr_count = 1;
  req.http.hdrs = &hdr;

  slice = grpc_httpcli_format_post_request(&req, NULL, 0);

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

  hdrs[0].key = "x-yz";
  hdrs[0].value = "abc";
  hdrs[1].key = "Content-Type";
  hdrs[1].value = "application/x-www-form-urlencoded";
  memset(&req, 0, sizeof(req));
  req.host = "example.com";
  req.http.path = "/index.html";
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

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);

  test_format_get_request();
  test_format_post_request();
  test_format_post_request_no_body();
  test_format_post_request_content_type_override();

  return 0;
}
