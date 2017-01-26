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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_PARSER_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_PARSER_H

#include <stddef.h>

#include <grpc/support/port_platform.h>
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/hpack_table.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/transport/metadata.h"

typedef struct grpc_chttp2_hpack_parser grpc_chttp2_hpack_parser;

typedef grpc_error *(*grpc_chttp2_hpack_parser_state)(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_hpack_parser *p, const uint8_t *beg,
    const uint8_t *end);

typedef struct {
  char *str;
  uint32_t length;
  uint32_t capacity;
} grpc_chttp2_hpack_parser_string;

struct grpc_chttp2_hpack_parser {
  /* user specified callback for each header output */
  void (*on_header)(grpc_exec_ctx *exec_ctx, void *user_data, grpc_mdelem *md);
  void *on_header_user_data;

  grpc_error *last_error;

  /* current parse state - or a function that implements it */
  grpc_chttp2_hpack_parser_state state;
  /* future states dependent on the opening op code */
  const grpc_chttp2_hpack_parser_state *next_state;
  /* what to do after skipping prioritization data */
  grpc_chttp2_hpack_parser_state after_prioritization;
  /* the value we're currently parsing */
  union {
    uint32_t *value;
    grpc_chttp2_hpack_parser_string *str;
  } parsing;
  /* string parameters for each chunk */
  grpc_chttp2_hpack_parser_string key;
  grpc_chttp2_hpack_parser_string value;
  /* parsed index */
  uint32_t index;
  /* length of source bytes for the currently parsing string */
  uint32_t strlen;
  /* number of source bytes read for the currently parsing string */
  uint32_t strgot;
  /* huffman decoding state */
  int16_t huff_state;
  /* is the string being decoded binary? */
  uint8_t binary;
  /* is the current string huffman encoded? */
  uint8_t huff;
  /* is a dynamic table update allowed? */
  uint8_t dynamic_table_update_allowed;
  /* set by higher layers, used by grpc_chttp2_header_parser_parse to signal
     it should append a metadata boundary at the end of frame */
  uint8_t is_boundary;
  uint8_t is_eof;
  uint32_t base64_buffer;

  /* hpack table */
  grpc_chttp2_hptbl table;
};

void grpc_chttp2_hpack_parser_init(grpc_exec_ctx *exec_ctx,
                                   grpc_chttp2_hpack_parser *p);
void grpc_chttp2_hpack_parser_destroy(grpc_exec_ctx *exec_ctx,
                                      grpc_chttp2_hpack_parser *p);

void grpc_chttp2_hpack_parser_set_has_priority(grpc_chttp2_hpack_parser *p);

/* returns 1 on success, 0 on error */
grpc_error *grpc_chttp2_hpack_parser_parse(grpc_exec_ctx *exec_ctx,
                                           grpc_chttp2_hpack_parser *p,
                                           const uint8_t *beg,
                                           const uint8_t *end);

/* wraps grpc_chttp2_hpack_parser_parse to provide a frame level parser for
   the transport */
grpc_error *grpc_chttp2_header_parser_parse(grpc_exec_ctx *exec_ctx,
                                            void *hpack_parser,
                                            grpc_chttp2_transport *t,
                                            grpc_chttp2_stream *s,
                                            grpc_slice slice, int is_last);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_PARSER_H */
