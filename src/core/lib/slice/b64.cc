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

#include "src/core/lib/slice/b64.h"

#include <stdint.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include "src/core/lib/slice/slice_internal.h"

/* --- Constants. --- */

static const int8_t base64_bytes[] = {
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   0x3E, -1,   -1,   -1,   0x3F,
    0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, -1,   -1,
    -1,   0x7F, -1,   -1,   -1,   0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12,
    0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, -1,   -1,   -1,   -1,   -1,
    -1,   0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24,
    0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30,
    0x31, 0x32, 0x33, -1,   -1,   -1,   -1,   -1};

static const char base64_url_unsafe_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char base64_url_safe_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

#define GRPC_BASE64_PAD_CHAR '='
#define GRPC_BASE64_PAD_BYTE 0x7F
#define GRPC_BASE64_MULTILINE_LINE_LEN 76
#define GRPC_BASE64_MULTILINE_NUM_BLOCKS (GRPC_BASE64_MULTILINE_LINE_LEN / 4)

/* --- base64 functions. --- */

char *grpc_base64_encode(const void *vdata, size_t data_size, int url_safe,
                         int multiline) {
  size_t result_projected_size =
      grpc_base64_estimate_encoded_size(data_size, url_safe, multiline);
  char *result = (char *)gpr_malloc(result_projected_size);
  grpc_base64_encode_core(result, vdata, data_size, url_safe, multiline);
  return result;
}

size_t grpc_base64_estimate_encoded_size(size_t data_size, int url_safe,
                                         int multiline) {
  size_t result_projected_size =
      4 * ((data_size + 3) / 3) +
      2 * (multiline ? (data_size / (3 * GRPC_BASE64_MULTILINE_NUM_BLOCKS))
                     : 0) +
      1;
  return result_projected_size;
}

void grpc_base64_encode_core(char *result, const void *vdata, size_t data_size,
                             int url_safe, int multiline) {
  const unsigned char *data = (const unsigned char *)vdata;
  const char *base64_chars =
      url_safe ? base64_url_safe_chars : base64_url_unsafe_chars;
  const size_t result_projected_size =
      grpc_base64_estimate_encoded_size(data_size, url_safe, multiline);

  char *current = result;
  size_t num_blocks = 0;
  size_t i = 0;

  /* Encode each block. */
  while (data_size >= 3) {
    *current++ = base64_chars[(data[i] >> 2) & 0x3F];
    *current++ =
        base64_chars[((data[i] & 0x03) << 4) | ((data[i + 1] >> 4) & 0x0F)];
    *current++ =
        base64_chars[((data[i + 1] & 0x0F) << 2) | ((data[i + 2] >> 6) & 0x03)];
    *current++ = base64_chars[data[i + 2] & 0x3F];

    data_size -= 3;
    i += 3;
    if (multiline && (++num_blocks == GRPC_BASE64_MULTILINE_NUM_BLOCKS)) {
      *current++ = '\r';
      *current++ = '\n';
      num_blocks = 0;
    }
  }

  /* Take care of the tail. */
  if (data_size == 2) {
    *current++ = base64_chars[(data[i] >> 2) & 0x3F];
    *current++ =
        base64_chars[((data[i] & 0x03) << 4) | ((data[i + 1] >> 4) & 0x0F)];
    *current++ = base64_chars[(data[i + 1] & 0x0F) << 2];
    *current++ = GRPC_BASE64_PAD_CHAR;
  } else if (data_size == 1) {
    *current++ = base64_chars[(data[i] >> 2) & 0x3F];
    *current++ = base64_chars[(data[i] & 0x03) << 4];
    *current++ = GRPC_BASE64_PAD_CHAR;
    *current++ = GRPC_BASE64_PAD_CHAR;
  }

  GPR_ASSERT(current >= result);
  GPR_ASSERT((uintptr_t)(current - result) < result_projected_size);
  result[current - result] = '\0';
}

grpc_slice grpc_base64_decode(grpc_exec_ctx *exec_ctx, const char *b64,
                              int url_safe) {
  return grpc_base64_decode_with_len(exec_ctx, b64, strlen(b64), url_safe);
}

static void decode_one_char(const unsigned char *codes, unsigned char *result,
                            size_t *result_offset) {
  uint32_t packed = ((uint32_t)codes[0] << 2) | ((uint32_t)codes[1] >> 4);
  result[(*result_offset)++] = (unsigned char)packed;
}

static void decode_two_chars(const unsigned char *codes, unsigned char *result,
                             size_t *result_offset) {
  uint32_t packed = ((uint32_t)codes[0] << 10) | ((uint32_t)codes[1] << 4) |
                    ((uint32_t)codes[2] >> 2);
  result[(*result_offset)++] = (unsigned char)(packed >> 8);
  result[(*result_offset)++] = (unsigned char)(packed);
}

static int decode_group(const unsigned char *codes, size_t num_codes,
                        unsigned char *result, size_t *result_offset) {
  GPR_ASSERT(num_codes <= 4);

  /* Short end groups that may not have padding. */
  if (num_codes == 1) {
    gpr_log(GPR_ERROR, "Invalid group. Must be at least 2 bytes.");
    return 0;
  }
  if (num_codes == 2) {
    decode_one_char(codes, result, result_offset);
    return 1;
  }
  if (num_codes == 3) {
    decode_two_chars(codes, result, result_offset);
    return 1;
  }

  /* Regular 4 byte groups with padding or not. */
  GPR_ASSERT(num_codes == 4);
  if (codes[0] == GRPC_BASE64_PAD_BYTE || codes[1] == GRPC_BASE64_PAD_BYTE) {
    gpr_log(GPR_ERROR, "Invalid padding detected.");
    return 0;
  }
  if (codes[2] == GRPC_BASE64_PAD_BYTE) {
    if (codes[3] == GRPC_BASE64_PAD_BYTE) {
      decode_one_char(codes, result, result_offset);
    } else {
      gpr_log(GPR_ERROR, "Invalid padding detected.");
      return 0;
    }
  } else if (codes[3] == GRPC_BASE64_PAD_BYTE) {
    decode_two_chars(codes, result, result_offset);
  } else {
    /* No padding. */
    uint32_t packed = ((uint32_t)codes[0] << 18) | ((uint32_t)codes[1] << 12) |
                      ((uint32_t)codes[2] << 6) | codes[3];
    result[(*result_offset)++] = (unsigned char)(packed >> 16);
    result[(*result_offset)++] = (unsigned char)(packed >> 8);
    result[(*result_offset)++] = (unsigned char)(packed);
  }
  return 1;
}

grpc_slice grpc_base64_decode_with_len(grpc_exec_ctx *exec_ctx, const char *b64,
                                       size_t b64_len, int url_safe) {
  grpc_slice result = GRPC_SLICE_MALLOC(b64_len);
  unsigned char *current = GRPC_SLICE_START_PTR(result);
  size_t result_size = 0;
  unsigned char codes[4];
  size_t num_codes = 0;

  while (b64_len--) {
    unsigned char c = (unsigned char)(*b64++);
    signed char code;
    if (c >= GPR_ARRAY_SIZE(base64_bytes)) continue;
    if (url_safe) {
      if (c == '+' || c == '/') {
        gpr_log(GPR_ERROR, "Invalid character for url safe base64 %c", c);
        goto fail;
      }
      if (c == '-') {
        c = '+';
      } else if (c == '_') {
        c = '/';
      }
    }
    code = base64_bytes[c];
    if (code == -1) {
      if (c != '\r' && c != '\n') {
        gpr_log(GPR_ERROR, "Invalid character %c", c);
        goto fail;
      }
    } else {
      codes[num_codes++] = (unsigned char)code;
      if (num_codes == 4) {
        if (!decode_group(codes, num_codes, current, &result_size)) goto fail;
        num_codes = 0;
      }
    }
  }

  if (num_codes != 0 &&
      !decode_group(codes, num_codes, current, &result_size)) {
    goto fail;
  }
  GRPC_SLICE_SET_LENGTH(result, result_size);
  return result;

fail:
  grpc_slice_unref_internal(exec_ctx, result);
  return grpc_empty_slice();
}
