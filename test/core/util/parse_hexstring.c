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

#include "test/core/util/parse_hexstring.h"
#include <grpc/support/log.h>

grpc_slice parse_hexstring(const char *hexstring) {
  size_t nibbles = 0;
  const char *p = 0;
  uint8_t *out;
  uint8_t temp;
  grpc_slice slice;

  for (p = hexstring; *p; p++) {
    nibbles += (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f');
  }

  GPR_ASSERT((nibbles & 1) == 0);

  slice = grpc_slice_malloc(nibbles / 2);
  out = GRPC_SLICE_START_PTR(slice);

  nibbles = 0;
  temp = 0;
  for (p = hexstring; *p; p++) {
    if (*p >= '0' && *p <= '9') {
      temp = (uint8_t)(temp << 4) | (uint8_t)(*p - '0');
      nibbles++;
    } else if (*p >= 'a' && *p <= 'f') {
      temp = (uint8_t)(temp << 4) | (uint8_t)(*p - 'a' + 10);
      nibbles++;
    }
    if (nibbles == 2) {
      *out++ = temp;
      nibbles = 0;
    }
  }

  return slice;
}
