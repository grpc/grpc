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

#include "src/core/transport/chttp2/frame_ping.h"

#include <string.h>

#include <grpc/support/log.h>

gpr_slice grpc_chttp2_ping_create(gpr_uint8 ack, gpr_uint8 *opaque_8bytes) {
  gpr_slice slice = gpr_slice_malloc(9 + 8);
  gpr_uint8 *p = GPR_SLICE_START_PTR(slice);

  *p++ = 0;
  *p++ = 0;
  *p++ = 8;
  *p++ = GRPC_CHTTP2_FRAME_PING;
  *p++ = ack ? 1 : 0;
  *p++ = 0;
  *p++ = 0;
  *p++ = 0;
  *p++ = 0;
  memcpy(p, opaque_8bytes, 8);

  return slice;
}

grpc_chttp2_parse_error grpc_chttp2_ping_parser_begin_frame(
    grpc_chttp2_ping_parser *parser, gpr_uint32 length, gpr_uint8 flags) {
  if (flags & 0xfe || length != 8) {
    gpr_log(GPR_ERROR, "invalid ping: length=%d, flags=%02x", length, flags);
    return GRPC_CHTTP2_CONNECTION_ERROR;
  }
  parser->byte = 0;
  parser->is_ack = flags;
  return GRPC_CHTTP2_PARSE_OK;
}

grpc_chttp2_parse_error grpc_chttp2_ping_parser_parse(
    void *parser, grpc_chttp2_parse_state *state, gpr_slice slice,
    int is_last) {
  gpr_uint8 *const beg = GPR_SLICE_START_PTR(slice);
  gpr_uint8 *const end = GPR_SLICE_END_PTR(slice);
  gpr_uint8 *cur = beg;
  grpc_chttp2_ping_parser *p = parser;

  while (p->byte != 8 && cur != end) {
    p->opaque_8bytes[p->byte] = *cur;
    cur++;
    p->byte++;
  }

  if (p->byte == 8) {
    GPR_ASSERT(is_last);
    if (p->is_ack) {
      state->process_ping_reply = 1;
    } else {
      state->send_ping_ack = 1;
    }
  }

  return GRPC_CHTTP2_PARSE_OK;
}
