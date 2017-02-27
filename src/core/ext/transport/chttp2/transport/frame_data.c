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

#include "src/core/ext/transport/chttp2/transport/frame_data.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/lib/slice/slice_internal.h"

grpc_error *grpc_chttp2_data_parser_init(grpc_chttp2_data_parser *parser) {
  parser->state = GRPC_CHTTP2_DATA_FH_0;
  parser->parsing_frame = NULL;
  return GRPC_ERROR_NONE;
}

void grpc_chttp2_data_parser_destroy(grpc_exec_ctx *exec_ctx,
                                     grpc_chttp2_data_parser *parser) {
  if (parser->parsing_frame != NULL) {
    grpc_chttp2_incoming_byte_stream_finished(
        exec_ctx, parser->parsing_frame, GRPC_ERROR_CREATE("Parser destroyed"));
  }
  GRPC_ERROR_UNREF(parser->error);
}

grpc_error *grpc_chttp2_data_parser_begin_frame(grpc_chttp2_data_parser *parser,
                                                uint8_t flags,
                                                uint32_t stream_id) {
  if (flags & ~GRPC_CHTTP2_DATA_FLAG_END_STREAM) {
    char *msg;
    gpr_asprintf(&msg, "unsupported data flags: 0x%02x", flags);
    grpc_error *err = grpc_error_set_int(
        GRPC_ERROR_CREATE(msg), GRPC_ERROR_INT_STREAM_ID, (intptr_t)stream_id);
    gpr_free(msg);
    return err;
  }

  if (flags & GRPC_CHTTP2_DATA_FLAG_END_STREAM) {
    parser->is_last_frame = 1;
  } else {
    parser->is_last_frame = 0;
  }

  return GRPC_ERROR_NONE;
}

void grpc_chttp2_incoming_frame_queue_merge(
    grpc_chttp2_incoming_frame_queue *head_dst,
    grpc_chttp2_incoming_frame_queue *tail_src) {
  if (tail_src->head == NULL) {
    return;
  }

  if (head_dst->head == NULL) {
    *head_dst = *tail_src;
    memset(tail_src, 0, sizeof(*tail_src));
    return;
  }

  head_dst->tail->next_message = tail_src->head;
  head_dst->tail = tail_src->tail;
  memset(tail_src, 0, sizeof(*tail_src));
}

grpc_byte_stream *grpc_chttp2_incoming_frame_queue_pop(
    grpc_chttp2_incoming_frame_queue *q) {
  grpc_byte_stream *out;
  if (q->head == NULL) {
    return NULL;
  }
  out = &q->head->base;
  if (q->head == q->tail) {
    memset(q, 0, sizeof(*q));
  } else {
    q->head = q->head->next_message;
  }
  return out;
}

void grpc_chttp2_encode_data(uint32_t id, grpc_slice_buffer *inbuf,
                             uint32_t write_bytes, int is_eof,
                             grpc_transport_one_way_stats *stats,
                             grpc_slice_buffer *outbuf) {
  grpc_slice hdr;
  uint8_t *p;
  static const size_t header_size = 9;

  hdr = grpc_slice_malloc(header_size);
  p = GRPC_SLICE_START_PTR(hdr);
  GPR_ASSERT(write_bytes < (1 << 24));
  *p++ = (uint8_t)(write_bytes >> 16);
  *p++ = (uint8_t)(write_bytes >> 8);
  *p++ = (uint8_t)(write_bytes);
  *p++ = GRPC_CHTTP2_FRAME_DATA;
  *p++ = is_eof ? GRPC_CHTTP2_DATA_FLAG_END_STREAM : 0;
  *p++ = (uint8_t)(id >> 24);
  *p++ = (uint8_t)(id >> 16);
  *p++ = (uint8_t)(id >> 8);
  *p++ = (uint8_t)(id);
  grpc_slice_buffer_add(outbuf, hdr);

  grpc_slice_buffer_move_first(inbuf, write_bytes, outbuf);

  stats->framing_bytes += header_size;
  stats->data_bytes += write_bytes;
}

static void grpc_chttp2_unprocessed_frames_buffer_push(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_data_parser *p, grpc_chttp2_stream *s,
    grpc_slice slice) {
  grpc_slice_buffer_add(&s->unprocessed_incoming_frames_buffer, slice);
  if (p->parsing_frame) {
    grpc_chttp2_incoming_byte_stream *bs = p->parsing_frame;
    // Necessary?
    gpr_mu_lock(&bs->slice_mu);
    if (bs->on_next != NULL) {
      grpc_closure_sched(exec_ctx, bs->on_next, GRPC_ERROR_NONE);
      bs->on_next = NULL;
    }
    gpr_mu_unlock(&bs->slice_mu);
  }
}

grpc_error *parse_inner_buffer(grpc_exec_ctx *exec_ctx,
                               grpc_chttp2_data_parser *p,
                               grpc_chttp2_transport *t, grpc_chttp2_stream *s,
                               grpc_slice slice) {
  uint8_t *const beg = GRPC_SLICE_START_PTR(slice);
  uint8_t *const end = GRPC_SLICE_END_PTR(slice);
  uint8_t *cur = beg;
  uint32_t message_flags;
  char *msg;

  if (cur == end) {
    return GRPC_ERROR_NONE;
  }

  /* If there is already pending data, or if there is a pending
   * incoming_byte_stream that is finished, append the data to unprocessed frame
   * buffer. */
  gpr_mu_lock(&s->buffer_mu);
  if (s->unprocessed_incoming_frames_buffer.count > 0) {
    s->stats.incoming.framing_bytes += GRPC_SLICE_LENGTH(slice);
    grpc_slice_ref_internal(slice);
    grpc_chttp2_unprocessed_frames_buffer_push(exec_ctx, p, s, slice);
    gpr_mu_unlock(&s->buffer_mu);
    return GRPC_ERROR_NONE;
  }

  switch (p->state) {
    case GRPC_CHTTP2_DATA_ERROR:
      p->state = GRPC_CHTTP2_DATA_ERROR;
      gpr_mu_unlock(&s->buffer_mu);
      return GRPC_ERROR_REF(p->error);
    case GRPC_CHTTP2_DATA_FH_0:
      if (s->incoming_frames != NULL) {
        s->stats.incoming.framing_bytes += (size_t)(end - cur);
        grpc_chttp2_unprocessed_frames_buffer_push(
            exec_ctx, p, s,
            grpc_slice_sub(slice, (size_t)(cur - beg), (size_t)(end - beg)));
        gpr_mu_unlock(&s->buffer_mu);
        return GRPC_ERROR_NONE;
      }
      s->stats.incoming.framing_bytes++;
      p->frame_type = *cur;
      switch (p->frame_type) {
        case 0:
          p->is_frame_compressed = 0; /* GPR_FALSE */
          break;
        case 1:
          p->is_frame_compressed = 1; /* GPR_TRUE */
          break;
        default:
          gpr_asprintf(&msg, "Bad GRPC frame type 0x%02x", p->frame_type);
          p->error = GRPC_ERROR_CREATE(msg);
          p->error = grpc_error_set_int(p->error, GRPC_ERROR_INT_STREAM_ID,
                                        (intptr_t)s->id);
          gpr_free(msg);
          msg = grpc_dump_slice(slice, GPR_DUMP_HEX | GPR_DUMP_ASCII);
          p->error =
              grpc_error_set_str(p->error, GRPC_ERROR_STR_RAW_BYTES, msg);
          gpr_free(msg);
          p->error =
              grpc_error_set_int(p->error, GRPC_ERROR_INT_OFFSET, cur - beg);
          p->state = GRPC_CHTTP2_DATA_ERROR;
          gpr_mu_unlock(&s->buffer_mu);
          return GRPC_ERROR_REF(p->error);
      }
      if (++cur == end) {
        p->state = GRPC_CHTTP2_DATA_FH_1;
        gpr_mu_unlock(&s->buffer_mu);
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_CHTTP2_DATA_FH_1:
      s->stats.incoming.framing_bytes++;
      p->frame_size = ((uint32_t)*cur) << 24;
      if (++cur == end) {
        p->state = GRPC_CHTTP2_DATA_FH_2;
        gpr_mu_unlock(&s->buffer_mu);
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_CHTTP2_DATA_FH_2:
      s->stats.incoming.framing_bytes++;
      p->frame_size |= ((uint32_t)*cur) << 16;
      if (++cur == end) {
        p->state = GRPC_CHTTP2_DATA_FH_3;
        gpr_mu_unlock(&s->buffer_mu);
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_CHTTP2_DATA_FH_3:
      s->stats.incoming.framing_bytes++;
      p->frame_size |= ((uint32_t)*cur) << 8;
      if (++cur == end) {
        p->state = GRPC_CHTTP2_DATA_FH_4;
        gpr_mu_unlock(&s->buffer_mu);
        return GRPC_ERROR_NONE;
      }
    /* fallthrough */
    case GRPC_CHTTP2_DATA_FH_4:
      s->stats.incoming.framing_bytes++;
      p->frame_size |= ((uint32_t)*cur);
      p->state = GRPC_CHTTP2_DATA_FRAME;
      ++cur;
      message_flags = 0;
      if (p->is_frame_compressed) {
        message_flags |= GRPC_WRITE_INTERNAL_COMPRESS;
      }
      GPR_ASSERT(s->incoming_frames == NULL);
      p->parsing_frame = grpc_chttp2_incoming_byte_stream_create(
          exec_ctx, t, s, p->frame_size, message_flags);
    /* fallthrough */
    case GRPC_CHTTP2_DATA_FRAME:
      if (cur == end) {
        gpr_mu_unlock(&s->buffer_mu);
        return GRPC_ERROR_NONE;
      }
      uint32_t remaining = (uint32_t)(end - cur);
      s->stats.incoming.data_bytes += remaining;
      grpc_chttp2_unprocessed_frames_buffer_push(
          exec_ctx, p, s,
          grpc_slice_sub(slice, (size_t)(cur - beg), (size_t)(end - beg)));
      gpr_mu_unlock(&s->buffer_mu);
      return GRPC_ERROR_NONE;
  }

  GPR_UNREACHABLE_CODE(return GRPC_ERROR_CREATE("Should never reach here"));
}

grpc_error *grpc_chttp2_data_parser_parse(grpc_exec_ctx *exec_ctx, void *parser,
                                          grpc_chttp2_transport *t,
                                          grpc_chttp2_stream *s,
                                          grpc_slice slice, int is_last) {
  grpc_chttp2_data_parser *p = parser;
  grpc_error *error = parse_inner_buffer(exec_ctx, p, t, s, slice);

  if (is_last && p->is_last_frame) {
    grpc_chttp2_mark_stream_closed(exec_ctx, t, s, true, false,
                                   GRPC_ERROR_NONE);
  }

  return error;
}
