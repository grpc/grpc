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

#include "src/core/transport/chttp2/frame_window_update.h"
#include "src/core/transport/chttp2/internal.h"

#include <grpc/support/log.h>

gpr_slice grpc_chttp2_window_update_create(gpr_uint32 id,
                                           gpr_uint32 window_update) {
  gpr_slice slice = gpr_slice_malloc(13);
  gpr_uint8 *p = GPR_SLICE_START_PTR(slice);

  GPR_ASSERT(window_update);

  *p++ = 0;
  *p++ = 0;
  *p++ = 4;
  *p++ = GRPC_CHTTP2_FRAME_WINDOW_UPDATE;
  *p++ = 0;
  *p++ = (gpr_uint8)(id >> 24);
  *p++ = (gpr_uint8)(id >> 16);
  *p++ = (gpr_uint8)(id >> 8);
  *p++ = (gpr_uint8)(id);
  *p++ = (gpr_uint8)(window_update >> 24);
  *p++ = (gpr_uint8)(window_update >> 16);
  *p++ = (gpr_uint8)(window_update >> 8);
  *p++ = (gpr_uint8)(window_update);

  return slice;
}

grpc_chttp2_parse_error grpc_chttp2_window_update_parser_begin_frame(
    grpc_chttp2_window_update_parser *parser, gpr_uint32 length,
    gpr_uint8 flags) {
  if (flags || length != 4) {
    gpr_log(GPR_ERROR, "invalid window update: length=%d, flags=%02x", length,
            flags);
    return GRPC_CHTTP2_CONNECTION_ERROR;
  }
  parser->byte = 0;
  parser->amount = 0;
  return GRPC_CHTTP2_PARSE_OK;
}

grpc_chttp2_parse_error grpc_chttp2_window_update_parser_parse(
    grpc_exec_ctx *exec_ctx, void *parser,
    grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_parsing *stream_parsing, gpr_slice slice, int is_last) {
  gpr_uint8 *const beg = GPR_SLICE_START_PTR(slice);
  gpr_uint8 *const end = GPR_SLICE_END_PTR(slice);
  gpr_uint8 *cur = beg;
  grpc_chttp2_window_update_parser *p = parser;

  while (p->byte != 4 && cur != end) {
    p->amount |= ((gpr_uint32)*cur) << (8 * (3 - p->byte));
    cur++;
    p->byte++;
  }

  if (p->byte == 4) {
    if (p->amount == 0 || (p->amount & 0x80000000u)) {
      gpr_log(GPR_ERROR, "invalid window update bytes: %d", p->amount);
      return GRPC_CHTTP2_CONNECTION_ERROR;
    }
    GPR_ASSERT(is_last);

    if (transport_parsing->incoming_stream_id != 0) {
      if (stream_parsing != NULL) {
        GRPC_CHTTP2_FLOWCTL_TRACE_STREAM("update", transport_parsing,
                                         stream_parsing, outgoing_window_update,
                                         p->amount);
        stream_parsing->outgoing_window_update += p->amount;
        grpc_chttp2_list_add_parsing_seen_stream(transport_parsing,
                                                 stream_parsing);
      }
    } else {
      GRPC_CHTTP2_FLOWCTL_TRACE_TRANSPORT("update", transport_parsing,
                                          outgoing_window_update, p->amount);
      transport_parsing->outgoing_window_update += p->amount;
    }
  }

  return GRPC_CHTTP2_PARSE_OK;
}
