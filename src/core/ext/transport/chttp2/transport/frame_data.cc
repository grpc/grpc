/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/frame_data.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/transport.h"

grpc_chttp2_data_parser::~grpc_chttp2_data_parser() {
  if (parsing_frame != nullptr) {
    GRPC_ERROR_UNREF(parsing_frame->Finished(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Parser destroyed"), false));
  }
  GRPC_ERROR_UNREF(error);
}

grpc_error* grpc_chttp2_data_parser_begin_frame(grpc_chttp2_data_parser* parser,
                                                uint8_t flags,
                                                uint32_t stream_id,
                                                grpc_chttp2_stream* s) {
  if (flags & ~GRPC_CHTTP2_DATA_FLAG_END_STREAM) {
    char* msg;
    gpr_asprintf(&msg, "unsupported data flags: 0x%02x", flags);
    grpc_error* err = grpc_error_set_int(
        GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg), GRPC_ERROR_INT_STREAM_ID,
        static_cast<intptr_t>(stream_id));
    gpr_free(msg);
    return err;
  }

  if (flags & GRPC_CHTTP2_DATA_FLAG_END_STREAM) {
    s->received_last_frame = true;
    s->eos_received = true;
  } else {
    s->received_last_frame = false;
  }

  return GRPC_ERROR_NONE;
}

void grpc_chttp2_encode_data(uint32_t id, grpc_slice_buffer* inbuf,
                             uint32_t write_bytes, int is_eof,
                             grpc_transport_one_way_stats* stats,
                             grpc_slice_buffer* outbuf) {
  grpc_slice hdr;
  uint8_t* p;
  static const size_t header_size = 9;

  hdr = GRPC_SLICE_MALLOC(header_size);
  p = GRPC_SLICE_START_PTR(hdr);
  GPR_ASSERT(write_bytes < (1 << 24));
  *p++ = static_cast<uint8_t>(write_bytes >> 16);
  *p++ = static_cast<uint8_t>(write_bytes >> 8);
  *p++ = static_cast<uint8_t>(write_bytes);
  *p++ = GRPC_CHTTP2_FRAME_DATA;
  *p++ = is_eof ? GRPC_CHTTP2_DATA_FLAG_END_STREAM : 0;
  *p++ = static_cast<uint8_t>(id >> 24);
  *p++ = static_cast<uint8_t>(id >> 16);
  *p++ = static_cast<uint8_t>(id >> 8);
  *p++ = static_cast<uint8_t>(id);
  grpc_slice_buffer_add(outbuf, hdr);

  grpc_slice_buffer_move_first_no_ref(inbuf, write_bytes, outbuf);

  stats->framing_bytes += header_size;
  stats->data_bytes += write_bytes;
}

grpc_error* grpc_deframe_unprocessed_incoming_frames(
    grpc_chttp2_data_parser* p, grpc_chttp2_stream* s,
    grpc_slice_buffer* slices, grpc_slice* slice_out,
    grpc_core::OrphanablePtr<grpc_core::ByteStream>* stream_out) {
  grpc_error* error = GRPC_ERROR_NONE;
  grpc_chttp2_transport* t = s->t;

  while (slices->count > 0) {
    uint8_t* beg = nullptr;
    uint8_t* end = nullptr;
    uint8_t* cur = nullptr;

    grpc_slice slice = grpc_slice_buffer_take_first(slices);

    beg = GRPC_SLICE_START_PTR(slice);
    end = GRPC_SLICE_END_PTR(slice);
    cur = beg;
    uint32_t message_flags;
    char* msg;

    if (cur == end) {
      grpc_slice_unref_internal(slice);
      continue;
    }

    switch (p->state) {
      case GRPC_CHTTP2_DATA_ERROR:
        p->state = GRPC_CHTTP2_DATA_ERROR;
        grpc_slice_unref_internal(slice);
        return GRPC_ERROR_REF(p->error);
      case GRPC_CHTTP2_DATA_FH_0:
        s->stats.incoming.framing_bytes++;
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
                                          static_cast<intptr_t>(s->id));
            gpr_free(msg);
            msg = grpc_dump_slice(slice, GPR_DUMP_HEX | GPR_DUMP_ASCII);
            p->error = grpc_error_set_str(p->error, GRPC_ERROR_STR_RAW_BYTES,
                                          grpc_slice_from_copied_string(msg));
            gpr_free(msg);
            p->error =
                grpc_error_set_int(p->error, GRPC_ERROR_INT_OFFSET, cur - beg);
            p->state = GRPC_CHTTP2_DATA_ERROR;
            grpc_slice_unref_internal(slice);
            return GRPC_ERROR_REF(p->error);
        }
        if (++cur == end) {
          p->state = GRPC_CHTTP2_DATA_FH_1;
          grpc_slice_unref_internal(slice);
          continue;
        }
      /* fallthrough */
      case GRPC_CHTTP2_DATA_FH_1:
        s->stats.incoming.framing_bytes++;
        p->frame_size = (static_cast<uint32_t>(*cur)) << 24;
        if (++cur == end) {
          p->state = GRPC_CHTTP2_DATA_FH_2;
          grpc_slice_unref_internal(slice);
          continue;
        }
      /* fallthrough */
      case GRPC_CHTTP2_DATA_FH_2:
        s->stats.incoming.framing_bytes++;
        p->frame_size |= (static_cast<uint32_t>(*cur)) << 16;
        if (++cur == end) {
          p->state = GRPC_CHTTP2_DATA_FH_3;
          grpc_slice_unref_internal(slice);
          continue;
        }
      /* fallthrough */
      case GRPC_CHTTP2_DATA_FH_3:
        s->stats.incoming.framing_bytes++;
        p->frame_size |= (static_cast<uint32_t>(*cur)) << 8;
        if (++cur == end) {
          p->state = GRPC_CHTTP2_DATA_FH_4;
          grpc_slice_unref_internal(slice);
          continue;
        }
      /* fallthrough */
      case GRPC_CHTTP2_DATA_FH_4:
        s->stats.incoming.framing_bytes++;
        GPR_ASSERT(stream_out != nullptr);
        GPR_ASSERT(p->parsing_frame == nullptr);
        p->frame_size |= (static_cast<uint32_t>(*cur));
        if (t->channelz_socket != nullptr) {
          t->channelz_socket->RecordMessageReceived();
        }
        p->state = GRPC_CHTTP2_DATA_FRAME;
        ++cur;
        message_flags = 0;
        if (p->is_frame_compressed) {
          message_flags |= GRPC_WRITE_INTERNAL_COMPRESS;
        }
        p->parsing_frame = grpc_core::New<grpc_core::Chttp2IncomingByteStream>(
            t, s, p->frame_size, message_flags);
        stream_out->reset(p->parsing_frame);
        if (p->parsing_frame->remaining_bytes() == 0) {
          GRPC_ERROR_UNREF(p->parsing_frame->Finished(GRPC_ERROR_NONE, true));
          p->parsing_frame = nullptr;
          p->state = GRPC_CHTTP2_DATA_FH_0;
        }
        s->pending_byte_stream = true;

        if (cur != end) {
          grpc_slice_buffer_undo_take_first(
              slices, grpc_slice_sub(slice, static_cast<size_t>(cur - beg),
                                     static_cast<size_t>(end - beg)));
        }
        grpc_slice_unref_internal(slice);
        return GRPC_ERROR_NONE;
      case GRPC_CHTTP2_DATA_FRAME: {
        GPR_ASSERT(p->parsing_frame != nullptr);
        GPR_ASSERT(slice_out != nullptr);
        if (cur == end) {
          grpc_slice_unref_internal(slice);
          continue;
        }
        uint32_t remaining = static_cast<uint32_t>(end - cur);
        if (remaining == p->frame_size) {
          s->stats.incoming.data_bytes += remaining;
          if (GRPC_ERROR_NONE !=
              (error = p->parsing_frame->Push(
                   grpc_slice_sub(slice, static_cast<size_t>(cur - beg),
                                  static_cast<size_t>(end - beg)),
                   slice_out))) {
            grpc_slice_unref_internal(slice);
            return error;
          }
          if (GRPC_ERROR_NONE !=
              (error = p->parsing_frame->Finished(GRPC_ERROR_NONE, true))) {
            grpc_slice_unref_internal(slice);
            return error;
          }
          p->parsing_frame = nullptr;
          p->state = GRPC_CHTTP2_DATA_FH_0;
          grpc_slice_unref_internal(slice);
          return GRPC_ERROR_NONE;
        } else if (remaining < p->frame_size) {
          s->stats.incoming.data_bytes += remaining;
          if (GRPC_ERROR_NONE !=
              (error = p->parsing_frame->Push(
                   grpc_slice_sub(slice, static_cast<size_t>(cur - beg),
                                  static_cast<size_t>(end - beg)),
                   slice_out))) {
            return error;
          }
          p->frame_size -= remaining;
          grpc_slice_unref_internal(slice);
          return GRPC_ERROR_NONE;
        } else {
          GPR_ASSERT(remaining > p->frame_size);
          s->stats.incoming.data_bytes += p->frame_size;
          if (GRPC_ERROR_NONE !=
              p->parsing_frame->Push(
                  grpc_slice_sub(
                      slice, static_cast<size_t>(cur - beg),
                      static_cast<size_t>(cur + p->frame_size - beg)),
                  slice_out)) {
            grpc_slice_unref_internal(slice);
            return error;
          }
          if (GRPC_ERROR_NONE !=
              (error = p->parsing_frame->Finished(GRPC_ERROR_NONE, true))) {
            grpc_slice_unref_internal(slice);
            return error;
          }
          p->parsing_frame = nullptr;
          p->state = GRPC_CHTTP2_DATA_FH_0;
          cur += p->frame_size;
          grpc_slice_buffer_undo_take_first(
              slices, grpc_slice_sub(slice, static_cast<size_t>(cur - beg),
                                     static_cast<size_t>(end - beg)));
          grpc_slice_unref_internal(slice);
          return GRPC_ERROR_NONE;
        }
      }
    }
  }

  return GRPC_ERROR_NONE;
}

grpc_error* grpc_chttp2_data_parser_parse(void* parser,
                                          grpc_chttp2_transport* t,
                                          grpc_chttp2_stream* s,
                                          grpc_slice slice, int is_last) {
  if (!s->pending_byte_stream) {
    grpc_slice_ref_internal(slice);
    grpc_slice_buffer_add(&s->frame_storage, slice);
    grpc_chttp2_maybe_complete_recv_message(t, s);
  } else if (s->on_next) {
    GPR_ASSERT(s->frame_storage.length == 0);
    grpc_slice_ref_internal(slice);
    grpc_slice_buffer_add(&s->unprocessed_incoming_frames_buffer, slice);
    GRPC_CLOSURE_SCHED(s->on_next, GRPC_ERROR_NONE);
    s->on_next = nullptr;
    s->unprocessed_incoming_frames_decompressed = false;
  } else {
    grpc_slice_ref_internal(slice);
    grpc_slice_buffer_add(&s->frame_storage, slice);
  }

  if (is_last && s->received_last_frame) {
    grpc_chttp2_mark_stream_closed(t, s, true, false, GRPC_ERROR_NONE);
  }

  return GRPC_ERROR_NONE;
}
