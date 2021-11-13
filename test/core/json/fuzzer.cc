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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/json/json.h"

bool squelch = true;
bool leak_check = true;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto json = grpc_core::Json::Parse(
      absl::string_view(reinterpret_cast<const char*>(data), size), &error);
  if (error == GRPC_ERROR_NONE) {
    auto text2 = json.Dump();
    auto json2 = grpc_core::Json::Parse(text2, &error);
    printf("%s --> %s\n", text2.c_str(), grpc_error_string(error));
    printf("json1: %s\n", json.Dump().c_str());
    printf("json2: %s\n", json2.Dump().c_str());
    GPR_ASSERT(error == GRPC_ERROR_NONE);
    GPR_ASSERT(json == json2);
  }
  GRPC_ERROR_UNREF(error);
  return 0;
}
