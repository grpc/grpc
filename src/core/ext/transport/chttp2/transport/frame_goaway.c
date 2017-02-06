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

#include "src/core/ext/transport/chttp2/transport/frame_goaway.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

void grpc_chttp2_goaway_parser_init(grpc_chttp2_goaway_parser *p) {
  p->debug_data = NULL;
}

void grpc_chttp2_goaway_parser_destroy(grpc_chttp2_goaway_parser *p) {
  gpr_free(p->debug_data);
}

grpc_error *grpc_chttp2_goaway_parser_begin_frame(grpc_chttp2_goaway_parser *p,
                                                  uint32_t length,
                                                  uint8_t flags) {
  if (length < 8) {
    char *msg;
    gpr_asprintf(&msg, "goaway frame too short (%d bytes)", length);
    grpc_error *err = GRPC_ERROR_CREATE(msg);
    gpr_free(msg);
    return err;
  }

  gpr_free(p->debug_data);
  p->debug_length = length - 8;
  p->debug_data = gpr_malloc(p->debug_length);
  p->debug_pos = 0;
  p->state = GRPC_CHTTP2_GOAWAY_LSI0;
  return GRPC_ERROR_NONE;
}

grpc_error *grpc_chttp2_goaway_parser_parse(grpc_exec_ctx *exec_ctx,
                                            void *parser,
                                            grpc_chttp2_transport *t,
                                            grpc_chttp2_stream *s,
                                            grpc_slice slice, int is_last) {
  uint8_t *const beg = GRPC_SLICE_START_PTR(slice);
  uint8_t *const end = GRPC_SLICE_END_PTR(slice);
  uint8_t *cur = beg;
  grpc_chttp2_goaway_parser *p = parser;

  switch (p->state) {
    case GRPC_CHTTP2_GOAWAY_LSI0:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_LSI0;
        return GRPC_ERROR_NONE;
      }
      p->last_stream_id = ((uint32_t)*cur) << 24;
      ++cur;
    /* fallthrough */
    case GRPC_CHTTP2_GOAWAY_LSI1:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_LSI1;
        return GRPC_ERROR_NONE;
      }
      p->last_stream_id |= ((uint32_t)*cur) << 16;
      ++cur;
    /* fallthrough */
    case GRPC_CHTTP2_GOAWAY_LSI2:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_LSI2;
        return GRPC_ERROR_NONE;
      }
      p->last_stream_id |= ((uint32_t)*cur) << 8;
      ++cur;
    /* fallthrough */
    case GRPC_CHTTP2_GOAWAY_LSI3:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_LSI3;
        return GRPC_ERROR_NONE;
      }
      p->last_stream_id |= ((uint32_t)*cur);
      ++cur;
    /* fallthrough */
    case GRPC_CHTTP2_GOAWAY_ERR0:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_ERR0;
        return GRPC_ERROR_NONE;
      }
      p->error_code = ((uint32_t)*cur) << 24;
      ++cur;
    /* fallthrough */
    case GRPC_CHTTP2_GOAWAY_ERR1:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_ERR1;
        return GRPC_ERROR_NONE;
      }
      p->error_code |= ((uint32_t)*cur) << 16;
      ++cur;
    /* fallthrough */
    case GRPC_CHTTP2_GOAWAY_ERR2:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_ERR2;
        return GRPC_ERROR_NONE;
      }
      p->error_code |= ((uint32_t)*cur) << 8;
      ++cur;
    /* fallthrough */
    case GRPC_CHTTP2_GOAWAY_ERR3:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_ERR3;
        return GRPC_ERROR_NONE;
      }
      p->error_code |= ((uint32_t)*cur);
      ++cur;
    /* fallthrough */
    case GRPC_CHTTP2_GOAWAY_DEBUG:
      if (end != cur)
        memcpy(p->debug_data + p->debug_pos, cur, (size_t)(end - cur));
      GPR_ASSERT((size_t)(end - cur) < UINT32_MAX - p->debug_pos);
      p->debug_pos += (uint32_t)(end - cur);
      p->state = GRPC_CHTTP2_GOAWAY_DEBUG;
      if (is_last) {
        grpc_chttp2_add_incoming_goaway(
            exec_ctx, t, (uint32_t)p->error_code,
            grpc_slice_new(p->debug_data, p->debug_length, gpr_free));
        p->debug_data = NULL;
      }
      return GRPC_ERROR_NONE;
  }
  GPR_UNREACHABLE_CODE(return GRPC_ERROR_CREATE("Should never reach here"));
}

void grpc_chttp2_goaway_append(uint32_t last_stream_id, uint32_t error_code,
                               grpc_slice debug_data,
                               grpc_slice_buffer *slice_buffer) {
  grpc_slice header = grpc_slice_malloc(9 + 4 + 4);
  uint8_t *p = GRPC_SLICE_START_PTR(header);
  uint32_t frame_length;
  GPR_ASSERT(GRPC_SLICE_LENGTH(debug_data) < UINT32_MAX - 4 - 4);
  frame_length = 4 + 4 + (uint32_t)GRPC_SLICE_LENGTH(debug_data);

  /* frame header: length */
  *p++ = (uint8_t)(frame_length >> 16);
  *p++ = (uint8_t)(frame_length >> 8);
  *p++ = (uint8_t)(frame_length);
  /* frame header: type */
  *p++ = GRPC_CHTTP2_FRAME_GOAWAY;
  /* frame header: flags */
  *p++ = 0;
  /* frame header: stream id */
  *p++ = 0;
  *p++ = 0;
  *p++ = 0;
  *p++ = 0;
  /* payload: last stream id */
  *p++ = (uint8_t)(last_stream_id >> 24);
  *p++ = (uint8_t)(last_stream_id >> 16);
  *p++ = (uint8_t)(last_stream_id >> 8);
  *p++ = (uint8_t)(last_stream_id);
  /* payload: error code */
  *p++ = (uint8_t)(error_code >> 24);
  *p++ = (uint8_t)(error_code >> 16);
  *p++ = (uint8_t)(error_code >> 8);
  *p++ = (uint8_t)(error_code);
  GPR_ASSERT(p == GRPC_SLICE_END_PTR(header));
  grpc_slice_buffer_add(slice_buffer, header);
  grpc_slice_buffer_add(slice_buffer, debug_data);
}
