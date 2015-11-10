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

#include "src/core/transport/chttp2/hpack_parser.h"
#include "src/core/transport/chttp2/internal.h"

#include <stddef.h>
#include <string.h>
#include <assert.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/useful.h>

#include "src/core/profiling/timers.h"
#include "src/core/support/string.h"
#include "src/core/transport/chttp2/bin_encoder.h"

typedef enum {
  NOT_BINARY,
  B64_BYTE0,
  B64_BYTE1,
  B64_BYTE2,
  B64_BYTE3
} binary_state;

/* How parsing works:

   The parser object keeps track of a function pointer which represents the
   current parse state.

   Each time new bytes are presented, we call into the current state, which
   recursively parses until all bytes in the given chunk are exhausted.

   The parse state that terminates then saves its function pointer to be the
   current state so that it can resume when more bytes are available.

   It's expected that most optimizing compilers will turn this code into
   a set of indirect jumps, and so not waste stack space. */

/* forward declarations for parsing states */
static int parse_begin(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                       const gpr_uint8 *end);
static int parse_error(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                       const gpr_uint8 *end);
static int parse_illegal_op(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                            const gpr_uint8 *end);

static int parse_string_prefix(grpc_chttp2_hpack_parser *p,
                               const gpr_uint8 *cur, const gpr_uint8 *end);
static int parse_key_string(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                            const gpr_uint8 *end);
static int parse_value_string_with_indexed_key(grpc_chttp2_hpack_parser *p,
                                               const gpr_uint8 *cur,
                                               const gpr_uint8 *end);
static int parse_value_string_with_literal_key(grpc_chttp2_hpack_parser *p,
                                               const gpr_uint8 *cur,
                                               const gpr_uint8 *end);

static int parse_value0(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                        const gpr_uint8 *end);
static int parse_value1(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                        const gpr_uint8 *end);
static int parse_value2(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                        const gpr_uint8 *end);
static int parse_value3(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                        const gpr_uint8 *end);
static int parse_value4(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                        const gpr_uint8 *end);
static int parse_value5up(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                          const gpr_uint8 *end);

static int parse_indexed_field(grpc_chttp2_hpack_parser *p,
                               const gpr_uint8 *cur, const gpr_uint8 *end);
static int parse_indexed_field_x(grpc_chttp2_hpack_parser *p,
                                 const gpr_uint8 *cur, const gpr_uint8 *end);
static int parse_lithdr_incidx(grpc_chttp2_hpack_parser *p,
                               const gpr_uint8 *cur, const gpr_uint8 *end);
static int parse_lithdr_incidx_x(grpc_chttp2_hpack_parser *p,
                                 const gpr_uint8 *cur, const gpr_uint8 *end);
static int parse_lithdr_incidx_v(grpc_chttp2_hpack_parser *p,
                                 const gpr_uint8 *cur, const gpr_uint8 *end);
static int parse_lithdr_notidx(grpc_chttp2_hpack_parser *p,
                               const gpr_uint8 *cur, const gpr_uint8 *end);
static int parse_lithdr_notidx_x(grpc_chttp2_hpack_parser *p,
                                 const gpr_uint8 *cur, const gpr_uint8 *end);
static int parse_lithdr_notidx_v(grpc_chttp2_hpack_parser *p,
                                 const gpr_uint8 *cur, const gpr_uint8 *end);
static int parse_lithdr_nvridx(grpc_chttp2_hpack_parser *p,
                               const gpr_uint8 *cur, const gpr_uint8 *end);
static int parse_lithdr_nvridx_x(grpc_chttp2_hpack_parser *p,
                                 const gpr_uint8 *cur, const gpr_uint8 *end);
static int parse_lithdr_nvridx_v(grpc_chttp2_hpack_parser *p,
                                 const gpr_uint8 *cur, const gpr_uint8 *end);
static int parse_max_tbl_size(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                              const gpr_uint8 *end);
static int parse_max_tbl_size_x(grpc_chttp2_hpack_parser *p,
                                const gpr_uint8 *cur, const gpr_uint8 *end);

/* we translate the first byte of a hpack field into one of these decoding
   cases, then use a lookup table to jump directly to the appropriate parser.

   _X => the integer index is all ones, meaning we need to do varint decoding
   _V => the integer index is all zeros, meaning we need to decode an additional
         string value */
typedef enum {
  INDEXED_FIELD,
  INDEXED_FIELD_X,
  LITHDR_INCIDX,
  LITHDR_INCIDX_X,
  LITHDR_INCIDX_V,
  LITHDR_NOTIDX,
  LITHDR_NOTIDX_X,
  LITHDR_NOTIDX_V,
  LITHDR_NVRIDX,
  LITHDR_NVRIDX_X,
  LITHDR_NVRIDX_V,
  MAX_TBL_SIZE,
  MAX_TBL_SIZE_X,
  ILLEGAL
} first_byte_type;

/* jump table of parse state functions -- order must match first_byte_type
   above */
static const grpc_chttp2_hpack_parser_state first_byte_action[] = {
    parse_indexed_field,   parse_indexed_field_x, parse_lithdr_incidx,
    parse_lithdr_incidx_x, parse_lithdr_incidx_v, parse_lithdr_notidx,
    parse_lithdr_notidx_x, parse_lithdr_notidx_v, parse_lithdr_nvridx,
    parse_lithdr_nvridx_x, parse_lithdr_nvridx_v, parse_max_tbl_size,
    parse_max_tbl_size_x,  parse_illegal_op};

/* indexes the first byte to a parse state function - generated by
   gen_hpack_tables.c */
static const gpr_uint8 first_byte_lut[256] = {
    LITHDR_NOTIDX_V, LITHDR_NOTIDX, LITHDR_NOTIDX, LITHDR_NOTIDX,
    LITHDR_NOTIDX,   LITHDR_NOTIDX, LITHDR_NOTIDX, LITHDR_NOTIDX,
    LITHDR_NOTIDX,   LITHDR_NOTIDX, LITHDR_NOTIDX, LITHDR_NOTIDX,
    LITHDR_NOTIDX,   LITHDR_NOTIDX, LITHDR_NOTIDX, LITHDR_NOTIDX_X,
    LITHDR_NVRIDX_V, LITHDR_NVRIDX, LITHDR_NVRIDX, LITHDR_NVRIDX,
    LITHDR_NVRIDX,   LITHDR_NVRIDX, LITHDR_NVRIDX, LITHDR_NVRIDX,
    LITHDR_NVRIDX,   LITHDR_NVRIDX, LITHDR_NVRIDX, LITHDR_NVRIDX,
    LITHDR_NVRIDX,   LITHDR_NVRIDX, LITHDR_NVRIDX, LITHDR_NVRIDX_X,
    MAX_TBL_SIZE,    MAX_TBL_SIZE,  MAX_TBL_SIZE,  MAX_TBL_SIZE,
    MAX_TBL_SIZE,    MAX_TBL_SIZE,  MAX_TBL_SIZE,  MAX_TBL_SIZE,
    MAX_TBL_SIZE,    MAX_TBL_SIZE,  MAX_TBL_SIZE,  MAX_TBL_SIZE,
    MAX_TBL_SIZE,    MAX_TBL_SIZE,  MAX_TBL_SIZE,  MAX_TBL_SIZE,
    MAX_TBL_SIZE,    MAX_TBL_SIZE,  MAX_TBL_SIZE,  MAX_TBL_SIZE,
    MAX_TBL_SIZE,    MAX_TBL_SIZE,  MAX_TBL_SIZE,  MAX_TBL_SIZE,
    MAX_TBL_SIZE,    MAX_TBL_SIZE,  MAX_TBL_SIZE,  MAX_TBL_SIZE,
    MAX_TBL_SIZE,    MAX_TBL_SIZE,  MAX_TBL_SIZE,  MAX_TBL_SIZE_X,
    LITHDR_INCIDX_V, LITHDR_INCIDX, LITHDR_INCIDX, LITHDR_INCIDX,
    LITHDR_INCIDX,   LITHDR_INCIDX, LITHDR_INCIDX, LITHDR_INCIDX,
    LITHDR_INCIDX,   LITHDR_INCIDX, LITHDR_INCIDX, LITHDR_INCIDX,
    LITHDR_INCIDX,   LITHDR_INCIDX, LITHDR_INCIDX, LITHDR_INCIDX,
    LITHDR_INCIDX,   LITHDR_INCIDX, LITHDR_INCIDX, LITHDR_INCIDX,
    LITHDR_INCIDX,   LITHDR_INCIDX, LITHDR_INCIDX, LITHDR_INCIDX,
    LITHDR_INCIDX,   LITHDR_INCIDX, LITHDR_INCIDX, LITHDR_INCIDX,
    LITHDR_INCIDX,   LITHDR_INCIDX, LITHDR_INCIDX, LITHDR_INCIDX,
    LITHDR_INCIDX,   LITHDR_INCIDX, LITHDR_INCIDX, LITHDR_INCIDX,
    LITHDR_INCIDX,   LITHDR_INCIDX, LITHDR_INCIDX, LITHDR_INCIDX,
    LITHDR_INCIDX,   LITHDR_INCIDX, LITHDR_INCIDX, LITHDR_INCIDX,
    LITHDR_INCIDX,   LITHDR_INCIDX, LITHDR_INCIDX, LITHDR_INCIDX,
    LITHDR_INCIDX,   LITHDR_INCIDX, LITHDR_INCIDX, LITHDR_INCIDX,
    LITHDR_INCIDX,   LITHDR_INCIDX, LITHDR_INCIDX, LITHDR_INCIDX,
    LITHDR_INCIDX,   LITHDR_INCIDX, LITHDR_INCIDX, LITHDR_INCIDX,
    LITHDR_INCIDX,   LITHDR_INCIDX, LITHDR_INCIDX, LITHDR_INCIDX_X,
    ILLEGAL,         INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD,
    INDEXED_FIELD,   INDEXED_FIELD, INDEXED_FIELD, INDEXED_FIELD_X,
};

/* state table for huffman decoding: given a state, gives an index/16 into
   next_sub_tbl. Taking that index and adding the value of the nibble being
   considered returns the next state.

   generated by gen_hpack_tables.c */
static const gpr_uint8 next_tbl[256] = {
    0,  1,  2,  3,  4,  1,  2, 5,  6,  1, 7,  8,  1,  3,  3,  9,  10, 11, 1,  1,
    1,  12, 1,  2,  13, 1,  1, 1,  1,  1, 1,  1,  1,  1,  1,  1,  1,  1,  1,  2,
    14, 1,  15, 16, 1,  17, 1, 15, 2,  7, 3,  18, 19, 1,  1,  1,  1,  20, 1,  1,
    1,  1,  1,  1,  1,  1,  1, 1,  15, 2, 2,  7,  21, 1,  22, 1,  1,  1,  1,  1,
    1,  1,  1,  15, 2,  2,  2, 2,  2,  2, 23, 24, 25, 1,  1,  1,  1,  2,  2,  2,
    26, 3,  3,  27, 10, 28, 1, 1,  1,  1, 1,  1,  2,  3,  29, 10, 30, 1,  1,  1,
    1,  1,  1,  1,  1,  1,  1, 1,  1,  1, 1,  31, 1,  1,  1,  1,  1,  1,  1,  2,
    2,  2,  2,  2,  2,  2,  2, 32, 1,  1, 15, 33, 1,  34, 35, 9,  36, 1,  1,  1,
    1,  1,  1,  1,  37, 1,  1, 1,  1,  1, 1,  2,  2,  2,  2,  2,  2,  2,  26, 9,
    38, 1,  1,  1,  1,  1,  1, 1,  15, 2, 2,  2,  2,  26, 3,  3,  39, 1,  1,  1,
    1,  1,  1,  1,  1,  1,  1, 1,  2,  2, 2,  2,  2,  2,  7,  3,  3,  3,  40, 2,
    41, 1,  1,  1,  42, 43, 1, 1,  44, 1, 1,  1,  1,  15, 2,  2,  2,  2,  2,  2,
    3,  3,  3,  45, 46, 1,  1, 2,  2,  2, 35, 3,  3,  18, 47, 2,
};

/* next state, based upon current state and the current nibble: see above.
   generated by gen_hpack_tables.c */
static const gpr_int16 next_sub_tbl[48 * 16] = {
    1,   204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217,
    218, 2,   6,   10,  13,  14,  15,  16,  17,  2,   6,   10,  13,  14,  15,
    16,  17,  3,   7,   11,  24,  3,   7,   11,  24,  3,   7,   11,  24,  3,
    7,   11,  24,  4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   4,   8,
    4,   8,   4,   8,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   5,
    199, 200, 201, 202, 203, 4,   8,   4,   8,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   9,   133, 134, 135, 136, 137, 138, 139, 140,
    141, 142, 143, 144, 145, 146, 147, 3,   7,   11,  24,  3,   7,   11,  24,
    4,   8,   4,   8,   4,   8,   4,   8,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   12,  132, 4,   8,   4,   8,   4,   8,
    4,   8,   4,   8,   4,   8,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   18,  19,  20,  21,  4,   8,   4,
    8,   4,   8,   4,   8,   4,   8,   0,   0,   0,   22,  23,  91,  25,  26,
    27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  3,
    7,   11,  24,  3,   7,   11,  24,  0,   0,   0,   0,   0,   41,  42,  43,
    2,   6,   10,  13,  14,  15,  16,  17,  3,   7,   11,  24,  3,   7,   11,
    24,  4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   0,   0,
    44,  45,  2,   6,   10,  13,  14,  15,  16,  17,  46,  47,  48,  49,  50,
    51,  52,  57,  4,   8,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   53,  54,  55,  56,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,
    68,  69,  70,  71,  72,  74,  0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   73,  75,  76,  77,  78,  79,  80,  81,  82,
    83,  84,  85,  86,  87,  88,  89,  90,  3,   7,   11,  24,  3,   7,   11,
    24,  3,   7,   11,  24,  0,   0,   0,   0,   3,   7,   11,  24,  3,   7,
    11,  24,  4,   8,   4,   8,   0,   0,   0,   92,  0,   0,   0,   93,  94,
    95,  96,  97,  98,  99,  100, 101, 102, 103, 104, 105, 3,   7,   11,  24,
    4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   4,
    8,   4,   8,   4,   8,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 4,
    8,   4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   0,   0,
    0,   117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130,
    131, 2,   6,   10,  13,  14,  15,  16,  17,  4,   8,   4,   8,   4,   8,
    4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   148,
    149, 150, 151, 3,   7,   11,  24,  4,   8,   4,   8,   0,   0,   0,   0,
    0,   0,   152, 153, 3,   7,   11,  24,  3,   7,   11,  24,  3,   7,   11,
    24,  154, 155, 156, 164, 3,   7,   11,  24,  3,   7,   11,  24,  3,   7,
    11,  24,  4,   8,   4,   8,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    157, 158, 159, 160, 161, 162, 163, 165, 166, 167, 168, 169, 170, 171, 172,
    173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187,
    188, 189, 190, 191, 192, 193, 194, 195, 196, 4,   8,   4,   8,   4,   8,
    4,   8,   4,   8,   4,   8,   4,   8,   197, 198, 4,   8,   4,   8,   4,
    8,   4,   8,   0,   0,   0,   0,   0,   0,   219, 220, 3,   7,   11,  24,
    4,   8,   4,   8,   4,   8,   0,   0,   221, 222, 223, 224, 3,   7,   11,
    24,  3,   7,   11,  24,  4,   8,   4,   8,   4,   8,   225, 228, 4,   8,
    4,   8,   4,   8,   0,   0,   0,   0,   0,   0,   0,   0,   226, 227, 229,
    230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244,
    4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   245, 246, 247, 248, 249, 250, 251, 252,
    253, 254, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   255,
};

/* emission table: indexed like next_tbl, ultimately gives the byte to be
   emitted, or -1 for no byte, or 256 for end of stream

   generated by gen_hpack_tables.c */
static const gpr_uint16 emit_tbl[256] = {
    0,   1,   2,   3,   4,   5,   6,   7,   0,   8,   9,   10,  11,  12,  13,
    14,  15,  16,  17,  18,  19,  20,  21,  22,  0,   23,  24,  25,  26,  27,
    28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,
    43,  44,  45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  0,   55,  56,
    57,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  0,
    71,  72,  73,  74,  75,  76,  77,  78,  79,  80,  81,  82,  83,  84,  85,
    86,  87,  88,  89,  90,  91,  92,  93,  94,  95,  96,  97,  98,  99,  100,
    101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115,
    116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130,
    131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145,
    146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 0,
    160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174,
    0,   175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188,
    189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203,
    204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218,
    219, 220, 221, 0,   222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232,
    233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247,
    248,
};

/* generated by gen_hpack_tables.c */
static const gpr_int16 emit_sub_tbl[249 * 16] = {
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  48,  48,  48,  48,  48,  48,  48,  48,  49,  49,  49,  49,  49,  49,
    49,  49,  48,  48,  48,  48,  49,  49,  49,  49,  50,  50,  50,  50,  97,
    97,  97,  97,  48,  48,  49,  49,  50,  50,  97,  97,  99,  99,  101, 101,
    105, 105, 111, 111, 48,  49,  50,  97,  99,  101, 105, 111, 115, 116, -1,
    -1,  -1,  -1,  -1,  -1,  32,  32,  32,  32,  32,  32,  32,  32,  37,  37,
    37,  37,  37,  37,  37,  37,  99,  99,  99,  99,  101, 101, 101, 101, 105,
    105, 105, 105, 111, 111, 111, 111, 115, 115, 116, 116, 32,  37,  45,  46,
    47,  51,  52,  53,  54,  55,  56,  57,  61,  61,  61,  61,  61,  61,  61,
    61,  65,  65,  65,  65,  65,  65,  65,  65,  115, 115, 115, 115, 116, 116,
    116, 116, 32,  32,  37,  37,  45,  45,  46,  46,  61,  65,  95,  98,  100,
    102, 103, 104, 108, 109, 110, 112, 114, 117, -1,  -1,  58,  58,  58,  58,
    58,  58,  58,  58,  66,  66,  66,  66,  66,  66,  66,  66,  47,  47,  51,
    51,  52,  52,  53,  53,  54,  54,  55,  55,  56,  56,  57,  57,  61,  61,
    65,  65,  95,  95,  98,  98,  100, 100, 102, 102, 103, 103, 104, 104, 108,
    108, 109, 109, 110, 110, 112, 112, 114, 114, 117, 117, 58,  66,  67,  68,
    69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,  80,  81,  82,  83,
    84,  85,  86,  87,  89,  106, 107, 113, 118, 119, 120, 121, 122, -1,  -1,
    -1,  -1,  38,  38,  38,  38,  38,  38,  38,  38,  42,  42,  42,  42,  42,
    42,  42,  42,  44,  44,  44,  44,  44,  44,  44,  44,  59,  59,  59,  59,
    59,  59,  59,  59,  88,  88,  88,  88,  88,  88,  88,  88,  90,  90,  90,
    90,  90,  90,  90,  90,  33,  33,  34,  34,  40,  40,  41,  41,  63,  63,
    39,  43,  124, -1,  -1,  -1,  35,  35,  35,  35,  35,  35,  35,  35,  62,
    62,  62,  62,  62,  62,  62,  62,  0,   0,   0,   0,   36,  36,  36,  36,
    64,  64,  64,  64,  91,  91,  91,  91,  69,  69,  69,  69,  69,  69,  69,
    69,  70,  70,  70,  70,  70,  70,  70,  70,  71,  71,  71,  71,  71,  71,
    71,  71,  72,  72,  72,  72,  72,  72,  72,  72,  73,  73,  73,  73,  73,
    73,  73,  73,  74,  74,  74,  74,  74,  74,  74,  74,  75,  75,  75,  75,
    75,  75,  75,  75,  76,  76,  76,  76,  76,  76,  76,  76,  77,  77,  77,
    77,  77,  77,  77,  77,  78,  78,  78,  78,  78,  78,  78,  78,  79,  79,
    79,  79,  79,  79,  79,  79,  80,  80,  80,  80,  80,  80,  80,  80,  81,
    81,  81,  81,  81,  81,  81,  81,  82,  82,  82,  82,  82,  82,  82,  82,
    83,  83,  83,  83,  83,  83,  83,  83,  84,  84,  84,  84,  84,  84,  84,
    84,  85,  85,  85,  85,  85,  85,  85,  85,  86,  86,  86,  86,  86,  86,
    86,  86,  87,  87,  87,  87,  87,  87,  87,  87,  89,  89,  89,  89,  89,
    89,  89,  89,  106, 106, 106, 106, 106, 106, 106, 106, 107, 107, 107, 107,
    107, 107, 107, 107, 113, 113, 113, 113, 113, 113, 113, 113, 118, 118, 118,
    118, 118, 118, 118, 118, 119, 119, 119, 119, 119, 119, 119, 119, 120, 120,
    120, 120, 120, 120, 120, 120, 121, 121, 121, 121, 121, 121, 121, 121, 122,
    122, 122, 122, 122, 122, 122, 122, 38,  38,  38,  38,  42,  42,  42,  42,
    44,  44,  44,  44,  59,  59,  59,  59,  88,  88,  88,  88,  90,  90,  90,
    90,  33,  34,  40,  41,  63,  -1,  -1,  -1,  39,  39,  39,  39,  39,  39,
    39,  39,  43,  43,  43,  43,  43,  43,  43,  43,  124, 124, 124, 124, 124,
    124, 124, 124, 35,  35,  35,  35,  62,  62,  62,  62,  0,   0,   36,  36,
    64,  64,  91,  91,  93,  93,  126, 126, 94,  125, -1,  -1,  60,  60,  60,
    60,  60,  60,  60,  60,  96,  96,  96,  96,  96,  96,  96,  96,  123, 123,
    123, 123, 123, 123, 123, 123, -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  92,
    92,  92,  92,  92,  92,  92,  92,  195, 195, 195, 195, 195, 195, 195, 195,
    208, 208, 208, 208, 208, 208, 208, 208, 128, 128, 128, 128, 130, 130, 130,
    130, 131, 131, 131, 131, 162, 162, 162, 162, 184, 184, 184, 184, 194, 194,
    194, 194, 224, 224, 224, 224, 226, 226, 226, 226, 153, 153, 161, 161, 167,
    167, 172, 172, 176, 176, 177, 177, 179, 179, 209, 209, 216, 216, 217, 217,
    227, 227, 229, 229, 230, 230, 129, 132, 133, 134, 136, 146, 154, 156, 160,
    163, 164, 169, 170, 173, 178, 181, 185, 186, 187, 189, 190, 196, 198, 228,
    232, 233, -1,  -1,  -1,  -1,  1,   1,   1,   1,   1,   1,   1,   1,   135,
    135, 135, 135, 135, 135, 135, 135, 137, 137, 137, 137, 137, 137, 137, 137,
    138, 138, 138, 138, 138, 138, 138, 138, 139, 139, 139, 139, 139, 139, 139,
    139, 140, 140, 140, 140, 140, 140, 140, 140, 141, 141, 141, 141, 141, 141,
    141, 141, 143, 143, 143, 143, 143, 143, 143, 143, 147, 147, 147, 147, 147,
    147, 147, 147, 149, 149, 149, 149, 149, 149, 149, 149, 150, 150, 150, 150,
    150, 150, 150, 150, 151, 151, 151, 151, 151, 151, 151, 151, 152, 152, 152,
    152, 152, 152, 152, 152, 155, 155, 155, 155, 155, 155, 155, 155, 157, 157,
    157, 157, 157, 157, 157, 157, 158, 158, 158, 158, 158, 158, 158, 158, 165,
    165, 165, 165, 165, 165, 165, 165, 166, 166, 166, 166, 166, 166, 166, 166,
    168, 168, 168, 168, 168, 168, 168, 168, 174, 174, 174, 174, 174, 174, 174,
    174, 175, 175, 175, 175, 175, 175, 175, 175, 180, 180, 180, 180, 180, 180,
    180, 180, 182, 182, 182, 182, 182, 182, 182, 182, 183, 183, 183, 183, 183,
    183, 183, 183, 188, 188, 188, 188, 188, 188, 188, 188, 191, 191, 191, 191,
    191, 191, 191, 191, 197, 197, 197, 197, 197, 197, 197, 197, 231, 231, 231,
    231, 231, 231, 231, 231, 239, 239, 239, 239, 239, 239, 239, 239, 9,   9,
    9,   9,   142, 142, 142, 142, 144, 144, 144, 144, 145, 145, 145, 145, 148,
    148, 148, 148, 159, 159, 159, 159, 171, 171, 171, 171, 206, 206, 206, 206,
    215, 215, 215, 215, 225, 225, 225, 225, 236, 236, 236, 236, 237, 237, 237,
    237, 199, 199, 207, 207, 234, 234, 235, 235, 192, 193, 200, 201, 202, 205,
    210, 213, 218, 219, 238, 240, 242, 243, 255, -1,  203, 203, 203, 203, 203,
    203, 203, 203, 204, 204, 204, 204, 204, 204, 204, 204, 211, 211, 211, 211,
    211, 211, 211, 211, 212, 212, 212, 212, 212, 212, 212, 212, 214, 214, 214,
    214, 214, 214, 214, 214, 221, 221, 221, 221, 221, 221, 221, 221, 222, 222,
    222, 222, 222, 222, 222, 222, 223, 223, 223, 223, 223, 223, 223, 223, 241,
    241, 241, 241, 241, 241, 241, 241, 244, 244, 244, 244, 244, 244, 244, 244,
    245, 245, 245, 245, 245, 245, 245, 245, 246, 246, 246, 246, 246, 246, 246,
    246, 247, 247, 247, 247, 247, 247, 247, 247, 248, 248, 248, 248, 248, 248,
    248, 248, 250, 250, 250, 250, 250, 250, 250, 250, 251, 251, 251, 251, 251,
    251, 251, 251, 252, 252, 252, 252, 252, 252, 252, 252, 253, 253, 253, 253,
    253, 253, 253, 253, 254, 254, 254, 254, 254, 254, 254, 254, 2,   2,   2,
    2,   3,   3,   3,   3,   4,   4,   4,   4,   5,   5,   5,   5,   6,   6,
    6,   6,   7,   7,   7,   7,   8,   8,   8,   8,   11,  11,  11,  11,  12,
    12,  12,  12,  14,  14,  14,  14,  15,  15,  15,  15,  16,  16,  16,  16,
    17,  17,  17,  17,  18,  18,  18,  18,  19,  19,  19,  19,  20,  20,  20,
    20,  21,  21,  21,  21,  23,  23,  23,  23,  24,  24,  24,  24,  25,  25,
    25,  25,  26,  26,  26,  26,  27,  27,  27,  27,  28,  28,  28,  28,  29,
    29,  29,  29,  30,  30,  30,  30,  31,  31,  31,  31,  127, 127, 127, 127,
    220, 220, 220, 220, 249, 249, 249, 249, 10,  13,  22,  256, 93,  93,  93,
    93,  126, 126, 126, 126, 94,  94,  125, 125, 60,  96,  123, -1,  92,  195,
    208, -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  128,
    128, 128, 128, 128, 128, 128, 128, 130, 130, 130, 130, 130, 130, 130, 130,
    131, 131, 131, 131, 131, 131, 131, 131, 162, 162, 162, 162, 162, 162, 162,
    162, 184, 184, 184, 184, 184, 184, 184, 184, 194, 194, 194, 194, 194, 194,
    194, 194, 224, 224, 224, 224, 224, 224, 224, 224, 226, 226, 226, 226, 226,
    226, 226, 226, 153, 153, 153, 153, 161, 161, 161, 161, 167, 167, 167, 167,
    172, 172, 172, 172, 176, 176, 176, 176, 177, 177, 177, 177, 179, 179, 179,
    179, 209, 209, 209, 209, 216, 216, 216, 216, 217, 217, 217, 217, 227, 227,
    227, 227, 229, 229, 229, 229, 230, 230, 230, 230, 129, 129, 132, 132, 133,
    133, 134, 134, 136, 136, 146, 146, 154, 154, 156, 156, 160, 160, 163, 163,
    164, 164, 169, 169, 170, 170, 173, 173, 178, 178, 181, 181, 185, 185, 186,
    186, 187, 187, 189, 189, 190, 190, 196, 196, 198, 198, 228, 228, 232, 232,
    233, 233, 1,   135, 137, 138, 139, 140, 141, 143, 147, 149, 150, 151, 152,
    155, 157, 158, 165, 166, 168, 174, 175, 180, 182, 183, 188, 191, 197, 231,
    239, -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  9,   9,   9,
    9,   9,   9,   9,   9,   142, 142, 142, 142, 142, 142, 142, 142, 144, 144,
    144, 144, 144, 144, 144, 144, 145, 145, 145, 145, 145, 145, 145, 145, 148,
    148, 148, 148, 148, 148, 148, 148, 159, 159, 159, 159, 159, 159, 159, 159,
    171, 171, 171, 171, 171, 171, 171, 171, 206, 206, 206, 206, 206, 206, 206,
    206, 215, 215, 215, 215, 215, 215, 215, 215, 225, 225, 225, 225, 225, 225,
    225, 225, 236, 236, 236, 236, 236, 236, 236, 236, 237, 237, 237, 237, 237,
    237, 237, 237, 199, 199, 199, 199, 207, 207, 207, 207, 234, 234, 234, 234,
    235, 235, 235, 235, 192, 192, 193, 193, 200, 200, 201, 201, 202, 202, 205,
    205, 210, 210, 213, 213, 218, 218, 219, 219, 238, 238, 240, 240, 242, 242,
    243, 243, 255, 255, 203, 204, 211, 212, 214, 221, 222, 223, 241, 244, 245,
    246, 247, 248, 250, 251, 252, 253, 254, -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  2,   2,   2,   2,   2,   2,   2,
    2,   3,   3,   3,   3,   3,   3,   3,   3,   4,   4,   4,   4,   4,   4,
    4,   4,   5,   5,   5,   5,   5,   5,   5,   5,   6,   6,   6,   6,   6,
    6,   6,   6,   7,   7,   7,   7,   7,   7,   7,   7,   8,   8,   8,   8,
    8,   8,   8,   8,   11,  11,  11,  11,  11,  11,  11,  11,  12,  12,  12,
    12,  12,  12,  12,  12,  14,  14,  14,  14,  14,  14,  14,  14,  15,  15,
    15,  15,  15,  15,  15,  15,  16,  16,  16,  16,  16,  16,  16,  16,  17,
    17,  17,  17,  17,  17,  17,  17,  18,  18,  18,  18,  18,  18,  18,  18,
    19,  19,  19,  19,  19,  19,  19,  19,  20,  20,  20,  20,  20,  20,  20,
    20,  21,  21,  21,  21,  21,  21,  21,  21,  23,  23,  23,  23,  23,  23,
    23,  23,  24,  24,  24,  24,  24,  24,  24,  24,  25,  25,  25,  25,  25,
    25,  25,  25,  26,  26,  26,  26,  26,  26,  26,  26,  27,  27,  27,  27,
    27,  27,  27,  27,  28,  28,  28,  28,  28,  28,  28,  28,  29,  29,  29,
    29,  29,  29,  29,  29,  30,  30,  30,  30,  30,  30,  30,  30,  31,  31,
    31,  31,  31,  31,  31,  31,  127, 127, 127, 127, 127, 127, 127, 127, 220,
    220, 220, 220, 220, 220, 220, 220, 249, 249, 249, 249, 249, 249, 249, 249,
    10,  10,  13,  13,  22,  22,  256, 256, 67,  67,  67,  67,  67,  67,  67,
    67,  68,  68,  68,  68,  68,  68,  68,  68,  95,  95,  95,  95,  95,  95,
    95,  95,  98,  98,  98,  98,  98,  98,  98,  98,  100, 100, 100, 100, 100,
    100, 100, 100, 102, 102, 102, 102, 102, 102, 102, 102, 103, 103, 103, 103,
    103, 103, 103, 103, 104, 104, 104, 104, 104, 104, 104, 104, 108, 108, 108,
    108, 108, 108, 108, 108, 109, 109, 109, 109, 109, 109, 109, 109, 110, 110,
    110, 110, 110, 110, 110, 110, 112, 112, 112, 112, 112, 112, 112, 112, 114,
    114, 114, 114, 114, 114, 114, 114, 117, 117, 117, 117, 117, 117, 117, 117,
    58,  58,  58,  58,  66,  66,  66,  66,  67,  67,  67,  67,  68,  68,  68,
    68,  69,  69,  69,  69,  70,  70,  70,  70,  71,  71,  71,  71,  72,  72,
    72,  72,  73,  73,  73,  73,  74,  74,  74,  74,  75,  75,  75,  75,  76,
    76,  76,  76,  77,  77,  77,  77,  78,  78,  78,  78,  79,  79,  79,  79,
    80,  80,  80,  80,  81,  81,  81,  81,  82,  82,  82,  82,  83,  83,  83,
    83,  84,  84,  84,  84,  85,  85,  85,  85,  86,  86,  86,  86,  87,  87,
    87,  87,  89,  89,  89,  89,  106, 106, 106, 106, 107, 107, 107, 107, 113,
    113, 113, 113, 118, 118, 118, 118, 119, 119, 119, 119, 120, 120, 120, 120,
    121, 121, 121, 121, 122, 122, 122, 122, 38,  38,  42,  42,  44,  44,  59,
    59,  88,  88,  90,  90,  -1,  -1,  -1,  -1,  33,  33,  33,  33,  33,  33,
    33,  33,  34,  34,  34,  34,  34,  34,  34,  34,  40,  40,  40,  40,  40,
    40,  40,  40,  41,  41,  41,  41,  41,  41,  41,  41,  63,  63,  63,  63,
    63,  63,  63,  63,  39,  39,  39,  39,  43,  43,  43,  43,  124, 124, 124,
    124, 35,  35,  62,  62,  0,   36,  64,  91,  93,  126, -1,  -1,  94,  94,
    94,  94,  94,  94,  94,  94,  125, 125, 125, 125, 125, 125, 125, 125, 60,
    60,  60,  60,  96,  96,  96,  96,  123, 123, 123, 123, -1,  -1,  -1,  -1,
    92,  92,  92,  92,  195, 195, 195, 195, 208, 208, 208, 208, 128, 128, 130,
    130, 131, 131, 162, 162, 184, 184, 194, 194, 224, 224, 226, 226, 153, 161,
    167, 172, 176, 177, 179, 209, 216, 217, 227, 229, 230, -1,  -1,  -1,  -1,
    -1,  -1,  -1,  129, 129, 129, 129, 129, 129, 129, 129, 132, 132, 132, 132,
    132, 132, 132, 132, 133, 133, 133, 133, 133, 133, 133, 133, 134, 134, 134,
    134, 134, 134, 134, 134, 136, 136, 136, 136, 136, 136, 136, 136, 146, 146,
    146, 146, 146, 146, 146, 146, 154, 154, 154, 154, 154, 154, 154, 154, 156,
    156, 156, 156, 156, 156, 156, 156, 160, 160, 160, 160, 160, 160, 160, 160,
    163, 163, 163, 163, 163, 163, 163, 163, 164, 164, 164, 164, 164, 164, 164,
    164, 169, 169, 169, 169, 169, 169, 169, 169, 170, 170, 170, 170, 170, 170,
    170, 170, 173, 173, 173, 173, 173, 173, 173, 173, 178, 178, 178, 178, 178,
    178, 178, 178, 181, 181, 181, 181, 181, 181, 181, 181, 185, 185, 185, 185,
    185, 185, 185, 185, 186, 186, 186, 186, 186, 186, 186, 186, 187, 187, 187,
    187, 187, 187, 187, 187, 189, 189, 189, 189, 189, 189, 189, 189, 190, 190,
    190, 190, 190, 190, 190, 190, 196, 196, 196, 196, 196, 196, 196, 196, 198,
    198, 198, 198, 198, 198, 198, 198, 228, 228, 228, 228, 228, 228, 228, 228,
    232, 232, 232, 232, 232, 232, 232, 232, 233, 233, 233, 233, 233, 233, 233,
    233, 1,   1,   1,   1,   135, 135, 135, 135, 137, 137, 137, 137, 138, 138,
    138, 138, 139, 139, 139, 139, 140, 140, 140, 140, 141, 141, 141, 141, 143,
    143, 143, 143, 147, 147, 147, 147, 149, 149, 149, 149, 150, 150, 150, 150,
    151, 151, 151, 151, 152, 152, 152, 152, 155, 155, 155, 155, 157, 157, 157,
    157, 158, 158, 158, 158, 165, 165, 165, 165, 166, 166, 166, 166, 168, 168,
    168, 168, 174, 174, 174, 174, 175, 175, 175, 175, 180, 180, 180, 180, 182,
    182, 182, 182, 183, 183, 183, 183, 188, 188, 188, 188, 191, 191, 191, 191,
    197, 197, 197, 197, 231, 231, 231, 231, 239, 239, 239, 239, 9,   9,   142,
    142, 144, 144, 145, 145, 148, 148, 159, 159, 171, 171, 206, 206, 215, 215,
    225, 225, 236, 236, 237, 237, 199, 207, 234, 235, 192, 192, 192, 192, 192,
    192, 192, 192, 193, 193, 193, 193, 193, 193, 193, 193, 200, 200, 200, 200,
    200, 200, 200, 200, 201, 201, 201, 201, 201, 201, 201, 201, 202, 202, 202,
    202, 202, 202, 202, 202, 205, 205, 205, 205, 205, 205, 205, 205, 210, 210,
    210, 210, 210, 210, 210, 210, 213, 213, 213, 213, 213, 213, 213, 213, 218,
    218, 218, 218, 218, 218, 218, 218, 219, 219, 219, 219, 219, 219, 219, 219,
    238, 238, 238, 238, 238, 238, 238, 238, 240, 240, 240, 240, 240, 240, 240,
    240, 242, 242, 242, 242, 242, 242, 242, 242, 243, 243, 243, 243, 243, 243,
    243, 243, 255, 255, 255, 255, 255, 255, 255, 255, 203, 203, 203, 203, 204,
    204, 204, 204, 211, 211, 211, 211, 212, 212, 212, 212, 214, 214, 214, 214,
    221, 221, 221, 221, 222, 222, 222, 222, 223, 223, 223, 223, 241, 241, 241,
    241, 244, 244, 244, 244, 245, 245, 245, 245, 246, 246, 246, 246, 247, 247,
    247, 247, 248, 248, 248, 248, 250, 250, 250, 250, 251, 251, 251, 251, 252,
    252, 252, 252, 253, 253, 253, 253, 254, 254, 254, 254, 2,   2,   3,   3,
    4,   4,   5,   5,   6,   6,   7,   7,   8,   8,   11,  11,  12,  12,  14,
    14,  15,  15,  16,  16,  17,  17,  18,  18,  19,  19,  20,  20,  21,  21,
    23,  23,  24,  24,  25,  25,  26,  26,  27,  27,  28,  28,  29,  29,  30,
    30,  31,  31,  127, 127, 220, 220, 249, 249, -1,  -1,  10,  10,  10,  10,
    10,  10,  10,  10,  13,  13,  13,  13,  13,  13,  13,  13,  22,  22,  22,
    22,  22,  22,  22,  22,  256, 256, 256, 256, 256, 256, 256, 256, 45,  45,
    45,  45,  45,  45,  45,  45,  46,  46,  46,  46,  46,  46,  46,  46,  47,
    47,  47,  47,  47,  47,  47,  47,  51,  51,  51,  51,  51,  51,  51,  51,
    52,  52,  52,  52,  52,  52,  52,  52,  53,  53,  53,  53,  53,  53,  53,
    53,  54,  54,  54,  54,  54,  54,  54,  54,  55,  55,  55,  55,  55,  55,
    55,  55,  56,  56,  56,  56,  56,  56,  56,  56,  57,  57,  57,  57,  57,
    57,  57,  57,  50,  50,  50,  50,  50,  50,  50,  50,  97,  97,  97,  97,
    97,  97,  97,  97,  99,  99,  99,  99,  99,  99,  99,  99,  101, 101, 101,
    101, 101, 101, 101, 101, 105, 105, 105, 105, 105, 105, 105, 105, 111, 111,
    111, 111, 111, 111, 111, 111, 115, 115, 115, 115, 115, 115, 115, 115, 116,
    116, 116, 116, 116, 116, 116, 116, 32,  32,  32,  32,  37,  37,  37,  37,
    45,  45,  45,  45,  46,  46,  46,  46,  47,  47,  47,  47,  51,  51,  51,
    51,  52,  52,  52,  52,  53,  53,  53,  53,  54,  54,  54,  54,  55,  55,
    55,  55,  56,  56,  56,  56,  57,  57,  57,  57,  61,  61,  61,  61,  65,
    65,  65,  65,  95,  95,  95,  95,  98,  98,  98,  98,  100, 100, 100, 100,
    102, 102, 102, 102, 103, 103, 103, 103, 104, 104, 104, 104, 108, 108, 108,
    108, 109, 109, 109, 109, 110, 110, 110, 110, 112, 112, 112, 112, 114, 114,
    114, 114, 117, 117, 117, 117, 58,  58,  66,  66,  67,  67,  68,  68,  69,
    69,  70,  70,  71,  71,  72,  72,  73,  73,  74,  74,  75,  75,  76,  76,
    77,  77,  78,  78,  79,  79,  80,  80,  81,  81,  82,  82,  83,  83,  84,
    84,  85,  85,  86,  86,  87,  87,  89,  89,  106, 106, 107, 107, 113, 113,
    118, 118, 119, 119, 120, 120, 121, 121, 122, 122, 38,  42,  44,  59,  88,
    90,  -1,  -1,  33,  33,  33,  33,  34,  34,  34,  34,  40,  40,  40,  40,
    41,  41,  41,  41,  63,  63,  63,  63,  39,  39,  43,  43,  124, 124, 35,
    62,  -1,  -1,  -1,  -1,  0,   0,   0,   0,   0,   0,   0,   0,   36,  36,
    36,  36,  36,  36,  36,  36,  64,  64,  64,  64,  64,  64,  64,  64,  91,
    91,  91,  91,  91,  91,  91,  91,  93,  93,  93,  93,  93,  93,  93,  93,
    126, 126, 126, 126, 126, 126, 126, 126, 94,  94,  94,  94,  125, 125, 125,
    125, 60,  60,  96,  96,  123, 123, -1,  -1,  92,  92,  195, 195, 208, 208,
    128, 130, 131, 162, 184, 194, 224, 226, -1,  -1,  153, 153, 153, 153, 153,
    153, 153, 153, 161, 161, 161, 161, 161, 161, 161, 161, 167, 167, 167, 167,
    167, 167, 167, 167, 172, 172, 172, 172, 172, 172, 172, 172, 176, 176, 176,
    176, 176, 176, 176, 176, 177, 177, 177, 177, 177, 177, 177, 177, 179, 179,
    179, 179, 179, 179, 179, 179, 209, 209, 209, 209, 209, 209, 209, 209, 216,
    216, 216, 216, 216, 216, 216, 216, 217, 217, 217, 217, 217, 217, 217, 217,
    227, 227, 227, 227, 227, 227, 227, 227, 229, 229, 229, 229, 229, 229, 229,
    229, 230, 230, 230, 230, 230, 230, 230, 230, 129, 129, 129, 129, 132, 132,
    132, 132, 133, 133, 133, 133, 134, 134, 134, 134, 136, 136, 136, 136, 146,
    146, 146, 146, 154, 154, 154, 154, 156, 156, 156, 156, 160, 160, 160, 160,
    163, 163, 163, 163, 164, 164, 164, 164, 169, 169, 169, 169, 170, 170, 170,
    170, 173, 173, 173, 173, 178, 178, 178, 178, 181, 181, 181, 181, 185, 185,
    185, 185, 186, 186, 186, 186, 187, 187, 187, 187, 189, 189, 189, 189, 190,
    190, 190, 190, 196, 196, 196, 196, 198, 198, 198, 198, 228, 228, 228, 228,
    232, 232, 232, 232, 233, 233, 233, 233, 1,   1,   135, 135, 137, 137, 138,
    138, 139, 139, 140, 140, 141, 141, 143, 143, 147, 147, 149, 149, 150, 150,
    151, 151, 152, 152, 155, 155, 157, 157, 158, 158, 165, 165, 166, 166, 168,
    168, 174, 174, 175, 175, 180, 180, 182, 182, 183, 183, 188, 188, 191, 191,
    197, 197, 231, 231, 239, 239, 9,   142, 144, 145, 148, 159, 171, 206, 215,
    225, 236, 237, -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  199, 199,
    199, 199, 199, 199, 199, 199, 207, 207, 207, 207, 207, 207, 207, 207, 234,
    234, 234, 234, 234, 234, 234, 234, 235, 235, 235, 235, 235, 235, 235, 235,
    192, 192, 192, 192, 193, 193, 193, 193, 200, 200, 200, 200, 201, 201, 201,
    201, 202, 202, 202, 202, 205, 205, 205, 205, 210, 210, 210, 210, 213, 213,
    213, 213, 218, 218, 218, 218, 219, 219, 219, 219, 238, 238, 238, 238, 240,
    240, 240, 240, 242, 242, 242, 242, 243, 243, 243, 243, 255, 255, 255, 255,
    203, 203, 204, 204, 211, 211, 212, 212, 214, 214, 221, 221, 222, 222, 223,
    223, 241, 241, 244, 244, 245, 245, 246, 246, 247, 247, 248, 248, 250, 250,
    251, 251, 252, 252, 253, 253, 254, 254, 2,   3,   4,   5,   6,   7,   8,
    11,  12,  14,  15,  16,  17,  18,  19,  20,  21,  23,  24,  25,  26,  27,
    28,  29,  30,  31,  127, 220, 249, -1,  10,  10,  10,  10,  13,  13,  13,
    13,  22,  22,  22,  22,  256, 256, 256, 256,
};

static const gpr_uint8 inverse_base64[256] = {
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 62,  255,
    255, 255, 63,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  255, 255,
    255, 64,  255, 255, 255, 0,   1,   2,   3,   4,   5,   6,   7,   8,   9,
    10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,
    25,  255, 255, 255, 255, 255, 255, 26,  27,  28,  29,  30,  31,  32,  33,
    34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,
    49,  50,  51,  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255,
};

/* emission helpers */
static int on_hdr(grpc_chttp2_hpack_parser *p, grpc_mdelem *md,
                  int add_to_table) {
  if (add_to_table) {
    if (!grpc_chttp2_hptbl_add(&p->table, md)) {
      return 0;
    }
  }
  p->on_header(p->on_header_user_data, md);
  return 1;
}

static grpc_mdstr *take_string(grpc_chttp2_hpack_parser *p,
                               grpc_chttp2_hpack_parser_string *str) {
  grpc_mdstr *s = grpc_mdstr_from_buffer(p->table.mdctx, (gpr_uint8 *)str->str,
                                         str->length);
  str->length = 0;
  return s;
}

/* jump to the next state */
static int parse_next(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                      const gpr_uint8 *end) {
  p->state = *p->next_state++;
  return p->state(p, cur, end);
}

/* begin parsing a header: all functionality is encoded into lookup tables
   above */
static int parse_begin(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                       const gpr_uint8 *end) {
  if (cur == end) {
    p->state = parse_begin;
    return 1;
  }

  return first_byte_action[first_byte_lut[*cur]](p, cur, end);
}

/* stream dependency and prioritization data: we just skip it */
static int parse_stream_weight(grpc_chttp2_hpack_parser *p,
                               const gpr_uint8 *cur, const gpr_uint8 *end) {
  if (cur == end) {
    p->state = parse_stream_weight;
    return 1;
  }

  return p->after_prioritization(p, cur + 1, end);
}

static int parse_stream_dep3(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                             const gpr_uint8 *end) {
  if (cur == end) {
    p->state = parse_stream_dep3;
    return 1;
  }

  return parse_stream_weight(p, cur + 1, end);
}

static int parse_stream_dep2(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                             const gpr_uint8 *end) {
  if (cur == end) {
    p->state = parse_stream_dep2;
    return 1;
  }

  return parse_stream_dep3(p, cur + 1, end);
}

static int parse_stream_dep1(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                             const gpr_uint8 *end) {
  if (cur == end) {
    p->state = parse_stream_dep1;
    return 1;
  }

  return parse_stream_dep2(p, cur + 1, end);
}

static int parse_stream_dep0(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                             const gpr_uint8 *end) {
  if (cur == end) {
    p->state = parse_stream_dep0;
    return 1;
  }

  return parse_stream_dep1(p, cur + 1, end);
}

/* emit an indexed field; for now just logs it to console; jumps to
   begin the next field on completion */
static int finish_indexed_field(grpc_chttp2_hpack_parser *p,
                                const gpr_uint8 *cur, const gpr_uint8 *end) {
  grpc_mdelem *md = grpc_chttp2_hptbl_lookup(&p->table, p->index);
  if (md == NULL) {
    gpr_log(GPR_ERROR, "Invalid HPACK index received: %d", p->index);
    return 0;
  }
  GRPC_MDELEM_REF(md);
  return on_hdr(p, md, 0) && parse_begin(p, cur, end);
}

/* parse an indexed field with index < 127 */
static int parse_indexed_field(grpc_chttp2_hpack_parser *p,
                               const gpr_uint8 *cur, const gpr_uint8 *end) {
  p->index = (*cur) & 0x7f;
  return finish_indexed_field(p, cur + 1, end);
}

/* parse an indexed field with index >= 127 */
static int parse_indexed_field_x(grpc_chttp2_hpack_parser *p,
                                 const gpr_uint8 *cur, const gpr_uint8 *end) {
  static const grpc_chttp2_hpack_parser_state and_then[] = {
      finish_indexed_field};
  p->next_state = and_then;
  p->index = 0x7f;
  p->parsing.value = &p->index;
  return parse_value0(p, cur + 1, end);
}

/* finish a literal header with incremental indexing: just log, and jump to '
   begin */
static int finish_lithdr_incidx(grpc_chttp2_hpack_parser *p,
                                const gpr_uint8 *cur, const gpr_uint8 *end) {
  grpc_mdelem *md = grpc_chttp2_hptbl_lookup(&p->table, p->index);
  return on_hdr(p, grpc_mdelem_from_metadata_strings(p->table.mdctx,
                                                     GRPC_MDSTR_REF(md->key),
                                                     take_string(p, &p->value)),
                1) &&
         parse_begin(p, cur, end);
}

/* finish a literal header with incremental indexing with no index */
static int finish_lithdr_incidx_v(grpc_chttp2_hpack_parser *p,
                                  const gpr_uint8 *cur, const gpr_uint8 *end) {
  return on_hdr(p, grpc_mdelem_from_metadata_strings(p->table.mdctx,
                                                     take_string(p, &p->key),
                                                     take_string(p, &p->value)),
                1) &&
         parse_begin(p, cur, end);
}

/* parse a literal header with incremental indexing; index < 63 */
static int parse_lithdr_incidx(grpc_chttp2_hpack_parser *p,
                               const gpr_uint8 *cur, const gpr_uint8 *end) {
  static const grpc_chttp2_hpack_parser_state and_then[] = {
      parse_value_string_with_indexed_key, finish_lithdr_incidx};
  p->next_state = and_then;
  p->index = (*cur) & 0x3f;
  return parse_string_prefix(p, cur + 1, end);
}

/* parse a literal header with incremental indexing; index >= 63 */
static int parse_lithdr_incidx_x(grpc_chttp2_hpack_parser *p,
                                 const gpr_uint8 *cur, const gpr_uint8 *end) {
  static const grpc_chttp2_hpack_parser_state and_then[] = {
      parse_string_prefix, parse_value_string_with_indexed_key,
      finish_lithdr_incidx};
  p->next_state = and_then;
  p->index = 0x3f;
  p->parsing.value = &p->index;
  return parse_value0(p, cur + 1, end);
}

/* parse a literal header with incremental indexing; index = 0 */
static int parse_lithdr_incidx_v(grpc_chttp2_hpack_parser *p,
                                 const gpr_uint8 *cur, const gpr_uint8 *end) {
  static const grpc_chttp2_hpack_parser_state and_then[] = {
      parse_key_string, parse_string_prefix,
      parse_value_string_with_literal_key, finish_lithdr_incidx_v};
  p->next_state = and_then;
  return parse_string_prefix(p, cur + 1, end);
}

/* finish a literal header without incremental indexing */
static int finish_lithdr_notidx(grpc_chttp2_hpack_parser *p,
                                const gpr_uint8 *cur, const gpr_uint8 *end) {
  grpc_mdelem *md = grpc_chttp2_hptbl_lookup(&p->table, p->index);
  return on_hdr(p, grpc_mdelem_from_metadata_strings(p->table.mdctx,
                                                     GRPC_MDSTR_REF(md->key),
                                                     take_string(p, &p->value)),
                0) &&
         parse_begin(p, cur, end);
}

/* finish a literal header without incremental indexing with index = 0 */
static int finish_lithdr_notidx_v(grpc_chttp2_hpack_parser *p,
                                  const gpr_uint8 *cur, const gpr_uint8 *end) {
  return on_hdr(p, grpc_mdelem_from_metadata_strings(p->table.mdctx,
                                                     take_string(p, &p->key),
                                                     take_string(p, &p->value)),
                0) &&
         parse_begin(p, cur, end);
}

/* parse a literal header without incremental indexing; index < 15 */
static int parse_lithdr_notidx(grpc_chttp2_hpack_parser *p,
                               const gpr_uint8 *cur, const gpr_uint8 *end) {
  static const grpc_chttp2_hpack_parser_state and_then[] = {
      parse_value_string_with_indexed_key, finish_lithdr_notidx};
  p->next_state = and_then;
  p->index = (*cur) & 0xf;
  return parse_string_prefix(p, cur + 1, end);
}

/* parse a literal header without incremental indexing; index >= 15 */
static int parse_lithdr_notidx_x(grpc_chttp2_hpack_parser *p,
                                 const gpr_uint8 *cur, const gpr_uint8 *end) {
  static const grpc_chttp2_hpack_parser_state and_then[] = {
      parse_string_prefix, parse_value_string_with_indexed_key,
      finish_lithdr_notidx};
  p->next_state = and_then;
  p->index = 0xf;
  p->parsing.value = &p->index;
  return parse_value0(p, cur + 1, end);
}

/* parse a literal header without incremental indexing; index == 0 */
static int parse_lithdr_notidx_v(grpc_chttp2_hpack_parser *p,
                                 const gpr_uint8 *cur, const gpr_uint8 *end) {
  static const grpc_chttp2_hpack_parser_state and_then[] = {
      parse_key_string, parse_string_prefix,
      parse_value_string_with_literal_key, finish_lithdr_notidx_v};
  p->next_state = and_then;
  return parse_string_prefix(p, cur + 1, end);
}

/* finish a literal header that is never indexed */
static int finish_lithdr_nvridx(grpc_chttp2_hpack_parser *p,
                                const gpr_uint8 *cur, const gpr_uint8 *end) {
  grpc_mdelem *md = grpc_chttp2_hptbl_lookup(&p->table, p->index);
  return on_hdr(p, grpc_mdelem_from_metadata_strings(p->table.mdctx,
                                                     GRPC_MDSTR_REF(md->key),
                                                     take_string(p, &p->value)),
                0) &&
         parse_begin(p, cur, end);
}

/* finish a literal header that is never indexed with an extra value */
static int finish_lithdr_nvridx_v(grpc_chttp2_hpack_parser *p,
                                  const gpr_uint8 *cur, const gpr_uint8 *end) {
  return on_hdr(p, grpc_mdelem_from_metadata_strings(p->table.mdctx,
                                                     take_string(p, &p->key),
                                                     take_string(p, &p->value)),
                0) &&
         parse_begin(p, cur, end);
}

/* parse a literal header that is never indexed; index < 15 */
static int parse_lithdr_nvridx(grpc_chttp2_hpack_parser *p,
                               const gpr_uint8 *cur, const gpr_uint8 *end) {
  static const grpc_chttp2_hpack_parser_state and_then[] = {
      parse_value_string_with_indexed_key, finish_lithdr_nvridx};
  p->next_state = and_then;
  p->index = (*cur) & 0xf;
  return parse_string_prefix(p, cur + 1, end);
}

/* parse a literal header that is never indexed; index >= 15 */
static int parse_lithdr_nvridx_x(grpc_chttp2_hpack_parser *p,
                                 const gpr_uint8 *cur, const gpr_uint8 *end) {
  static const grpc_chttp2_hpack_parser_state and_then[] = {
      parse_string_prefix, parse_value_string_with_indexed_key,
      finish_lithdr_nvridx};
  p->next_state = and_then;
  p->index = 0xf;
  p->parsing.value = &p->index;
  return parse_value0(p, cur + 1, end);
}

/* parse a literal header that is never indexed; index == 0 */
static int parse_lithdr_nvridx_v(grpc_chttp2_hpack_parser *p,
                                 const gpr_uint8 *cur, const gpr_uint8 *end) {
  static const grpc_chttp2_hpack_parser_state and_then[] = {
      parse_key_string, parse_string_prefix,
      parse_value_string_with_literal_key, finish_lithdr_nvridx_v};
  p->next_state = and_then;
  return parse_string_prefix(p, cur + 1, end);
}

/* finish parsing a max table size change */
static int finish_max_tbl_size(grpc_chttp2_hpack_parser *p,
                               const gpr_uint8 *cur, const gpr_uint8 *end) {
  gpr_log(GPR_INFO, "MAX TABLE SIZE: %d", p->index);
  return grpc_chttp2_hptbl_set_current_table_size(&p->table, p->index) &&
         parse_begin(p, cur, end);
}

/* parse a max table size change, max size < 15 */
static int parse_max_tbl_size(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                              const gpr_uint8 *end) {
  p->index = (*cur) & 0x1f;
  return finish_max_tbl_size(p, cur + 1, end);
}

/* parse a max table size change, max size >= 15 */
static int parse_max_tbl_size_x(grpc_chttp2_hpack_parser *p,
                                const gpr_uint8 *cur, const gpr_uint8 *end) {
  static const grpc_chttp2_hpack_parser_state and_then[] = {
      finish_max_tbl_size};
  p->next_state = and_then;
  p->index = 0x1f;
  p->parsing.value = &p->index;
  return parse_value0(p, cur + 1, end);
}

/* a parse error: jam the parse state into parse_error, and return error */
static int parse_error(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                       const gpr_uint8 *end) {
  p->state = parse_error;
  return 0;
}

static int parse_illegal_op(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                            const gpr_uint8 *end) {
  GPR_ASSERT(cur != end);
  gpr_log(GPR_DEBUG, "Illegal hpack op code %d", *cur);
  return parse_error(p, cur, end);
}

/* parse the 1st byte of a varint into p->parsing.value
   no overflow is possible */
static int parse_value0(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                        const gpr_uint8 *end) {
  if (cur == end) {
    p->state = parse_value0;
    return 1;
  }

  *p->parsing.value += (*cur) & 0x7f;

  if ((*cur) & 0x80) {
    return parse_value1(p, cur + 1, end);
  } else {
    return parse_next(p, cur + 1, end);
  }
}

/* parse the 2nd byte of a varint into p->parsing.value
   no overflow is possible */
static int parse_value1(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                        const gpr_uint8 *end) {
  if (cur == end) {
    p->state = parse_value1;
    return 1;
  }

  *p->parsing.value += (((gpr_uint32)*cur) & 0x7f) << 7;

  if ((*cur) & 0x80) {
    return parse_value2(p, cur + 1, end);
  } else {
    return parse_next(p, cur + 1, end);
  }
}

/* parse the 3rd byte of a varint into p->parsing.value
   no overflow is possible */
static int parse_value2(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                        const gpr_uint8 *end) {
  if (cur == end) {
    p->state = parse_value2;
    return 1;
  }

  *p->parsing.value += (((gpr_uint32)*cur) & 0x7f) << 14;

  if ((*cur) & 0x80) {
    return parse_value3(p, cur + 1, end);
  } else {
    return parse_next(p, cur + 1, end);
  }
}

/* parse the 4th byte of a varint into p->parsing.value
   no overflow is possible */
static int parse_value3(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                        const gpr_uint8 *end) {
  if (cur == end) {
    p->state = parse_value3;
    return 1;
  }

  *p->parsing.value += (((gpr_uint32)*cur) & 0x7f) << 21;

  if ((*cur) & 0x80) {
    return parse_value4(p, cur + 1, end);
  } else {
    return parse_next(p, cur + 1, end);
  }
}

/* parse the 5th byte of a varint into p->parsing.value
   depending on the byte, we may overflow, and care must be taken */
static int parse_value4(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                        const gpr_uint8 *end) {
  gpr_uint8 c;
  gpr_uint32 cur_value;
  gpr_uint32 add_value;

  if (cur == end) {
    p->state = parse_value4;
    return 1;
  }

  c = (*cur) & 0x7f;
  if (c > 0xf) {
    goto error;
  }

  cur_value = *p->parsing.value;
  add_value = ((gpr_uint32)c) << 28;
  if (add_value > 0xffffffffu - cur_value) {
    goto error;
  }

  *p->parsing.value = cur_value + add_value;

  if ((*cur) & 0x80) {
    return parse_value5up(p, cur + 1, end);
  } else {
    return parse_next(p, cur + 1, end);
  }

error:
  gpr_log(GPR_ERROR,
          "integer overflow in hpack integer decoding: have 0x%08x, "
          "got byte 0x%02x",
          *p->parsing.value, *cur);
  return parse_error(p, cur, end);
}

/* parse any trailing bytes in a varint: it's possible to append an arbitrary
   number of 0x80's and not affect the value - a zero will terminate - and
   anything else will overflow */
static int parse_value5up(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                          const gpr_uint8 *end) {
  while (cur != end && *cur == 0x80) {
    ++cur;
  }

  if (cur == end) {
    p->state = parse_value5up;
    return 1;
  }

  if (*cur == 0) {
    return parse_next(p, cur + 1, end);
  }

  gpr_log(GPR_ERROR,
          "integer overflow in hpack integer decoding: have 0x%08x, "
          "got byte 0x%02x sometime after byte 4");
  return parse_error(p, cur, end);
}

/* parse a string prefix */
static int parse_string_prefix(grpc_chttp2_hpack_parser *p,
                               const gpr_uint8 *cur, const gpr_uint8 *end) {
  if (cur == end) {
    p->state = parse_string_prefix;
    return 1;
  }

  p->strlen = (*cur) & 0x7f;
  p->huff = (*cur) >> 7;
  if (p->strlen == 0x7f) {
    p->parsing.value = &p->strlen;
    return parse_value0(p, cur + 1, end);
  } else {
    return parse_next(p, cur + 1, end);
  }
}

/* append some bytes to a string */
static void append_bytes(grpc_chttp2_hpack_parser_string *str,
                         const gpr_uint8 *data, size_t length) {
  if (length + str->length > str->capacity) {
    GPR_ASSERT(str->length + length <= GPR_UINT32_MAX);
    str->capacity = (gpr_uint32)(str->length + length);
    str->str = gpr_realloc(str->str, str->capacity);
  }
  memcpy(str->str + str->length, data, length);
  GPR_ASSERT(length <= GPR_UINT32_MAX - str->length);
  str->length += (gpr_uint32)length;
}

static int append_string(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                         const gpr_uint8 *end) {
  grpc_chttp2_hpack_parser_string *str = p->parsing.str;
  gpr_uint32 bits;
  gpr_uint8 decoded[3];
  switch ((binary_state)p->binary) {
    case NOT_BINARY:
      append_bytes(str, cur, (size_t)(end - cur));
      return 1;
    b64_byte0:
    case B64_BYTE0:
      if (cur == end) {
        p->binary = B64_BYTE0;
        return 1;
      }
      bits = inverse_base64[*cur];
      ++cur;
      if (bits == 255)
        return 0;
      else if (bits == 64)
        goto b64_byte0;
      p->base64_buffer = bits << 18;
    /* fallthrough */
    b64_byte1:
    case B64_BYTE1:
      if (cur == end) {
        p->binary = B64_BYTE1;
        return 1;
      }
      bits = inverse_base64[*cur];
      ++cur;
      if (bits == 255)
        return 0;
      else if (bits == 64)
        goto b64_byte1;
      p->base64_buffer |= bits << 12;
    /* fallthrough */
    b64_byte2:
    case B64_BYTE2:
      if (cur == end) {
        p->binary = B64_BYTE2;
        return 1;
      }
      bits = inverse_base64[*cur];
      ++cur;
      if (bits == 255)
        return 0;
      else if (bits == 64)
        goto b64_byte2;
      p->base64_buffer |= bits << 6;
    /* fallthrough */
    b64_byte3:
    case B64_BYTE3:
      if (cur == end) {
        p->binary = B64_BYTE3;
        return 1;
      }
      bits = inverse_base64[*cur];
      ++cur;
      if (bits == 255)
        return 0;
      else if (bits == 64)
        goto b64_byte3;
      p->base64_buffer |= bits;
      bits = p->base64_buffer;
      decoded[0] = (gpr_uint8)(bits >> 16);
      decoded[1] = (gpr_uint8)(bits >> 8);
      decoded[2] = (gpr_uint8)(bits);
      append_bytes(str, decoded, 3);
      goto b64_byte0;
  }
  GPR_UNREACHABLE_CODE(return 1);
}

/* append a null terminator to a string */
static int finish_str(grpc_chttp2_hpack_parser *p) {
  gpr_uint8 terminator = 0;
  gpr_uint8 decoded[2];
  gpr_uint32 bits;
  grpc_chttp2_hpack_parser_string *str = p->parsing.str;
  switch ((binary_state)p->binary) {
    case NOT_BINARY:
      break;
    case B64_BYTE0:
      break;
    case B64_BYTE1:
      gpr_log(GPR_ERROR, "illegal base64 encoding");
      return 0; /* illegal encoding */
    case B64_BYTE2:
      bits = p->base64_buffer;
      if (bits & 0xffff) {
        gpr_log(GPR_ERROR, "trailing bits in base64 encoding: 0x%04x",
                bits & 0xffff);
        return 0;
      }
      decoded[0] = (gpr_uint8)(bits >> 16);
      append_bytes(str, decoded, 1);
      break;
    case B64_BYTE3:
      bits = p->base64_buffer;
      if (bits & 0xff) {
        gpr_log(GPR_ERROR, "trailing bits in base64 encoding: 0x%02x",
                bits & 0xff);
        return 0;
      }
      decoded[0] = (gpr_uint8)(bits >> 16);
      decoded[1] = (gpr_uint8)(bits >> 8);
      append_bytes(str, decoded, 2);
      break;
  }
  append_bytes(str, &terminator, 1);
  p->parsing.str->length--; /* don't actually count the null terminator */
  return 1;
}

/* decode a nibble from a huffman encoded stream */
static int huff_nibble(grpc_chttp2_hpack_parser *p, gpr_uint8 nibble) {
  gpr_int16 emit = emit_sub_tbl[16 * emit_tbl[p->huff_state] + nibble];
  gpr_int16 next = next_sub_tbl[16 * next_tbl[p->huff_state] + nibble];
  if (emit != -1) {
    if (emit >= 0 && emit < 256) {
      gpr_uint8 c = (gpr_uint8)emit;
      if (!append_string(p, &c, (&c) + 1)) return 0;
    } else {
      assert(emit == 256);
    }
  }
  p->huff_state = next;
  return 1;
}

/* decode full bytes from a huffman encoded stream */
static int add_huff_bytes(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                          const gpr_uint8 *end) {
  for (; cur != end; ++cur) {
    if (!huff_nibble(p, *cur >> 4) || !huff_nibble(p, *cur & 0xf)) return 0;
  }
  return 1;
}

/* decode some string bytes based on the current decoding mode
   (huffman or not) */
static int add_str_bytes(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                         const gpr_uint8 *end) {
  if (p->huff) {
    return add_huff_bytes(p, cur, end);
  } else {
    return append_string(p, cur, end);
  }
}

/* parse a string - tries to do large chunks at a time */
static int parse_string(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                        const gpr_uint8 *end) {
  size_t remaining = p->strlen - p->strgot;
  size_t given = (size_t)(end - cur);
  if (remaining <= given) {
    return add_str_bytes(p, cur, cur + remaining) && finish_str(p) &&
           parse_next(p, cur + remaining, end);
  } else {
    if (!add_str_bytes(p, cur, cur + given)) return 0;
    GPR_ASSERT(given <= GPR_UINT32_MAX - p->strgot);
    p->strgot += (gpr_uint32)given;
    p->state = parse_string;
    return 1;
  }
}

/* begin parsing a string - performs setup, calls parse_string */
static int begin_parse_string(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                              const gpr_uint8 *end, gpr_uint8 binary,
                              grpc_chttp2_hpack_parser_string *str) {
  p->strgot = 0;
  str->length = 0;
  p->parsing.str = str;
  p->huff_state = 0;
  p->binary = binary;
  return parse_string(p, cur, end);
}

/* parse the key string */
static int parse_key_string(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                            const gpr_uint8 *end) {
  return begin_parse_string(p, cur, end, NOT_BINARY, &p->key);
}

/* check if a key represents a binary header or not */
typedef enum { BINARY_HEADER, PLAINTEXT_HEADER, ERROR_HEADER } is_binary_header;

static is_binary_header is_binary_literal_header(grpc_chttp2_hpack_parser *p) {
  return grpc_is_binary_header(p->key.str, p->key.length) ? BINARY_HEADER
                                                          : PLAINTEXT_HEADER;
}

static is_binary_header is_binary_indexed_header(grpc_chttp2_hpack_parser *p) {
  grpc_mdelem *elem = grpc_chttp2_hptbl_lookup(&p->table, p->index);
  if (!elem) return ERROR_HEADER;
  return grpc_is_binary_header(
             (const char *)GPR_SLICE_START_PTR(elem->key->slice),
             GPR_SLICE_LENGTH(elem->key->slice))
             ? BINARY_HEADER
             : PLAINTEXT_HEADER;
}

/* parse the value string */
static int parse_value_string(grpc_chttp2_hpack_parser *p, const gpr_uint8 *cur,
                              const gpr_uint8 *end, is_binary_header type) {
  switch (type) {
    case BINARY_HEADER:
      return begin_parse_string(p, cur, end, B64_BYTE0, &p->value);
    case PLAINTEXT_HEADER:
      return begin_parse_string(p, cur, end, NOT_BINARY, &p->value);
    case ERROR_HEADER:
      return 0;
  }
  /* Add code to prevent return without value error */
  GPR_UNREACHABLE_CODE(return 0);
}

static int parse_value_string_with_indexed_key(grpc_chttp2_hpack_parser *p,
                                               const gpr_uint8 *cur,
                                               const gpr_uint8 *end) {
  return parse_value_string(p, cur, end, is_binary_indexed_header(p));
}

static int parse_value_string_with_literal_key(grpc_chttp2_hpack_parser *p,
                                               const gpr_uint8 *cur,
                                               const gpr_uint8 *end) {
  return parse_value_string(p, cur, end, is_binary_literal_header(p));
}

/* PUBLIC INTERFACE */

static void on_header_not_set(void *user_data, grpc_mdelem *md) {
  char *keyhex = gpr_dump_slice(md->key->slice, GPR_DUMP_HEX | GPR_DUMP_ASCII);
  char *valuehex =
      gpr_dump_slice(md->value->slice, GPR_DUMP_HEX | GPR_DUMP_ASCII);
  gpr_log(GPR_ERROR, "on_header callback not set; key=%s value=%s", keyhex,
          valuehex);
  gpr_free(keyhex);
  gpr_free(valuehex);
  GRPC_MDELEM_UNREF(md);
  abort();
}

void grpc_chttp2_hpack_parser_init(grpc_chttp2_hpack_parser *p,
                                   grpc_mdctx *mdctx) {
  p->on_header = on_header_not_set;
  p->on_header_user_data = NULL;
  p->state = parse_begin;
  p->key.str = NULL;
  p->key.capacity = 0;
  p->key.length = 0;
  p->value.str = NULL;
  p->value.capacity = 0;
  p->value.length = 0;
  grpc_chttp2_hptbl_init(&p->table, mdctx);
}

void grpc_chttp2_hpack_parser_set_has_priority(grpc_chttp2_hpack_parser *p) {
  p->after_prioritization = p->state;
  p->state = parse_stream_dep0;
}

void grpc_chttp2_hpack_parser_destroy(grpc_chttp2_hpack_parser *p) {
  grpc_chttp2_hptbl_destroy(&p->table);
  gpr_free(p->key.str);
  gpr_free(p->value.str);
}

int grpc_chttp2_hpack_parser_parse(grpc_chttp2_hpack_parser *p,
                                   const gpr_uint8 *beg, const gpr_uint8 *end) {
  /* TODO(ctiller): limit the distance of end from beg, and perform multiple
     steps in the event of a large chunk of data to limit
     stack space usage when no tail call optimization is
     available */
  return p->state(p, beg, end);
}

grpc_chttp2_parse_error grpc_chttp2_header_parser_parse(
    grpc_exec_ctx *exec_ctx, void *hpack_parser,
    grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_parsing *stream_parsing, gpr_slice slice, int is_last) {
  grpc_chttp2_hpack_parser *parser = hpack_parser;
  GPR_TIMER_BEGIN("grpc_chttp2_hpack_parser_parse", 0);
  if (!grpc_chttp2_hpack_parser_parse(parser, GPR_SLICE_START_PTR(slice),
                                      GPR_SLICE_END_PTR(slice))) {
    GPR_TIMER_END("grpc_chttp2_hpack_parser_parse", 0);
    return GRPC_CHTTP2_CONNECTION_ERROR;
  }
  if (is_last) {
    if (parser->is_boundary && parser->state != parse_begin) {
      gpr_log(GPR_ERROR,
              "end of header frame not aligned with a hpack record boundary");
      GPR_TIMER_END("grpc_chttp2_hpack_parser_parse", 0);
      return GRPC_CHTTP2_CONNECTION_ERROR;
    }
    if (parser->is_boundary) {
      stream_parsing
          ->got_metadata_on_parse[stream_parsing->header_frames_received] = 1;
      stream_parsing->header_frames_received++;
      grpc_chttp2_list_add_parsing_seen_stream(transport_parsing,
                                               stream_parsing);
    }
    if (parser->is_eof) {
      stream_parsing->received_close = 1;
    }
    parser->on_header = on_header_not_set;
    parser->on_header_user_data = NULL;
    parser->is_boundary = 0xde;
    parser->is_eof = 0xde;
  }
  GPR_TIMER_END("grpc_chttp2_hpack_parser_parse", 0);
  return GRPC_CHTTP2_PARSE_OK;
}
