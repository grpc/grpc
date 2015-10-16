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

#include "src/core/transport/chttp2/frame_data.h"

#include <string.h>

#include "src/core/transport/chttp2/internal.h"
#include "src/core/support/string.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>
#include "src/core/transport/transport.h"

grpc_chttp2_parse_error grpc_chttp2_data_parser_init(
    grpc_chttp2_data_parser *parser) {
  parser->state = GRPC_CHTTP2_DATA_FH_0;
  grpc_sopb_init(&parser->incoming_sopb);
  return GRPC_CHTTP2_PARSE_OK;
}

void grpc_chttp2_data_parser_destroy(grpc_chttp2_data_parser *parser) {
  grpc_sopb_destroy(&parser->incoming_sopb);
}

grpc_chttp2_parse_error grpc_chttp2_data_parser_begin_frame(
    grpc_chttp2_data_parser *parser, gpr_uint8 flags) {
  if (flags & ~GRPC_CHTTP2_DATA_FLAG_END_STREAM) {
    gpr_log(GPR_ERROR, "unsupported data flags: 0x%02x", flags);
    return GRPC_CHTTP2_STREAM_ERROR;
  }

  if (flags & GRPC_CHTTP2_DATA_FLAG_END_STREAM) {
    parser->is_last_frame = 1;
  } else {
    parser->is_last_frame = 0;
  }

  return GRPC_CHTTP2_PARSE_OK;
}

grpc_chttp2_parse_error grpc_chttp2_data_parser_parse(
    grpc_exec_ctx *exec_ctx, void *parser,
    grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_parsing *stream_parsing, gpr_slice slice, int is_last) {
  gpr_uint8 *const beg = GPR_SLICE_START_PTR(slice);
  gpr_uint8 *const end = GPR_SLICE_END_PTR(slice);
  gpr_uint8 *cur = beg;
  grpc_chttp2_data_parser *p = parser;
  gpr_uint32 message_flags = 0;

  if (is_last && p->is_last_frame) {
    stream_parsing->received_close = 1;
  }

  if (cur == end) {
    return GRPC_CHTTP2_PARSE_OK;
  }

  switch (p->state) {
  fh_0:
    case GRPC_CHTTP2_DATA_FH_0:
      p->frame_type = *cur;
      switch (p->frame_type) {
        case 0:
          p->is_frame_compressed = 0; /* GPR_FALSE */
          break;
        case 1:
          p->is_frame_compressed = 1; /* GPR_TRUE */
          break;
        default:
          gpr_log(GPR_ERROR, "Bad GRPC frame type 0x%02x", p->frame_type);
          return GRPC_CHTTP2_STREAM_ERROR;
      }
      if (++cur == end) {
        p->state = GRPC_CHTTP2_DATA_FH_1;
        return GRPC_CHTTP2_PARSE_OK;
      }
    /* fallthrough */
    case GRPC_CHTTP2_DATA_FH_1:
      p->frame_size = ((gpr_uint32)*cur) << 24;
      if (++cur == end) {
        p->state = GRPC_CHTTP2_DATA_FH_2;
        return GRPC_CHTTP2_PARSE_OK;
      }
    /* fallthrough */
    case GRPC_CHTTP2_DATA_FH_2:
      p->frame_size |= ((gpr_uint32)*cur) << 16;
      if (++cur == end) {
        p->state = GRPC_CHTTP2_DATA_FH_3;
        return GRPC_CHTTP2_PARSE_OK;
      }
    /* fallthrough */
    case GRPC_CHTTP2_DATA_FH_3:
      p->frame_size |= ((gpr_uint32)*cur) << 8;
      if (++cur == end) {
        p->state = GRPC_CHTTP2_DATA_FH_4;
        return GRPC_CHTTP2_PARSE_OK;
      }
    /* fallthrough */
    case GRPC_CHTTP2_DATA_FH_4:
      p->frame_size |= ((gpr_uint32)*cur);
      p->state = GRPC_CHTTP2_DATA_FRAME;
      ++cur;
      if (p->is_frame_compressed) {
        message_flags |= GRPC_WRITE_INTERNAL_COMPRESS;
      }
      grpc_sopb_add_begin_message(&p->incoming_sopb, p->frame_size,
                                  message_flags);
    /* fallthrough */
    case GRPC_CHTTP2_DATA_FRAME:
      if (cur == end) {
        grpc_chttp2_list_add_parsing_seen_stream(transport_parsing,
                                                 stream_parsing);
        return GRPC_CHTTP2_PARSE_OK;
      }
      grpc_chttp2_list_add_parsing_seen_stream(transport_parsing,
                                               stream_parsing);
      if ((gpr_uint32)(end - cur) == p->frame_size) {
        grpc_sopb_add_slice(
            &p->incoming_sopb,
            gpr_slice_sub(slice, (size_t)(cur - beg), (size_t)(end - beg)));
        p->state = GRPC_CHTTP2_DATA_FH_0;
        return GRPC_CHTTP2_PARSE_OK;
      } else if ((gpr_uint32)(end - cur) > p->frame_size) {
        grpc_sopb_add_slice(&p->incoming_sopb,
                            gpr_slice_sub(slice, (size_t)(cur - beg),
                                          (size_t)(cur + p->frame_size - beg)));
        cur += p->frame_size;
        goto fh_0; /* loop */
      } else {
        grpc_sopb_add_slice(
            &p->incoming_sopb,
            gpr_slice_sub(slice, (size_t)(cur - beg), (size_t)(end - beg)));
        GPR_ASSERT((size_t)(end - cur) <= p->frame_size);
        p->frame_size -= (gpr_uint32)(end - cur);
        return GRPC_CHTTP2_PARSE_OK;
      }
  }

  GPR_UNREACHABLE_CODE(return GRPC_CHTTP2_CONNECTION_ERROR);
}
