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

#include "src/core/ext/transport/chttp2/transport/frame_rst_stream.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/lib/transport/http2_errors.h"

grpc_slice grpc_chttp2_rst_stream_create(uint32_t id, uint32_t code,
                                         grpc_transport_one_way_stats *stats) {
  static const size_t frame_size = 13;
  grpc_slice slice = grpc_slice_malloc(frame_size);
  stats->framing_bytes += frame_size;
  uint8_t *p = GRPC_SLICE_START_PTR(slice);

  // Frame size.
  *p++ = 0;
  *p++ = 0;
  *p++ = 4;
  // Frame type.
  *p++ = GRPC_CHTTP2_FRAME_RST_STREAM;
  // Flags.
  *p++ = 0;
  // Stream ID.
  *p++ = (uint8_t)(id >> 24);
  *p++ = (uint8_t)(id >> 16);
  *p++ = (uint8_t)(id >> 8);
  *p++ = (uint8_t)(id);
  // Error code.
  *p++ = (uint8_t)(code >> 24);
  *p++ = (uint8_t)(code >> 16);
  *p++ = (uint8_t)(code >> 8);
  *p++ = (uint8_t)(code);

  return slice;
}

grpc_error *grpc_chttp2_rst_stream_parser_begin_frame(
    grpc_chttp2_rst_stream_parser *parser, uint32_t length, uint8_t flags) {
  if (length != 4) {
    char *msg;
    gpr_asprintf(&msg, "invalid rst_stream: length=%d, flags=%02x", length,
                 flags);
    grpc_error *err = GRPC_ERROR_CREATE(msg);
    gpr_free(msg);
    return err;
  }
  parser->byte = 0;
  return GRPC_ERROR_NONE;
}

grpc_error *grpc_chttp2_rst_stream_parser_parse(grpc_exec_ctx *exec_ctx,
                                                void *parser,
                                                grpc_chttp2_transport *t,
                                                grpc_chttp2_stream *s,
                                                grpc_slice slice, int is_last) {
  uint8_t *const beg = GRPC_SLICE_START_PTR(slice);
  uint8_t *const end = GRPC_SLICE_END_PTR(slice);
  uint8_t *cur = beg;
  grpc_chttp2_rst_stream_parser *p = parser;

  while (p->byte != 4 && cur != end) {
    p->reason_bytes[p->byte] = *cur;
    cur++;
    p->byte++;
  }
  s->stats.incoming.framing_bytes += (uint64_t)(end - cur);

  if (p->byte == 4) {
    GPR_ASSERT(is_last);
    uint32_t reason = (((uint32_t)p->reason_bytes[0]) << 24) |
                      (((uint32_t)p->reason_bytes[1]) << 16) |
                      (((uint32_t)p->reason_bytes[2]) << 8) |
                      (((uint32_t)p->reason_bytes[3]));
    grpc_error *error = GRPC_ERROR_NONE;
    if (reason != GRPC_HTTP2_NO_ERROR || s->header_frames_received < 2) {
      char *message;
      gpr_asprintf(&message, "Received RST_STREAM with error code %d", reason);
      error = grpc_error_set_int(
          grpc_error_set_str(GRPC_ERROR_CREATE("RST_STREAM"),
                             GRPC_ERROR_STR_GRPC_MESSAGE, message),
          GRPC_ERROR_INT_HTTP2_ERROR, (intptr_t)reason);
      gpr_free(message);
    }
    grpc_chttp2_mark_stream_closed(exec_ctx, t, s, true, true, error);
  }

  return GRPC_ERROR_NONE;
}
