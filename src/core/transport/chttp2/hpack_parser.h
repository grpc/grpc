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

#ifndef GRPC_INTERNAL_CORE_TRANSPORT_CHTTP2_HPACK_PARSER_H
#define GRPC_INTERNAL_CORE_TRANSPORT_CHTTP2_HPACK_PARSER_H

#include <stddef.h>

#include <grpc/support/port_platform.h>
#include "src/core/iomgr/exec_ctx.h"
#include "src/core/transport/chttp2/frame.h"
#include "src/core/transport/chttp2/hpack_table.h"
#include "src/core/transport/metadata.h"

typedef struct grpc_chttp2_hpack_parser grpc_chttp2_hpack_parser;

typedef int (*grpc_chttp2_hpack_parser_state)(grpc_chttp2_hpack_parser *p,
                                              const gpr_uint8 *beg,
                                              const gpr_uint8 *end);

typedef struct {
  char *str;
  gpr_uint32 length;
  gpr_uint32 capacity;
} grpc_chttp2_hpack_parser_string;

struct grpc_chttp2_hpack_parser {
  /* user specified callback for each header output */
  void (*on_header)(void *user_data, grpc_mdelem *md);
  void *on_header_user_data;

  /* current parse state - or a function that implements it */
  grpc_chttp2_hpack_parser_state state;
  /* future states dependent on the opening op code */
  const grpc_chttp2_hpack_parser_state *next_state;
  /* what to do after skipping prioritization data */
  grpc_chttp2_hpack_parser_state after_prioritization;
  /* the value we're currently parsing */
  union {
    gpr_uint32 *value;
    grpc_chttp2_hpack_parser_string *str;
  } parsing;
  /* string parameters for each chunk */
  grpc_chttp2_hpack_parser_string key;
  grpc_chttp2_hpack_parser_string value;
  /* parsed index */
  gpr_uint32 index;
  /* length of source bytes for the currently parsing string */
  gpr_uint32 strlen;
  /* number of source bytes read for the currently parsing string */
  gpr_uint32 strgot;
  /* huffman decoding state */
  gpr_int16 huff_state;
  /* is the string being decoded binary? */
  gpr_uint8 binary;
  /* is the current string huffman encoded? */
  gpr_uint8 huff;
  /* set by higher layers, used by grpc_chttp2_header_parser_parse to signal
     it should append a metadata boundary at the end of frame */
  gpr_uint8 is_boundary;
  gpr_uint8 is_eof;
  gpr_uint32 base64_buffer;

  /* hpack table */
  grpc_chttp2_hptbl table;
};

void grpc_chttp2_hpack_parser_init(grpc_chttp2_hpack_parser *p,
                                   grpc_mdctx *mdctx);
void grpc_chttp2_hpack_parser_destroy(grpc_chttp2_hpack_parser *p);

void grpc_chttp2_hpack_parser_set_has_priority(grpc_chttp2_hpack_parser *p);

/* returns 1 on success, 0 on error */
int grpc_chttp2_hpack_parser_parse(grpc_chttp2_hpack_parser *p,
                                   const gpr_uint8 *beg, const gpr_uint8 *end);

/* wraps grpc_chttp2_hpack_parser_parse to provide a frame level parser for
   the transport */
grpc_chttp2_parse_error grpc_chttp2_header_parser_parse(
    grpc_exec_ctx *exec_ctx, void *hpack_parser,
    grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_parsing *stream_parsing, gpr_slice slice, int is_last);

#endif /* GRPC_INTERNAL_CORE_TRANSPORT_CHTTP2_HPACK_PARSER_H */
