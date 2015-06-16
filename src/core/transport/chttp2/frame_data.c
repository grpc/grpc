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

#include "src/core/support/string.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>
#include "src/core/transport/transport.h"
#include "src/core/compression/message_compress.h"

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

/** Performs any extra work needed after a frame has been assembled */
grpc_chttp2_parse_error parse_postprocessing(grpc_chttp2_data_parser *p) {
  if (p->is_frame_compressed) {  /* Decompress */
    /* Reorganize the slices within p->incoming_sopb into a gpr_slice_buffer to
     * be fed to the decompression function */
    gpr_slice_buffer sb_in, sb_out;
    grpc_stream_op_buffer *sopb = &p->incoming_sopb;
    size_t i;
    gpr_slice_buffer_init(&sb_in);
    gpr_slice_buffer_init(&sb_out);
    for (i = 0; i < sopb->nops; ++i) {
      if (sopb->ops->type == GRPC_OP_SLICE) {
        gpr_slice_buffer_add(&sb_in, sopb->ops->data.slice);
      }
    }
    grpc_msg_decompress(GRPC_COMPRESS_GZIP /* XXX */, &sb_in, &sb_out);
    /* copy uncompressed output back to p->incoming_sopb */
    grpc_sopb_reset(sopb);
    grpc_sopb_add_begin_message(sopb, sb_out.length, 0);
    for (i = 0; i < sb_out.count; ++i) {
      grpc_sopb_add_slice(sopb, sb_out.slices[i]);
    }
    gpr_slice_buffer_destroy(&sb_in);
    gpr_slice_buffer_destroy(&sb_out);
  }

  return GRPC_CHTTP2_PARSE_OK;
}

grpc_chttp2_parse_error grpc_chttp2_data_parser_parse(
    void *parser, grpc_chttp2_parse_state *state, gpr_slice slice,
    int is_last) {
  gpr_uint8 *const beg = GPR_SLICE_START_PTR(slice);
  gpr_uint8 *const end = GPR_SLICE_END_PTR(slice);
  gpr_uint8 *cur = beg;
  grpc_chttp2_data_parser *p = parser;

  if (is_last && p->is_last_frame) {
    state->end_of_stream = 1;
    state->need_flush_reads = 1;
  }

  if (cur == end) {
    return GRPC_CHTTP2_PARSE_OK;
  }

  switch (p->state) {
  fh_0:
    case GRPC_CHTTP2_DATA_FH_0:
      p->frame_type = *cur;
      if (++cur == end) {
        p->state = GRPC_CHTTP2_DATA_FH_1;
        return GRPC_CHTTP2_PARSE_OK;
      }
      switch (p->frame_type) {
        case 0:
          break;
        case 1:
          p->is_frame_compressed = 1;  /* GPR_TRUE */
          break;
        default:
          gpr_log(GPR_ERROR, "Bad GRPC frame type 0x%02x", p->frame_type);
          return GRPC_CHTTP2_STREAM_ERROR;
      }
    /* fallthrough */
    case GRPC_CHTTP2_DATA_FH_1:
      p->frame_size = ((gpr_uint32) * cur) << 24;
      if (++cur == end) {
        p->state = GRPC_CHTTP2_DATA_FH_2;
        return GRPC_CHTTP2_PARSE_OK;
      }
    /* fallthrough */
    case GRPC_CHTTP2_DATA_FH_2:
      p->frame_size |= ((gpr_uint32) * cur) << 16;
      if (++cur == end) {
        p->state = GRPC_CHTTP2_DATA_FH_3;
        return GRPC_CHTTP2_PARSE_OK;
      }
    /* fallthrough */
    case GRPC_CHTTP2_DATA_FH_3:
      p->frame_size |= ((gpr_uint32) * cur) << 8;
      if (++cur == end) {
        p->state = GRPC_CHTTP2_DATA_FH_4;
        return GRPC_CHTTP2_PARSE_OK;
      }
    /* fallthrough */
    case GRPC_CHTTP2_DATA_FH_4:
      p->frame_size |= ((gpr_uint32) * cur);
      p->state = GRPC_CHTTP2_DATA_FRAME;
      ++cur;
      state->need_flush_reads = 1;
      grpc_sopb_add_begin_message(&p->incoming_sopb, p->frame_size, 0);
    /* fallthrough */
    case GRPC_CHTTP2_DATA_FRAME:
      if (cur == end) {
        return parse_postprocessing(p);
      } else if ((gpr_uint32)(end - cur) == p->frame_size) {
        state->need_flush_reads = 1;
        grpc_sopb_add_slice(&p->incoming_sopb,
                            gpr_slice_sub(slice, cur - beg, end - beg));
        p->state = GRPC_CHTTP2_DATA_FH_0;
        return parse_postprocessing(p);
      } else if ((gpr_uint32)(end - cur) > p->frame_size) {
        state->need_flush_reads = 1;
        grpc_sopb_add_slice(
            &p->incoming_sopb,
            gpr_slice_sub(slice, cur - beg, cur + p->frame_size - beg));
        cur += p->frame_size;
        goto fh_0; /* loop */
      } else {
        state->need_flush_reads = 1;
        grpc_sopb_add_slice(&p->incoming_sopb,
                            gpr_slice_sub(slice, cur - beg, end - beg));
        p->frame_size -= (end - cur);
        return parse_postprocessing(p);
      }
  }

  gpr_log(GPR_ERROR, "should never reach here");
  abort();
  return GRPC_CHTTP2_CONNECTION_ERROR;
}
