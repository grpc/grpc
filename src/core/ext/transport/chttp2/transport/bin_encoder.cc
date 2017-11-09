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

#include "src/core/ext/transport/chttp2/transport/bin_encoder.h"

#include <string.h>

#include <grpc/support/log.h>
#include "src/core/ext/transport/chttp2/transport/huffsyms.h"

static const char alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

typedef struct {
  uint16_t bits;
  uint8_t length;
} b64_huff_sym;

static const b64_huff_sym huff_alphabet[64] = {
    {0x21, 6}, {0x5d, 7}, {0x5e, 7},   {0x5f, 7}, {0x60, 7}, {0x61, 7},
    {0x62, 7}, {0x63, 7}, {0x64, 7},   {0x65, 7}, {0x66, 7}, {0x67, 7},
    {0x68, 7}, {0x69, 7}, {0x6a, 7},   {0x6b, 7}, {0x6c, 7}, {0x6d, 7},
    {0x6e, 7}, {0x6f, 7}, {0x70, 7},   {0x71, 7}, {0x72, 7}, {0xfc, 8},
    {0x73, 7}, {0xfd, 8}, {0x3, 5},    {0x23, 6}, {0x4, 5},  {0x24, 6},
    {0x5, 5},  {0x25, 6}, {0x26, 6},   {0x27, 6}, {0x6, 5},  {0x74, 7},
    {0x75, 7}, {0x28, 6}, {0x29, 6},   {0x2a, 6}, {0x7, 5},  {0x2b, 6},
    {0x76, 7}, {0x2c, 6}, {0x8, 5},    {0x9, 5},  {0x2d, 6}, {0x77, 7},
    {0x78, 7}, {0x79, 7}, {0x7a, 7},   {0x7b, 7}, {0x0, 5},  {0x1, 5},
    {0x2, 5},  {0x19, 6}, {0x1a, 6},   {0x1b, 6}, {0x1c, 6}, {0x1d, 6},
    {0x1e, 6}, {0x1f, 6}, {0x7fb, 11}, {0x18, 6}};

static const uint8_t tail_xtra[3] = {0, 2, 3};

grpc_slice grpc_chttp2_base64_encode(grpc_slice input) {
  size_t input_length = GRPC_SLICE_LENGTH(input);
  size_t input_triplets = input_length / 3;
  size_t tail_case = input_length % 3;
  size_t output_length = input_triplets * 4 + tail_xtra[tail_case];
  grpc_slice output = GRPC_SLICE_MALLOC(output_length);
  uint8_t* in = GRPC_SLICE_START_PTR(input);
  char* out = (char*)GRPC_SLICE_START_PTR(output);
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

  GPR_ASSERT(out == (char*)GRPC_SLICE_END_PTR(output));
  GPR_ASSERT(in == GRPC_SLICE_END_PTR(input));
  return output;
}

grpc_slice grpc_chttp2_huffman_compress(grpc_slice input) {
  size_t nbits;
  uint8_t* in;
  uint8_t* out;
  grpc_slice output;
  uint32_t temp = 0;
  uint32_t temp_length = 0;

  nbits = 0;
  for (in = GRPC_SLICE_START_PTR(input); in != GRPC_SLICE_END_PTR(input);
       ++in) {
    nbits += grpc_chttp2_huffsyms[*in].length;
  }

  output = GRPC_SLICE_MALLOC(nbits / 8 + (nbits % 8 != 0));
  out = GRPC_SLICE_START_PTR(output);
  for (in = GRPC_SLICE_START_PTR(input); in != GRPC_SLICE_END_PTR(input);
       ++in) {
    int sym = *in;
    temp <<= grpc_chttp2_huffsyms[sym].length;
    temp |= grpc_chttp2_huffsyms[sym].bits;
    temp_length += grpc_chttp2_huffsyms[sym].length;

    while (temp_length > 8) {
      temp_length -= 8;
      *out++ = (uint8_t)(temp >> temp_length);
    }
  }

  if (temp_length) {
    /* NB: the following integer arithmetic operation needs to be in its
     * expanded form due to the "integral promotion" performed (see section
     * 3.2.1.1 of the C89 draft standard). A cast to the smaller container type
     * is then required to avoid the compiler warning */
    *out++ = (uint8_t)((uint8_t)(temp << (8u - temp_length)) |
                       (uint8_t)(0xffu >> temp_length));
  }

  GPR_ASSERT(out == GRPC_SLICE_END_PTR(output));

  return output;
}

typedef struct {
  uint32_t temp;
  uint32_t temp_length;
  uint8_t* out;
} huff_out;

static void enc_flush_some(huff_out* out) {
  while (out->temp_length > 8) {
    out->temp_length -= 8;
    *out->out++ = (uint8_t)(out->temp >> out->temp_length);
  }
}

static void enc_add2(huff_out* out, uint8_t a, uint8_t b) {
  b64_huff_sym sa = huff_alphabet[a];
  b64_huff_sym sb = huff_alphabet[b];
  out->temp = (out->temp << (sa.length + sb.length)) |
              ((uint32_t)sa.bits << sb.length) | sb.bits;
  out->temp_length += (uint32_t)sa.length + (uint32_t)sb.length;
  enc_flush_some(out);
}

static void enc_add1(huff_out* out, uint8_t a) {
  b64_huff_sym sa = huff_alphabet[a];
  out->temp = (out->temp << sa.length) | sa.bits;
  out->temp_length += sa.length;
  enc_flush_some(out);
}

grpc_slice grpc_chttp2_base64_encode_and_huffman_compress(grpc_slice input) {
  size_t input_length = GRPC_SLICE_LENGTH(input);
  size_t input_triplets = input_length / 3;
  size_t tail_case = input_length % 3;
  size_t output_syms = input_triplets * 4 + tail_xtra[tail_case];
  size_t max_output_bits = 11 * output_syms;
  size_t max_output_length = max_output_bits / 8 + (max_output_bits % 8 != 0);
  grpc_slice output = GRPC_SLICE_MALLOC(max_output_length);
  uint8_t* in = GRPC_SLICE_START_PTR(input);
  uint8_t* start_out = GRPC_SLICE_START_PTR(output);
  huff_out out;
  size_t i;

  out.temp = 0;
  out.temp_length = 0;
  out.out = start_out;

  /* encode full triplets */
  for (i = 0; i < input_triplets; i++) {
    const uint8_t low_to_high = (uint8_t)((in[0] & 0x3) << 4);
    const uint8_t high_to_low = in[1] >> 4;
    enc_add2(&out, in[0] >> 2, low_to_high | high_to_low);

    const uint8_t a = (uint8_t)((in[1] & 0xf) << 2);
    const uint8_t b = (in[2] >> 6);
    enc_add2(&out, a | b, in[2] & 0x3f);
    in += 3;
  }

  /* encode the remaining bytes */
  switch (tail_case) {
    case 0:
      break;
    case 1:
      enc_add2(&out, in[0] >> 2, (uint8_t)((in[0] & 0x3) << 4));
      in += 1;
      break;
    case 2: {
      const uint8_t low_to_high = (uint8_t)((in[0] & 0x3) << 4);
      const uint8_t high_to_low = in[1] >> 4;
      enc_add2(&out, in[0] >> 2, low_to_high | high_to_low);
      enc_add1(&out, (uint8_t)((in[1] & 0xf) << 2));
      in += 2;
      break;
    }
  }

  if (out.temp_length) {
    /* NB: the following integer arithmetic operation needs to be in its
     * expanded form due to the "integral promotion" performed (see section
     * 3.2.1.1 of the C89 draft standard). A cast to the smaller container type
     * is then required to avoid the compiler warning */
    *out.out++ = (uint8_t)((uint8_t)(out.temp << (8u - out.temp_length)) |
                           (uint8_t)(0xffu >> out.temp_length));
  }

  GPR_ASSERT(out.out <= GRPC_SLICE_END_PTR(output));
  GRPC_SLICE_SET_LENGTH(output, out.out - start_out);

  GPR_ASSERT(in == GRPC_SLICE_END_PTR(input));
  return output;
}
