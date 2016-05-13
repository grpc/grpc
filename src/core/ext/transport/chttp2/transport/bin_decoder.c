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

#include "src/core/ext/transport/chttp2/transport/bin_decoder.h"
#include <grpc/support/log.h>
#include <stdio.h>

static uint8_t decode_table[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  62, 0,  0,  0,  63, 52, 53, 54, 55, 56, 57, 58, 59, 60,
    61, 0,  0,  0,  0,  0,  0,  0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 0,  0,  0,  0,
    0,  0,  26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42,
    43, 44, 45, 46, 47, 48, 49, 50, 51, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0};

static const uint8_t tail_xtra[4] = {0, 0, 1, 2};

gpr_slice grpc_chttp2_base64_decode(gpr_slice input) {
  size_t input_length = GPR_SLICE_LENGTH(input);
  GPR_ASSERT(input_length % 4 == 0);
  size_t output_length = input_length / 4 * 3;

  if (input_length > 0) {
    uint8_t *input_end = GPR_SLICE_END_PTR(input);
    if (*(--input_end) == '=') {
      output_length--;
      if (*(--input_end) == '=') {
        output_length--;
      }
    }
  }

  gpr_log(GPR_ERROR, "input_length: %d, output_length: %d\n", input_length,
          output_length);

  return grpc_chttp2_base64_decode_with_length(input, output_length);
}

gpr_slice grpc_chttp2_base64_decode_with_length(gpr_slice input,
                                                size_t output_length) {
  size_t input_length = GPR_SLICE_LENGTH(input);
  // The length of a base64 string cannot be 4 * n + 1
  GPR_ASSERT(input_length % 4 != 1);
  GPR_ASSERT(output_length <=
             input_length / 4 * 3 + tail_xtra[input_length % 4]);
  size_t output_triplets = output_length / 3;
  size_t tail_case = output_length % 3;
  gpr_slice output = gpr_slice_malloc(output_length);
  uint8_t *in = GPR_SLICE_START_PTR(input);
  uint8_t *out = GPR_SLICE_START_PTR(output);
  size_t i;

  for (i = 0; i < output_triplets; i++) {
    out[0] = (uint8_t)((decode_table[in[0]] << 2) | (decode_table[in[1]] >> 4));
    out[1] = (uint8_t)((decode_table[in[1]] << 4) | (decode_table[in[2]] >> 2));
    out[2] = (uint8_t)((decode_table[in[2]] << 6) | decode_table[in[3]]);
    out += 3;
    in += 4;
  }

  if (tail_case > 0) {
    switch (tail_case) {
      case 2:
        out[1] =
            (uint8_t)((decode_table[in[1]] << 4) | (decode_table[in[2]] >> 2));
      case 1:
        out[0] =
            (uint8_t)((decode_table[in[0]] << 2) | (decode_table[in[1]] >> 4));
    }
    out += tail_case;
    in += tail_case + 1;
  }

  GPR_ASSERT(out == GPR_SLICE_END_PTR(output));
  GPR_ASSERT(in <= GPR_SLICE_END_PTR(input));
  return output;
}
