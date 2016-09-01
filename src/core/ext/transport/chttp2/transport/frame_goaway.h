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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FRAME_GOAWAY_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FRAME_GOAWAY_H

#include <grpc/support/port_platform.h>
#include <grpc/support/slice.h>
#include <grpc/support/slice_buffer.h>
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/lib/iomgr/exec_ctx.h"

typedef enum {
  GRPC_CHTTP2_GOAWAY_LSI0,
  GRPC_CHTTP2_GOAWAY_LSI1,
  GRPC_CHTTP2_GOAWAY_LSI2,
  GRPC_CHTTP2_GOAWAY_LSI3,
  GRPC_CHTTP2_GOAWAY_ERR0,
  GRPC_CHTTP2_GOAWAY_ERR1,
  GRPC_CHTTP2_GOAWAY_ERR2,
  GRPC_CHTTP2_GOAWAY_ERR3,
  GRPC_CHTTP2_GOAWAY_DEBUG
} grpc_chttp2_goaway_parse_state;

typedef struct {
  grpc_chttp2_goaway_parse_state state;
  uint32_t last_stream_id;
  uint32_t error_code;
  char *debug_data;
  uint32_t debug_length;
  uint32_t debug_pos;
} grpc_chttp2_goaway_parser;

void grpc_chttp2_goaway_parser_init(grpc_chttp2_goaway_parser *p);
void grpc_chttp2_goaway_parser_destroy(grpc_chttp2_goaway_parser *p);
grpc_error *grpc_chttp2_goaway_parser_begin_frame(
    grpc_chttp2_goaway_parser *parser, uint32_t length, uint8_t flags);
grpc_error *grpc_chttp2_goaway_parser_parse(
    grpc_exec_ctx *exec_ctx, void *parser,
    grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_parsing *stream_parsing, gpr_slice slice, int is_last);

void grpc_chttp2_goaway_append(uint32_t last_stream_id, uint32_t error_code,
                               gpr_slice debug_data,
                               gpr_slice_buffer *slice_buffer);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FRAME_GOAWAY_H */
