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

#include "src/core/transport/chttp2/bin_encoder.h"

#include <string.h>

#include "src/core/transport/chttp2/huffsyms.h"
#include <grpc/support/log.h>

static const char alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

typedef struct {
  gpr_uint16 bits;
  gpr_uint8 length;
} b64_huff_sym;

static const b64_huff_sym huff_alphabet[64] = {{0x21, 6},
                                               {0x5d, 7},
                                               {0x5e, 7},
                                               {0x5f, 7},
                                               {0x60, 7},
                                               {0x61, 7},
                                               {0x62, 7},
                                               {0x63, 7},
                                               {0x64, 7},
                                               {0x65, 7},
                                               {0x66, 7},
                                               {0x67, 7},
                                               {0x68, 7},
                                               {0x69, 7},
                                               {0x6a, 7},
                                               {0x6b, 7},
                                               {0x6c, 7},
                                               {0x6d, 7},
                                               {0x6e, 7},
                                               {0x6f, 7},
                                               {0x70, 7},
                                               {0x71, 7},
                                               {0x72, 7},
                                               {0xfc, 8},
                                               {0x73, 7},
                                               {0xfd, 8},
                                               {0x3, 5},
                                               {0x23, 6},
                                               {0x4, 5},
                                               {0x24, 6},
                                               {0x5, 5},
                                               {0x25, 6},
                                               {0x26, 6},
                                               {0x27, 6},
                                               {0x6, 5},
                                               {0x74, 7},
                                               {0x75, 7},
                                               {0x28, 6},
                                               {0x29, 6},
                                               {0x2a, 6},
                                               {0x7, 5},
                                               {0x2b, 6},
                                               {0x76, 7},
                                               {0x2c, 6},
                                               {0x8, 5},
                                               {0x9, 5},
                                               {0x2d, 6},
                                               {0x77, 7},
                                               {0x78, 7},
                                               {0x79, 7},
                                               {0x7a, 7},
                                               {0x7b, 7},
                                               {0x0, 5},
                                               {0x1, 5},
                                               {0x2, 5},
                                               {0x19, 6},
                                               {0x1a, 6},
                                               {0x1b, 6},
                                               {0x1c, 6},
                                               {0x1d, 6},
                                               {0x1e, 6},
                                               {0x1f, 6},
                                               {0x7fb, 11},
                                               {0x18, 6}};

static const gpr_uint8 tail_xtra[3] = {0, 2, 3};

gpr_slice grpc_chttp2_base64_encode(gpr_slice input) {
  size_t input_length = GPR_SLICE_LENGTH(input);
  size_t input_triplets = input_length / 3;
  size_t tail_case = input_length % 3;
  size_t output_length = input_triplets * 4 + tail_xtra[tail_case];
  gpr_slice output = gpr_slice_malloc(output_length);
  gpr_uint8 *in = GPR_SLICE_START_PTR(input);
  char *out = (char *)GPR_SLICE_START_PTR(output);
  size_t i;

  /* encode full triplets */
  for (i = 0; i < input_triplets; i++) {
    out[0] = alphabet[in[0] >> 2];
    out[1] = alphabet[((in[0] & 0x3) << 4) | (in[1] >> 4)];
    out[2] = alphabet[((in[1] & 0xf) << 2) | (in[2] >> 6)];
    out[3] = alphabet[in[2] & 0x3f];
    out += 4;
    in += 3;
  }

  /* encode the remaining bytes */
  switch (tail_case) {
    case 0:
      break;
    case 1:
      out[0] = alphabet[in[0] >> 2];
      out[1] = alphabet[(in[0] & 0x3) << 4];
      out += 2;
      in += 1;
      break;
    case 2:
      out[0] = alphabet[in[0] >> 2];
      out[1] = alphabet[((in[0] & 0x3) << 4) | (in[1] >> 4)];
      out[2] = alphabet[(in[1] & 0xf) << 2];
      out += 3;
      in += 2;
      break;
  }

  GPR_ASSERT(out == (char *)GPR_SLICE_END_PTR(output));
  GPR_ASSERT(in == GPR_SLICE_END_PTR(input));
  return output;
}

gpr_slice grpc_chttp2_huffman_compress(gpr_slice input) {
  size_t nbits;
  gpr_uint8 *in;
  gpr_uint8 *out;
  gpr_slice output;
  gpr_uint32 temp = 0;
  gpr_uint32 temp_length = 0;

  nbits = 0;
  for (in = GPR_SLICE_START_PTR(input); in != GPR_SLICE_END_PTR(input); ++in) {
    nbits += grpc_chttp2_huffsyms[*in].length;
  }

  output = gpr_slice_malloc(nbits / 8 + (nbits % 8 != 0));
  out = GPR_SLICE_START_PTR(output);
  for (in = GPR_SLICE_START_PTR(input); in != GPR_SLICE_END_PTR(input); ++in) {
    int sym = *in;
    temp <<= grpc_chttp2_huffsyms[sym].length;
    temp |= grpc_chttp2_huffsyms[sym].bits;
    temp_length += grpc_chttp2_huffsyms[sym].length;

    while (temp_length > 8) {
      temp_length -= 8;
      *out++ = (gpr_uint8)(temp >> temp_length);
    }
  }

  if (temp_length) {
    /* NB: the following integer arithmetic operation needs to be in its
     * expanded form due to the "integral promotion" performed (see section
     * 3.2.1.1 of the C89 draft standard). A cast to the smaller container type
     * is then required to avoid the compiler warning */
    *out++ = (gpr_uint8)((gpr_uint8)(temp << (8u - temp_length)) |
                         (gpr_uint8)(0xffu >> temp_length));
  }

  GPR_ASSERT(out == GPR_SLICE_END_PTR(output));

  return output;
}

typedef struct {
  gpr_uint32 temp;
  gpr_uint32 temp_length;
  gpr_uint8 *out;
} huff_out;

static void enc_flush_some(huff_out *out) {
  while (out->temp_length > 8) {
    out->temp_length -= 8;
    *out->out++ = (gpr_uint8)(out->temp >> out->temp_length);
  }
}

static void enc_add2(huff_out *out, gpr_uint8 a, gpr_uint8 b) {
  b64_huff_sym sa = huff_alphabet[a];
  b64_huff_sym sb = huff_alphabet[b];
  out->temp = (out->temp << (sa.length + sb.length)) |
              ((gpr_uint32)sa.bits << sb.length) | sb.bits;
  out->temp_length += (gpr_uint32)sa.length + (gpr_uint32)sb.length;
  enc_flush_some(out);
}

static void enc_add1(huff_out *out, gpr_uint8 a) {
  b64_huff_sym sa = huff_alphabet[a];
  out->temp = (out->temp << sa.length) | sa.bits;
  out->temp_length += sa.length;
  enc_flush_some(out);
}

gpr_slice grpc_chttp2_base64_encode_and_huffman_compress(gpr_slice input) {
  size_t input_length = GPR_SLICE_LENGTH(input);
  size_t input_triplets = input_length / 3;
  size_t tail_case = input_length % 3;
  size_t output_syms = input_triplets * 4 + tail_xtra[tail_case];
  size_t max_output_bits = 11 * output_syms;
  size_t max_output_length = max_output_bits / 8 + (max_output_bits % 8 != 0);
  gpr_slice output = gpr_slice_malloc(max_output_length);
  gpr_uint8 *in = GPR_SLICE_START_PTR(input);
  gpr_uint8 *start_out = GPR_SLICE_START_PTR(output);
  huff_out out;
  size_t i;

  out.temp = 0;
  out.temp_length = 0;
  out.out = start_out;

  /* encode full triplets */
  for (i = 0; i < input_triplets; i++) {
    enc_add2(&out, in[0] >> 2, (gpr_uint8)((in[0] & 0x3) << 4) | (in[1] >> 4));
    enc_add2(&out, (gpr_uint8)((in[1] & 0xf) << 2) | (in[2] >> 6),
             (gpr_uint8)(in[2] & 0x3f));
    in += 3;
  }

  /* encode the remaining bytes */
  switch (tail_case) {
    case 0:
      break;
    case 1:
      enc_add2(&out, in[0] >> 2, (gpr_uint8)((in[0] & 0x3) << 4));
      in += 1;
      break;
    case 2:
      enc_add2(&out, in[0] >> 2,
               (gpr_uint8)((in[0] & 0x3) << 4) | (gpr_uint8)(in[1] >> 4));
      enc_add1(&out, (gpr_uint8)((in[1] & 0xf) << 2));
      in += 2;
      break;
  }

  if (out.temp_length) {
    /* NB: the following integer arithmetic operation needs to be in its
     * expanded form due to the "integral promotion" performed (see section
     * 3.2.1.1 of the C89 draft standard). A cast to the smaller container type
     * is then required to avoid the compiler warning */
    *out.out++ = (gpr_uint8)((gpr_uint8)(out.temp << (8u - out.temp_length)) |
                             (gpr_uint8)(0xffu >> out.temp_length));
  }

  GPR_ASSERT(out.out <= GPR_SLICE_END_PTR(output));
  GPR_SLICE_SET_LENGTH(output, out.out - start_out);

  GPR_ASSERT(in == GPR_SLICE_END_PTR(input));
  return output;
}

int grpc_is_binary_header(const char *key, size_t length) {
  if (length < 5) return 0;
  return 0 == memcmp(key + length - 4, "-bin", 4);
}
