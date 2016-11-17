/*
 *
 * Copyright 2016, Google Inc.
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

#include <stdlib.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>

static int conforms_to(grpc_slice slice, const uint8_t *legal_bits) {
  const uint8_t *p = GRPC_SLICE_START_PTR(slice);
  const uint8_t *e = GRPC_SLICE_END_PTR(slice);
  for (; p != e; p++) {
    int idx = *p;
    int byte = idx / 8;
    int bit = idx % 8;
    if ((legal_bits[byte] & (1 << bit)) == 0) return 0;
  }
  return 1;
}

int grpc_header_key_is_legal(grpc_slice slice) {
  static const uint8_t legal_header_bits[256 / 8] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0xff, 0x03, 0x00, 0x00, 0x00,
      0x80, 0xfe, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  return GRPC_SLICE_LENGTH(slice) != 0 && conforms_to(slice, legal_header_bits);
}

int grpc_header_nonbin_value_is_legal(grpc_slice slice) {
  static const uint8_t legal_header_bits[256 / 8] = {
      0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  return conforms_to(slice, legal_header_bits);
}

int grpc_is_binary_header(grpc_slice slice) {
  if (GRPC_SLICE_LENGTH(slice) < 5) return 0;
  return 0 == memcmp(GRPC_SLICE_END_PTR(slice) - 4, "-bin", 4);
}
