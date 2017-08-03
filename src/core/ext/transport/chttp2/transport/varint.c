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

#include "src/core/ext/transport/chttp2/transport/varint.h"

uint32_t grpc_chttp2_hpack_varint_length(uint32_t tail_value) {
  if (tail_value < (1 << 7)) {
    return 2;
  } else if (tail_value < (1 << 14)) {
    return 3;
  } else if (tail_value < (1 << 21)) {
    return 4;
  } else if (tail_value < (1 << 28)) {
    return 5;
  } else {
    return 6;
  }
}

void grpc_chttp2_hpack_write_varint_tail(uint32_t tail_value, uint8_t* target,
                                         uint32_t tail_length) {
  switch (tail_length) {
    case 5:
      target[4] = (uint8_t)((tail_value >> 28) | 0x80);
    /* fallthrough */
    case 4:
      target[3] = (uint8_t)((tail_value >> 21) | 0x80);
    /* fallthrough */
    case 3:
      target[2] = (uint8_t)((tail_value >> 14) | 0x80);
    /* fallthrough */
    case 2:
      target[1] = (uint8_t)((tail_value >> 7) | 0x80);
    /* fallthrough */
    case 1:
      target[0] = (uint8_t)((tail_value) | 0x80);
  }
  target[tail_length - 1] &= 0x7f;
}
