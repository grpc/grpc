/*
 *
 * Copyright 2015-2016, Google Inc.
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

#include "src/core/ext/transport/chttp2/transport/frame_rst_stream.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"

#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/frame.h"

gpr_slice grpc_chttp2_rst_stream_create(uint32_t id, uint32_t code,
                                        grpc_transport_one_way_stats *stats) {
  gpr_slice slice = gpr_slice_malloc(13);
  stats->framing_bytes += 13;
  uint8_t *p = GPR_SLICE_START_PTR(slice);

  *p++ = 0;
  *p++ = 0;
  *p++ = 4;
  *p++ = GRPC_CHTTP2_FRAME_RST_STREAM;
  *p++ = 0;
  *p++ = (uint8_t)(id >> 24);
  *p++ = (uint8_t)(id >> 16);
  *p++ = (uint8_t)(id >> 8);
  *p++ = (uint8_t)(id);
  *p++ = (uint8_t)(code >> 24);
  *p++ = (uint8_t)(code >> 16);
  *p++ = (uint8_t)(code >> 8);
  *p++ = (uint8_t)(code);

  return slice;
}

grpc_chttp2_parse_error grpc_chttp2_rst_stream_parser_begin_frame(
    grpc_chttp2_rst_stream_parser *parser, uint32_t length, uint8_t flags) {
  if (length != 4) {
    gpr_log(GPR_ERROR, "invalid rst_stream: length=%d, flags=%02x", length,
            flags);
    return GRPC_CHTTP2_CONNECTION_ERROR;
  }
  parser->byte = 0;
  return GRPC_CHTTP2_PARSE_OK;
}

grpc_chttp2_parse_error grpc_chttp2_rst_stream_parser_parse(
    grpc_exec_ctx *exec_ctx, void *parser,
    grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_parsing *stream_parsing, gpr_slice slice, int is_last) {
  uint8_t *const beg = GPR_SLICE_START_PTR(slice);
  uint8_t *const end = GPR_SLICE_END_PTR(slice);
  uint8_t *cur = beg;
  grpc_chttp2_rst_stream_parser *p = parser;

  while (p->byte != 4 && cur != end) {
    p->reason_bytes[p->byte] = *cur;
    cur++;
    p->byte++;
  }
  stream_parsing->stats.incoming.framing_bytes += (uint64_t)(end - cur);

  if (p->byte == 4) {
    GPR_ASSERT(is_last);
    stream_parsing->received_close = 1;
    stream_parsing->saw_rst_stream = 1;
    stream_parsing->rst_stream_reason = (((uint32_t)p->reason_bytes[0]) << 24) |
                                        (((uint32_t)p->reason_bytes[1]) << 16) |
                                        (((uint32_t)p->reason_bytes[2]) << 8) |
                                        (((uint32_t)p->reason_bytes[3]));
  }

  return GRPC_CHTTP2_PARSE_OK;
}
