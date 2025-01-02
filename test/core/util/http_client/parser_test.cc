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

#include "src/core/util/http_client/parser.h"

#include <grpc/support/alloc.h>
#include <stdarg.h>
#include <string.h>

#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "gtest/gtest.h"
#include "src/core/util/useful.h"
#include "test/core/test_util/slice_splitter.h"
#include "test/core/test_util/test_config.h"

static void test_request_succeeds(grpc_slice_split_mode split_mode,
                                  const char* request_text,
                                  const char* expect_method,
                                  grpc_http_version expect_version,
                                  const char* expect_path,
                                  const char* expect_body, ...) {
  grpc_http_parser parser;
  grpc_slice input_slice = grpc_slice_from_copied_string(request_text);
  size_t num_slices;
  size_t i;
  grpc_slice* slices;
  va_list args;
  grpc_http_request request;
  memset(&request, 0, sizeof(request));

  grpc_split_slices(split_mode, &input_slice, 1, &slices, &num_slices);
  grpc_slice_unref(input_slice);

  grpc_http_parser_init(&parser, GRPC_HTTP_REQUEST, &request);

  for (i = 0; i < num_slices; i++) {
    ASSERT_EQ(grpc_http_parser_parse(&parser, slices[i], nullptr),
              absl::OkStatus());
    grpc_slice_unref(slices[i]);
  }
  ASSERT_EQ(grpc_http_parser_eof(&parser), absl::OkStatus());

  ASSERT_EQ(GRPC_HTTP_REQUEST, parser.type);
  ASSERT_STREQ(expect_method, request.method);
  ASSERT_STREQ(expect_path, request.path);
  ASSERT_EQ(expect_version, request.version);

  if (expect_body != nullptr) {
    ASSERT_EQ(strlen(expect_body), request.body_length);
    ASSERT_EQ(0, memcmp(expect_body, request.body, request.body_length));
  } else {
    ASSERT_EQ(request.body_length, 0);
  }

  va_start(args, expect_body);
  i = 0;
  for (;;) {
    char* expect_key;
    char* expect_value;
    expect_key = va_arg(args, char*);
    if (!expect_key) break;
    ASSERT_LT(i, request.hdr_count);
    expect_value = va_arg(args, char*);
    ASSERT_TRUE(expect_value);
    ASSERT_STREQ(expect_key, request.hdrs[i].key);
    ASSERT_STREQ(expect_value, request.hdrs[i].value);
    i++;
  }
  va_end(args);
  ASSERT_EQ(i, request.hdr_count);

  grpc_http_request_destroy(&request);
  grpc_http_parser_destroy(&parser);
  gpr_free(slices);
}

static void test_succeeds(grpc_slice_split_mode split_mode,
                          const char* response_text, int expect_status,
                          const char* expect_body, ...) {
  grpc_http_parser parser;
  grpc_slice input_slice = grpc_slice_from_copied_string(response_text);
  size_t num_slices;
  size_t i;
  grpc_slice* slices;
  va_list args;
  grpc_http_response response;
  response = {};

  grpc_split_slices(split_mode, &input_slice, 1, &slices, &num_slices);
  grpc_slice_unref(input_slice);

  grpc_http_parser_init(&parser, GRPC_HTTP_RESPONSE, &response);

  for (i = 0; i < num_slices; i++) {
    ASSERT_EQ(grpc_http_parser_parse(&parser, slices[i], nullptr),
              absl::OkStatus());
    grpc_slice_unref(slices[i]);
  }
  ASSERT_EQ(grpc_http_parser_eof(&parser), absl::OkStatus());

  ASSERT_EQ(GRPC_HTTP_RESPONSE, parser.type);
  ASSERT_EQ(expect_status, response.status);
  if (expect_body != nullptr) {
    ASSERT_EQ(strlen(expect_body), response.body_length);
    ASSERT_EQ(0, memcmp(expect_body, response.body, response.body_length));
  } else {
    ASSERT_EQ(response.body_length, 0);
  }

  va_start(args, expect_body);
  i = 0;
  for (;;) {
    char* expect_key;
    char* expect_value;
    expect_key = va_arg(args, char*);
    if (!expect_key) break;
    ASSERT_LT(i, response.hdr_count);
    expect_value = va_arg(args, char*);
    ASSERT_TRUE(expect_value);
    ASSERT_STREQ(expect_key, response.hdrs[i].key);
    ASSERT_STREQ(expect_value, response.hdrs[i].value);
    i++;
  }
  va_end(args);
  ASSERT_EQ(i, response.hdr_count);

  grpc_http_response_destroy(&response);
  grpc_http_parser_destroy(&parser);
  gpr_free(slices);
}

static void test_fails(grpc_slice_split_mode split_mode,
                       const char* response_text) {
  grpc_http_parser parser;
  grpc_slice input_slice = grpc_slice_from_copied_string(response_text);
  size_t num_slices;
  size_t i;
  grpc_slice* slices;
  grpc_error_handle error;
  grpc_http_response response;
  response = {};

  grpc_split_slices(split_mode, &input_slice, 1, &slices, &num_slices);
  grpc_slice_unref(input_slice);

  grpc_http_parser_init(&parser, GRPC_HTTP_RESPONSE, &response);

  for (i = 0; i < num_slices; i++) {
    if (absl::OkStatus() == error) {
      error = grpc_http_parser_parse(&parser, slices[i], nullptr);
    }
    grpc_slice_unref(slices[i]);
  }
  if (absl::OkStatus() == error) {
    error = grpc_http_parser_eof(&parser);
  }
  ASSERT_FALSE(error.ok());

  grpc_http_response_destroy(&response);
  grpc_http_parser_destroy(&parser);
  gpr_free(slices);
}

static void test_request_fails(grpc_slice_split_mode split_mode,
                               const char* request_text) {
  grpc_http_parser parser;
  grpc_slice input_slice = grpc_slice_from_copied_string(request_text);
  size_t num_slices;
  size_t i;
  grpc_slice* slices;
  grpc_error_handle error;
  grpc_http_request request;
  memset(&request, 0, sizeof(request));

  grpc_split_slices(split_mode, &input_slice, 1, &slices, &num_slices);
  grpc_slice_unref(input_slice);

  grpc_http_parser_init(&parser, GRPC_HTTP_REQUEST, &request);

  for (i = 0; i < num_slices; i++) {
    if (error.ok()) {
      error = grpc_http_parser_parse(&parser, slices[i], nullptr);
    }
    grpc_slice_unref(slices[i]);
  }
  if (error.ok()) {
    error = grpc_http_parser_eof(&parser);
  }
  ASSERT_FALSE(error.ok());

  grpc_http_request_destroy(&request);
  grpc_http_parser_destroy(&parser);
  gpr_free(slices);
}

TEST(ParserTest, MainTest) {
  size_t i;
  const grpc_slice_split_mode split_modes[] = {GRPC_SLICE_SPLIT_IDENTITY,
                                               GRPC_SLICE_SPLIT_ONE_BYTE};
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
                  404, nullptr, NULL);
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
    test_succeeds(split_modes[i],
                  "HTTP/1.1 200 OK\r\n"
                  "Transfer-Encoding: chunked\r\n"
                  "\r\n"
                  "4\r\n"
                  "This\r\n"
                  "16;param1;param2\r\n"
                  " is a chunked encoding\r\n"
                  "1D\r\n"
                  " example.\r\nNo params handled.\r\n"
                  "0\r\n"
                  "\r\n",
                  200,
                  "This is a chunked encoding example.\r\nNo params handled.",
                  "Transfer-Encoding", "chunked", NULL);
    test_succeeds(split_modes[i],
                  "HTTP/1.1 200 OK\r\n"
                  "Transfer-Encoding: chunked\r\n"
                  "\r\n"
                  "e\r\n"
                  "HTTP Trailers \r\n"
                  "13\r\n"
                  "are also supported.\r\n"
                  "0\r\n"
                  "abc: xyz\r\n"
                  "\r\n",
                  200, "HTTP Trailers are also supported.", "Transfer-Encoding",
                  "chunked", "abc", "xyz", NULL);
    test_request_succeeds(split_modes[i],
                          "GET / HTTP/1.0\r\n"
                          "\r\n",
                          "GET", GRPC_HTTP_HTTP10, "/", nullptr, NULL);
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
    test_request_fails(split_modes[i], "GET\r\n");
    test_request_fails(split_modes[i], "GET /\r\n");
    test_request_fails(split_modes[i], "GET / HTTP/0.0\r\n");
    test_request_fails(split_modes[i], "GET / ____/1.0\r\n");
    test_request_fails(split_modes[i], "GET / HTTP/1.2\r\n");
    test_request_fails(split_modes[i], "GET / HTTP/1.0\n");

    char* tmp1 =
        static_cast<char*>(gpr_malloc(2 * GRPC_HTTP_PARSER_MAX_HEADER_LENGTH));
    memset(tmp1, 'a', (2 * GRPC_HTTP_PARSER_MAX_HEADER_LENGTH) - 1);
    tmp1[(2 * GRPC_HTTP_PARSER_MAX_HEADER_LENGTH) - 1] = 0;
    std::string tmp2 =
        absl::StrFormat("HTTP/1.0 200 OK\r\nxyz: %s\r\n\r\n", tmp1);
    gpr_free(tmp1);
    test_fails(split_modes[i], tmp2.c_str());
  }
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
