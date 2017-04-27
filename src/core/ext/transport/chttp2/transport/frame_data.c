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
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/transport/transport.h"

grpc_error *grpc_chttp2_data_parser_init(grpc_chttp2_data_parser *parser) {
  parser->state = GRPC_CHTTP2_DATA_FH_0;
  parser->parsing_frame = NULL;
  return GRPC_ERROR_NONE;
}

void grpc_chttp2_data_parser_destroy(grpc_exec_ctx *exec_ctx,
                                     grpc_chttp2_data_parser *parser) {
  if (parser->parsing_frame != NULL) {
    GRPC_ERROR_UNREF(grpc_chttp2_incoming_byte_stream_finished(
        exec_ctx, parser->parsing_frame,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Parser destroyed"), false));
  }
  GRPC_ERROR_UNREF(parser->error);
}

grpc_error *grpc_chttp2_data_parser_begin_frame(grpc_chttp2_data_parser *parser,
                                                uint8_t flags,
                                                uint32_t stream_id,
                                                grpc_chttp2_stream *s) {
  if (flags & ~GRPC_CHTTP2_DATA_FLAG_END_STREAM) {
    char *msg;
    gpr_asprintf(&msg, "unsupported data flags: 0x%02x", flags);
    grpc_error *err =
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg),
                           GRPC_ERROR_INT_STREAM_ID, (intptr_t)stream_id);
    gpr_free(msg);
    return err;
  }

  if (flags & GRPC_CHTTP2_DATA_FLAG_END_STREAM) {
    s->received_last_frame = true;
  } else {
    s->received_last_frame = false;
  }

  return GRPC_ERROR_NONE;
}

void grpc_chttp2_encode_data(uint32_t id, grpc_slice_buffer *inbuf,
                             uint32_t write_bytes, int is_eof,
                             grpc_transport_one_way_stats *stats,
                             grpc_slice_buffer *outbuf) {
  grpc_slice hdr;
  uint8_t *p;
  static const size_t header_size = 9;

  hdr = GRPC_SLICE_MALLOC(header_size);
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

  grpc_slice_buffer_move_first_no_ref(inbuf, write_bytes, outbuf);

  stats->framing_bytes += header_size;
  stats->data_bytes += write_bytes;
}

grpc_error *grpc_deframe_unprocessed_incoming_frames(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_data_parser *p, grpc_chttp2_stream *s,
    grpc_slice_buffer *slices, grpc_slice *slice_out,
    grpc_byte_stream **stream_out) {
  grpc_error *error = GRPC_ERROR_NONE;
  grpc_chttp2_transport *t = s->t;

  while (slices->count > 0) {
    uint8_t *beg = NULL;
    uint8_t *end = NULL;
    uint8_t *cur = NULL;

    grpc_slice slice = grpc_slice_buffer_take_first(slices);

    beg = GRPC_SLICE_START_PTR(slice);
    end = GRPC_SLICE_END_PTR(slice);
    cur = beg;
    uint32_t message_flags;
    char *msg;

    if (cur == end) {
      grpc_slice_unref_internal(exec_ctx, slice);
      continue;
    }

    switch (p->state) {
      case GRPC_CHTTP2_DATA_ERROR:
        p->state = GRPC_CHTTP2_DATA_ERROR;
        grpc_slice_unref_internal(exec_ctx, slice);
        return GRPC_ERROR_REF(p->error);
      case GRPC_CHTTP2_DATA_FH_0:
        p->frame_type = *cur;
        switch (p->frame_type) {
          case 0:
            p->is_frame_compressed = false; /* GPR_FALSE */
            break;
          case 1:
            p->is_frame_compressed = true; /* GPR_TRUE */
            break;
          default:
            gpr_asprintf(&msg, "Bad GRPC frame type 0x%02x", p->frame_type);
            p->error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
            p->error = grpc_error_set_int(p->error, GRPC_ERROR_INT_STREAM_ID,
                                          (intptr_t)s->id);
            gpr_free(msg);
            msg = grpc_dump_slice(slice, GPR_DUMP_HEX | GPR_DUMP_ASCII);
            p->error = grpc_error_set_str(p->error, GRPC_ERROR_STR_RAW_BYTES,
                                          grpc_slice_from_copied_string(msg));
            gpr_free(msg);
            p->error =
                grpc_error_set_int(p->error, GRPC_ERROR_INT_OFFSET, cur - beg);
            p->state = GRPC_CHTTP2_DATA_ERROR;
            grpc_slice_unref_internal(exec_ctx, slice);
            return GRPC_ERROR_REF(p->error);
        }
        if (++cur == end) {
          p->state = GRPC_CHTTP2_DATA_FH_1;
          grpc_slice_unref_internal(exec_ctx, slice);
          continue;
        }
      /* fallthrough */
      case GRPC_CHTTP2_DATA_FH_1:
        p->frame_size = ((uint32_t)*cur) << 24;
        if (++cur == end) {
          p->state = GRPC_CHTTP2_DATA_FH_2;
          grpc_slice_unref_internal(exec_ctx, slice);
          continue;
        }
      /* fallthrough */
      case GRPC_CHTTP2_DATA_FH_2:
        p->frame_size |= ((uint32_t)*cur) << 16;
        if (++cur == end) {
          p->state = GRPC_CHTTP2_DATA_FH_3;
          grpc_slice_unref_internal(exec_ctx, slice);
          continue;
        }
      /* fallthrough */
      case GRPC_CHTTP2_DATA_FH_3:
        p->frame_size |= ((uint32_t)*cur) << 8;
        if (++cur == end) {
          p->state = GRPC_CHTTP2_DATA_FH_4;
          grpc_slice_unref_internal(exec_ctx, slice);
          continue;
        }
      /* fallthrough */
      case GRPC_CHTTP2_DATA_FH_4:
        GPR_ASSERT(stream_out != NULL);
        GPR_ASSERT(p->parsing_frame == NULL);
        p->frame_size |= ((uint32_t)*cur);
        p->state = GRPC_CHTTP2_DATA_FRAME;
        ++cur;
        message_flags = 0;
        if (p->is_frame_compressed) {
          message_flags |= GRPC_WRITE_INTERNAL_COMPRESS;
        }
        p->parsing_frame = grpc_chttp2_incoming_byte_stream_create(
            exec_ctx, t, s, p->frame_size, message_flags);
        *stream_out = &p->parsing_frame->base;
        if (p->parsing_frame->remaining_bytes == 0) {
          GRPC_ERROR_UNREF(grpc_chttp2_incoming_byte_stream_finished(
              exec_ctx, p->parsing_frame, GRPC_ERROR_NONE, true));
          p->parsing_frame = NULL;
          p->state = GRPC_CHTTP2_DATA_FH_0;
        }
        s->pending_byte_stream = true;

        if (cur != end) {
          grpc_slice_buffer_undo_take_first(
              &s->unprocessed_incoming_frames_buffer,
              grpc_slice_sub(slice, (size_t)(cur - beg), (size_t)(end - beg)));
        }
        grpc_slice_unref_internal(exec_ctx, slice);
        return GRPC_ERROR_NONE;
      case GRPC_CHTTP2_DATA_FRAME: {
        GPR_ASSERT(p->parsing_frame != NULL);
        GPR_ASSERT(slice_out != NULL);
        if (cur == end) {
          grpc_slice_unref_internal(exec_ctx, slice);
          continue;
        }
        uint32_t remaining = (uint32_t)(end - cur);
        if (remaining == p->frame_size) {
          if (GRPC_ERROR_NONE != (error = grpc_chttp2_incoming_byte_stream_push(
                                      exec_ctx, p->parsing_frame,
                                      grpc_slice_sub(slice, (size_t)(cur - beg),
                                                     (size_t)(end - beg)),
                                      slice_out))) {
            grpc_slice_unref_internal(exec_ctx, slice);
            return error;
          }
          if (GRPC_ERROR_NONE !=
              (error = grpc_chttp2_incoming_byte_stream_finished(
                   exec_ctx, p->parsing_frame, GRPC_ERROR_NONE, true))) {
            grpc_slice_unref_internal(exec_ctx, slice);
            return error;
          }
          p->parsing_frame = NULL;
          p->state = GRPC_CHTTP2_DATA_FH_0;
          grpc_slice_unref_internal(exec_ctx, slice);
          return GRPC_ERROR_NONE;
        } else if (remaining < p->frame_size) {
          if (GRPC_ERROR_NONE != (error = grpc_chttp2_incoming_byte_stream_push(
                                      exec_ctx, p->parsing_frame,
                                      grpc_slice_sub(slice, (size_t)(cur - beg),
                                                     (size_t)(end - beg)),
                                      slice_out))) {
            return error;
          }
          p->frame_size -= remaining;
          grpc_slice_unref_internal(exec_ctx, slice);
          return GRPC_ERROR_NONE;
        } else {
          GPR_ASSERT(remaining > p->frame_size);
          if (GRPC_ERROR_NONE !=
              (grpc_chttp2_incoming_byte_stream_push(
                  exec_ctx, p->parsing_frame,
                  grpc_slice_sub(slice, (size_t)(cur - beg),
                                 (size_t)(cur + p->frame_size - beg)),
                  slice_out))) {
            grpc_slice_unref_internal(exec_ctx, slice);
            return error;
          }
          if (GRPC_ERROR_NONE !=
              (error = grpc_chttp2_incoming_byte_stream_finished(
                   exec_ctx, p->parsing_frame, GRPC_ERROR_NONE, true))) {
            grpc_slice_unref_internal(exec_ctx, slice);
            return error;
          }
          p->parsing_frame = NULL;
          p->state = GRPC_CHTTP2_DATA_FH_0;
          cur += p->frame_size;
          grpc_slice_buffer_undo_take_first(
              &s->unprocessed_incoming_frames_buffer,
              grpc_slice_sub(slice, (size_t)(cur - beg), (size_t)(end - beg)));
          grpc_slice_unref_internal(exec_ctx, slice);
          return GRPC_ERROR_NONE;
        }
      }
    }
  }

  return GRPC_ERROR_NONE;
}

grpc_error *grpc_chttp2_data_parser_parse(grpc_exec_ctx *exec_ctx, void *parser,
                                          grpc_chttp2_transport *t,
                                          grpc_chttp2_stream *s,
                                          grpc_slice slice, int is_last) {
  /* grpc_error *error = parse_inner_buffer(exec_ctx, p, t, s, slice); */
  s->stats.incoming.framing_bytes += GRPC_SLICE_LENGTH(slice);
  if (!s->pending_byte_stream) {
    grpc_slice_ref_internal(slice);
    grpc_slice_buffer_add(&s->frame_storage, slice);
    grpc_chttp2_maybe_complete_recv_message(exec_ctx, t, s);
  } else if (s->on_next) {
    GPR_ASSERT(s->frame_storage.length == 0);
    grpc_slice_ref_internal(slice);
    grpc_slice_buffer_add(&s->unprocessed_incoming_frames_buffer, slice);
    grpc_closure_sched(exec_ctx, s->on_next, GRPC_ERROR_NONE);
    s->on_next = NULL;
  } else {
    grpc_slice_ref_internal(slice);
    grpc_slice_buffer_add(&s->frame_storage, slice);
  }

  if (is_last && s->received_last_frame) {
    grpc_chttp2_mark_stream_closed(exec_ctx, t, s, true, false,
                                   GRPC_ERROR_NONE);
  }

  return GRPC_ERROR_NONE;
}
