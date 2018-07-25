/*
 *
 * Copyright 2018 gRPC authors.
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

#include "test/core/util/fuzzer_util.h"

#include <grpc/support/alloc.h>

#include "src/core/lib/gpr/useful.h"

namespace grpc_core {
namespace testing {

uint8_t grpc_fuzzer_get_next_byte(input_stream* inp) {
  if (inp->cur == inp->end) {
    return 0;
  }
  return *inp->cur++;
}

char* grpc_fuzzer_get_next_string(input_stream* inp, bool* special) {
  char* str = nullptr;
  size_t cap = 0;
  size_t sz = 0;
  char c;
  do {
    if (cap == sz) {
      cap = GPR_MAX(3 * cap / 2, cap + 8);
      str = static_cast<char*>(gpr_realloc(str, cap));
    }
    c = static_cast<char>(grpc_fuzzer_get_next_byte(inp));
    str[sz++] = c;
  } while (c != 0 && c != 1);
  if (special != nullptr) {
    *special = (c == 1);
  }
  if (c == 1) {
    str[sz - 1] = 0;
  }
  return str;
}

uint32_t grpc_fuzzer_get_next_uint32(input_stream* inp) {
  uint8_t b = grpc_fuzzer_get_next_byte(inp);
  uint32_t x = b & 0x7f;
  if (b & 0x80) {
    x <<= 7;
    b = grpc_fuzzer_get_next_byte(inp);
    x |= b & 0x7f;
    if (b & 0x80) {
      x <<= 7;
      b = grpc_fuzzer_get_next_byte(inp);
      x |= b & 0x7f;
      if (b & 0x80) {
        x <<= 7;
        b = grpc_fuzzer_get_next_byte(inp);
        x |= b & 0x7f;
        if (b & 0x80) {
          x = (x << 4) | (grpc_fuzzer_get_next_byte(inp) & 0x0f);
        }
      }
    }
  }
  return x;
}

}  // namespace testing
}  // namespace grpc_core
