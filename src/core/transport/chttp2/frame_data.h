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

#ifndef GRPC_INTERNAL_CORE_TRANSPORT_CHTTP2_FRAME_DATA_H
#define GRPC_INTERNAL_CORE_TRANSPORT_CHTTP2_FRAME_DATA_H

/* Parser for GRPC streams embedded in DATA frames */

#include <grpc/support/slice.h>
#include <grpc/support/slice_buffer.h>
#include "src/core/transport/stream_op.h"
#include "src/core/transport/chttp2/frame.h"

typedef enum {
  GRPC_CHTTP2_DATA_FH_0,
  GRPC_CHTTP2_DATA_FH_1,
  GRPC_CHTTP2_DATA_FH_2,
  GRPC_CHTTP2_DATA_FH_3,
  GRPC_CHTTP2_DATA_FH_4,
  GRPC_CHTTP2_DATA_FRAME
} grpc_chttp2_stream_state;

typedef struct {
  grpc_chttp2_stream_state state;
  gpr_uint8 is_last_frame;
  gpr_uint8 frame_type;
  gpr_uint32 frame_size;

  int is_frame_compressed;
  grpc_stream_op_buffer incoming_sopb;
} grpc_chttp2_data_parser;

/* initialize per-stream state for data frame parsing */
grpc_chttp2_parse_error grpc_chttp2_data_parser_init(
    grpc_chttp2_data_parser *parser);

void grpc_chttp2_data_parser_destroy(grpc_chttp2_data_parser *parser);

/* start processing a new data frame */
grpc_chttp2_parse_error grpc_chttp2_data_parser_begin_frame(
    grpc_chttp2_data_parser *parser, gpr_uint8 flags);

/* handle a slice of a data frame - is_last indicates the last slice of a
   frame */
grpc_chttp2_parse_error grpc_chttp2_data_parser_parse(
    void *parser, grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_parsing *stream_parsing, gpr_slice slice, int is_last);

/* create a slice with an empty data frame and is_last set */
gpr_slice grpc_chttp2_data_frame_create_empty_close(gpr_uint32 id);

#endif /* GRPC_INTERNAL_CORE_TRANSPORT_CHTTP2_FRAME_DATA_H */
