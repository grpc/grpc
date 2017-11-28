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

#include <stdint.h>
#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/lib/http/parser.h"

bool squelch = true;
bool leak_check = true;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  grpc_http_parser parser;
  grpc_http_response response;
  memset(&response, 0, sizeof(response));
  grpc_http_parser_init(&parser, GRPC_HTTP_RESPONSE, &response);
  grpc_slice slice = grpc_slice_from_copied_buffer((const char*)data, size);
  GRPC_ERROR_UNREF(grpc_http_parser_parse(&parser, slice, nullptr));
  GRPC_ERROR_UNREF(grpc_http_parser_eof(&parser));
  grpc_slice_unref(slice);
  grpc_http_parser_destroy(&parser);
  grpc_http_response_destroy(&response);
  return 0;
}
