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

#include "src/core/lib/http/parser.h"

#include <stdarg.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>
#include "test/core/util/slice_splitter.h"
#include "test/core/util/test_config.h"

static void test_request_succeeds(grpc_slice_split_mode split_mode,
                                  char *request, char *expect_method,
                                  grpc_http_version expect_version,
                                  char *expect_path, char *expect_body, ...) {
  grpc_http_parser parser;
  gpr_slice input_slice = gpr_slice_from_copied_string(request);
  size_t num_slices;
  size_t i;
  gpr_slice *slices;
  va_list args;

  grpc_split_slices(split_mode, &input_slice, 1, &slices, &num_slices);
  gpr_slice_unref(input_slice);

  grpc_http_parser_init(&parser);

  for (i = 0; i < num_slices; i++) {
    GPR_ASSERT(grpc_http_parser_parse(&parser, slices[i]));
    gpr_slice_unref(slices[i]);
  }
  GPR_ASSERT(grpc_http_parser_eof(&parser));

  GPR_ASSERT(GRPC_HTTP_REQUEST == parser.type);
  GPR_ASSERT(0 == strcmp(expect_method, parser.http.request.method));
  GPR_ASSERT(0 == strcmp(expect_path, parser.http.request.path));
  GPR_ASSERT(expect_version == parser.http.request.version);

  if (expect_body != NULL) {
    GPR_ASSERT(strlen(expect_body) == parser.http.request.body_length);
    GPR_ASSERT(0 == memcmp(expect_body, parser.http.request.body,
                           parser.http.request.body_length));
  } else {
    GPR_ASSERT(parser.http.request.body_length == 0);
  }

  va_start(args, expect_body);
  i = 0;
  for (;;) {
    char *expect_key;
    char *expect_value;
    expect_key = va_arg(args, char *);
    if (!expect_key) break;
    GPR_ASSERT(i < parser.http.request.hdr_count);
    expect_value = va_arg(args, char *);
    GPR_ASSERT(expect_value);
    GPR_ASSERT(0 == strcmp(expect_key, parser.http.request.hdrs[i].key));
    GPR_ASSERT(0 == strcmp(expect_value, parser.http.request.hdrs[i].value));
    i++;
  }
  va_end(args);
  GPR_ASSERT(i == parser.http.request.hdr_count);

  grpc_http_parser_destroy(&parser);
  gpr_free(slices);
}

static void test_succeeds(grpc_slice_split_mode split_mode, char *response,
                          int expect_status, char *expect_body, ...) {
  grpc_http_parser parser;
  gpr_slice input_slice = gpr_slice_from_copied_string(response);
  size_t num_slices;
  size_t i;
  gpr_slice *slices;
  va_list args;

  grpc_split_slices(split_mode, &input_slice, 1, &slices, &num_slices);
  gpr_slice_unref(input_slice);

  grpc_http_parser_init(&parser);

  for (i = 0; i < num_slices; i++) {
    GPR_ASSERT(grpc_http_parser_parse(&parser, slices[i]));
    gpr_slice_unref(slices[i]);
  }
  GPR_ASSERT(grpc_http_parser_eof(&parser));

  GPR_ASSERT(GRPC_HTTP_RESPONSE == parser.type);
  GPR_ASSERT(expect_status == parser.http.response.status);
  if (expect_body != NULL) {
    GPR_ASSERT(strlen(expect_body) == parser.http.response.body_length);
    GPR_ASSERT(0 == memcmp(expect_body, parser.http.response.body,
                           parser.http.response.body_length));
  } else {
    GPR_ASSERT(parser.http.response.body_length == 0);
  }

  va_start(args, expect_body);
  i = 0;
  for (;;) {
    char *expect_key;
    char *expect_value;
    expect_key = va_arg(args, char *);
    if (!expect_key) break;
    GPR_ASSERT(i < parser.http.response.hdr_count);
    expect_value = va_arg(args, char *);
    GPR_ASSERT(expect_value);
    GPR_ASSERT(0 == strcmp(expect_key, parser.http.response.hdrs[i].key));
    GPR_ASSERT(0 == strcmp(expect_value, parser.http.response.hdrs[i].value));
    i++;
  }
  va_end(args);
  GPR_ASSERT(i == parser.http.response.hdr_count);

  grpc_http_parser_destroy(&parser);
  gpr_free(slices);
}

static void test_fails(grpc_slice_split_mode split_mode, char *response) {
  grpc_http_parser parser;
  gpr_slice input_slice = gpr_slice_from_copied_string(response);
  size_t num_slices;
  size_t i;
  gpr_slice *slices;
  int done = 0;

  grpc_split_slices(split_mode, &input_slice, 1, &slices, &num_slices);
  gpr_slice_unref(input_slice);

  grpc_http_parser_init(&parser);

  for (i = 0; i < num_slices; i++) {
    if (!done && !grpc_http_parser_parse(&parser, slices[i])) {
      done = 1;
    }
    gpr_slice_unref(slices[i]);
  }
  if (!done && !grpc_http_parser_eof(&parser)) {
    done = 1;
  }
  GPR_ASSERT(done);

  grpc_http_parser_destroy(&parser);
  gpr_free(slices);
}

static const uint8_t failed_test1[] = {
    0x9e, 0x48, 0x54, 0x54, 0x50, 0x2f, 0x31, 0x2e, 0x30, 0x4a,
    0x48, 0x54, 0x54, 0x30, 0x32, 0x16, 0xa,  0x2f, 0x48, 0x20,
    0x31, 0x2e, 0x31, 0x20, 0x32, 0x30, 0x31, 0x54, 0x54, 0xb9,
    0x32, 0x31, 0x2e, 0x20, 0x32, 0x30, 0x20,
};

typedef struct {
  const char *name;
  const uint8_t *data;
  size_t length;
} failed_test;

#define FAILED_TEST(name) \
  { #name, name, sizeof(name) }

failed_test failed_tests[] = {
    FAILED_TEST(failed_test1),
};

static void test_doesnt_crash(failed_test t) {
  gpr_log(GPR_DEBUG, "Run previously failed test: %s", t.name);
  grpc_http_parser p;
  grpc_http_parser_init(&p);
  gpr_slice slice =
      gpr_slice_from_copied_buffer((const char *)t.data, t.length);
  grpc_http_parser_parse(&p, slice);
  gpr_slice_unref(slice);
  grpc_http_parser_destroy(&p);
}

int main(int argc, char **argv) {
  size_t i;
  const grpc_slice_split_mode split_modes[] = {GRPC_SLICE_SPLIT_IDENTITY,
                                               GRPC_SLICE_SPLIT_ONE_BYTE};
  char *tmp1, *tmp2;

  grpc_test_init(argc, argv);

  for (i = 0; i < GPR_ARRAY_SIZE(failed_tests); i++) {
    test_doesnt_crash(failed_tests[i]);
  }

  for (i = 0; i < GPR_ARRAY_SIZE(split_modes); i++) {
    test_succeeds(split_modes[i],
                  "HTTP/1.0 200 OK\r\n"
                  "xyz: abc\r\n"
                  "\r\n"
                  "hello world!",
                  200, "hello world!", "xyz", "abc", NULL);
    test_succeeds(split_modes[i],
                  "HTTP/1.0 404 Not Found\r\n"
                  "\r\n",
                  404, NULL, NULL);
    test_succeeds(split_modes[i],
                  "HTTP/1.1 200 OK\r\n"
                  "xyz: abc\r\n"
                  "\r\n"
                  "hello world!",
                  200, "hello world!", "xyz", "abc", NULL);
    test_succeeds(split_modes[i],
                  "HTTP/1.1 200 OK\n"
                  "\n"
                  "abc",
                  200, "abc", NULL);
    test_request_succeeds(split_modes[i],
                          "GET / HTTP/1.0\r\n"
                          "\r\n",
                          "GET", GRPC_HTTP_HTTP10, "/", NULL, NULL);
    test_request_succeeds(split_modes[i],
                          "GET / HTTP/1.0\r\n"
                          "\r\n"
                          "xyz",
                          "GET", GRPC_HTTP_HTTP10, "/", "xyz", NULL);
    test_request_succeeds(split_modes[i],
                          "GET / HTTP/1.1\r\n"
                          "\r\n"
                          "xyz",
                          "GET", GRPC_HTTP_HTTP11, "/", "xyz", NULL);
    test_request_succeeds(split_modes[i],
                          "GET / HTTP/2.0\r\n"
                          "\r\n"
                          "xyz",
                          "GET", GRPC_HTTP_HTTP20, "/", "xyz", NULL);
    test_request_succeeds(split_modes[i],
                          "GET / HTTP/1.0\r\n"
                          "xyz: abc\r\n"
                          "\r\n"
                          "xyz",
                          "GET", GRPC_HTTP_HTTP10, "/", "xyz", "xyz", "abc",
                          NULL);
    test_request_succeeds(split_modes[i],
                          "GET / HTTP/1.0\n"
                          "\n"
                          "xyz",
                          "GET", GRPC_HTTP_HTTP10, "/", "xyz", NULL);
    test_fails(split_modes[i], "HTTP/1.0\r\n");
    test_fails(split_modes[i], "HTTP/1.2\r\n");
    test_fails(split_modes[i], "HTTP/1.0 000 XYX\r\n");
    test_fails(split_modes[i], "HTTP/1.0 200 OK\n");
    test_fails(split_modes[i], "HTTP/1.0 200 OK\r\n");
    test_fails(split_modes[i], "HTTP/1.0 200 OK\r\nFoo x\r\n");
    test_fails(split_modes[i],
               "HTTP/1.0 200 OK\r\n"
               "xyz: abc\r\n"
               "  def\r\n"
               "\r\n"
               "hello world!");
    test_fails(split_modes[i], "GET\r\n");
    test_fails(split_modes[i], "GET /\r\n");
    test_fails(split_modes[i], "GET / HTTP/0.0\r\n");
    test_fails(split_modes[i], "GET / ____/1.0\r\n");
    test_fails(split_modes[i], "GET / HTTP/1.2\r\n");
    test_fails(split_modes[i], "GET / HTTP/1.0\n");

    tmp1 = gpr_malloc(2 * GRPC_HTTP_PARSER_MAX_HEADER_LENGTH);
    memset(tmp1, 'a', 2 * GRPC_HTTP_PARSER_MAX_HEADER_LENGTH - 1);
    tmp1[2 * GRPC_HTTP_PARSER_MAX_HEADER_LENGTH - 1] = 0;
    gpr_asprintf(&tmp2, "HTTP/1.0 200 OK\r\nxyz: %s\r\n\r\n", tmp1);
    test_fails(split_modes[i], tmp2);
    gpr_free(tmp1);
    gpr_free(tmp2);
  }

  return 0;
}
