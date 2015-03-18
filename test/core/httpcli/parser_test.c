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

#include "src/core/httpcli/parser.h"

#include <stdarg.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>
#include "test/core/util/slice_splitter.h"
#include "test/core/util/test_config.h"

static void test_succeeds(grpc_slice_split_mode split_mode, char *response,
                          int expect_status, char *expect_body, ...) {
  grpc_httpcli_parser parser;
  gpr_slice input_slice = gpr_slice_from_copied_string(response);
  size_t num_slices;
  size_t i;
  gpr_slice *slices;
  va_list args;

  grpc_split_slices(split_mode, &input_slice, 1, &slices, &num_slices);
  gpr_slice_unref(input_slice);

  grpc_httpcli_parser_init(&parser);

  for (i = 0; i < num_slices; i++) {
    GPR_ASSERT(grpc_httpcli_parser_parse(&parser, slices[i]));
    gpr_slice_unref(slices[i]);
  }
  GPR_ASSERT(grpc_httpcli_parser_eof(&parser));

  GPR_ASSERT(expect_status == parser.r.status);
  if (expect_body != NULL) {
    GPR_ASSERT(strlen(expect_body) == parser.r.body_length);
    GPR_ASSERT(0 == memcmp(expect_body, parser.r.body, parser.r.body_length));
  } else {
    GPR_ASSERT(parser.r.body_length == 0);
  }

  va_start(args, expect_body);
  i = 0;
  for (;;) {
    char *expect_key;
    char *expect_value;
    expect_key = va_arg(args, char *);
    if (!expect_key) break;
    GPR_ASSERT(i < parser.r.hdr_count);
    expect_value = va_arg(args, char *);
    GPR_ASSERT(expect_value);
    GPR_ASSERT(0 == strcmp(expect_key, parser.r.hdrs[i].key));
    GPR_ASSERT(0 == strcmp(expect_value, parser.r.hdrs[i].value));
    i++;
  }
  va_end(args);
  GPR_ASSERT(i == parser.r.hdr_count);

  grpc_httpcli_parser_destroy(&parser);
  gpr_free(slices);
}

static void test_fails(grpc_slice_split_mode split_mode, char *response) {
  grpc_httpcli_parser parser;
  gpr_slice input_slice = gpr_slice_from_copied_string(response);
  size_t num_slices;
  size_t i;
  gpr_slice *slices;
  int done = 0;

  grpc_split_slices(split_mode, &input_slice, 1, &slices, &num_slices);
  gpr_slice_unref(input_slice);

  grpc_httpcli_parser_init(&parser);

  for (i = 0; i < num_slices; i++) {
    if (!done && !grpc_httpcli_parser_parse(&parser, slices[i])) {
      done = 1;
    }
    gpr_slice_unref(slices[i]);
  }
  if (!done && !grpc_httpcli_parser_eof(&parser)) {
    done = 1;
  }
  GPR_ASSERT(done);

  grpc_httpcli_parser_destroy(&parser);
  gpr_free(slices);
}

int main(int argc, char **argv) {
  size_t i;
  const grpc_slice_split_mode split_modes[] = {GRPC_SLICE_SPLIT_IDENTITY,
                                               GRPC_SLICE_SPLIT_ONE_BYTE};

  grpc_test_init(argc, argv);

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
    test_fails(split_modes[i], "HTTP/1.0\r\n");
    test_fails(split_modes[i], "HTTP/1.2\r\n");
    test_fails(split_modes[i], "HTTP/1.0 000 XYX\r\n");
    test_fails(split_modes[i], "HTTP/1.0 200 OK\n");
    test_fails(split_modes[i], "HTTP/1.0 200 OK\r\n");
    test_fails(split_modes[i], "HTTP/1.0 200 OK\r\nFoo x\r\n");
  }

  return 0;
}
