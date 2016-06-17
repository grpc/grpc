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
    case 4:
      target[3] = (uint8_t)((tail_value >> 21) | 0x80);
    case 3:
      target[2] = (uint8_t)((tail_value >> 14) | 0x80);
    case 2:
      target[1] = (uint8_t)((tail_value >> 7) | 0x80);
    case 1:
      target[0] = (uint8_t)((tail_value) | 0x80);
  }
  target[tail_length - 1] &= 0x7f;
}
