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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FRAME_DATA_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FRAME_DATA_H

/* Parser for GRPC streams embedded in DATA frames */

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/transport/byte_stream.h"
#include "src/core/lib/transport/transport.h"

typedef enum {
  GRPC_CHTTP2_DATA_FH_0,
  GRPC_CHTTP2_DATA_FH_1,
  GRPC_CHTTP2_DATA_FH_2,
  GRPC_CHTTP2_DATA_FH_3,
  GRPC_CHTTP2_DATA_FH_4,
  GRPC_CHTTP2_DATA_FRAME,
  GRPC_CHTTP2_DATA_ERROR
} grpc_chttp2_stream_state;

typedef struct grpc_chttp2_incoming_byte_stream
    grpc_chttp2_incoming_byte_stream;

typedef struct {
  grpc_chttp2_stream_state state;
  uint8_t frame_type;
  uint32_t frame_size;
  grpc_error *error;

  bool is_frame_compressed;
  grpc_chttp2_incoming_byte_stream *parsing_frame;
} grpc_chttp2_data_parser;

/* initialize per-stream state for data frame parsing */
grpc_error *grpc_chttp2_data_parser_init(grpc_chttp2_data_parser *parser);

void grpc_chttp2_data_parser_destroy(grpc_exec_ctx *exec_ctx,
                                     grpc_chttp2_data_parser *parser);

/* start processing a new data frame */
grpc_error *grpc_chttp2_data_parser_begin_frame(grpc_chttp2_data_parser *parser,
                                                uint8_t flags,
                                                uint32_t stream_id,
                                                grpc_chttp2_stream *s);

/* handle a slice of a data frame - is_last indicates the last slice of a
   frame */
grpc_error *grpc_chttp2_data_parser_parse(grpc_exec_ctx *exec_ctx, void *parser,
                                          grpc_chttp2_transport *t,
                                          grpc_chttp2_stream *s,
                                          grpc_slice slice, int is_last);

void grpc_chttp2_encode_data(uint32_t id, grpc_slice_buffer *inbuf,
                             uint32_t write_bytes, int is_eof,
                             grpc_transport_one_way_stats *stats,
                             grpc_slice_buffer *outbuf);

grpc_error *grpc_deframe_unprocessed_incoming_frames(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_data_parser *p, grpc_chttp2_stream *s,
    grpc_slice_buffer *slices, grpc_slice *slice_out,
    grpc_byte_stream **stream_out);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FRAME_DATA_H */
