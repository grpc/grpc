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

#include "src/core/ext/transport/chttp2/transport/frame_window_update.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

grpc_slice grpc_chttp2_window_update_create(
    uint32_t id, uint32_t window_update, grpc_transport_one_way_stats* stats) {
  static const size_t frame_size = 13;
  grpc_slice slice = GRPC_SLICE_MALLOC(frame_size);
  stats->header_bytes += frame_size;
  uint8_t* p = GRPC_SLICE_START_PTR(slice);

  GPR_ASSERT(window_update);

  *p++ = 0;
  *p++ = 0;
  *p++ = 4;
  *p++ = GRPC_CHTTP2_FRAME_WINDOW_UPDATE;
  *p++ = 0;
  *p++ = static_cast<uint8_t>(id >> 24);
  *p++ = static_cast<uint8_t>(id >> 16);
  *p++ = static_cast<uint8_t>(id >> 8);
  *p++ = static_cast<uint8_t>(id);
  *p++ = static_cast<uint8_t>(window_update >> 24);
  *p++ = static_cast<uint8_t>(window_update >> 16);
  *p++ = static_cast<uint8_t>(window_update >> 8);
  *p++ = static_cast<uint8_t>(window_update);

  return slice;
}

grpc_error* grpc_chttp2_window_update_parser_begin_frame(
    grpc_chttp2_window_update_parser* parser, uint32_t length, uint8_t flags) {
  if (flags || length != 4) {
    char* msg;
    gpr_asprintf(&msg, "invalid window update: length=%d, flags=%02x", length,
                 flags);
    grpc_error* err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    return err;
  }
  parser->byte = 0;
  parser->amount = 0;
  return GRPC_ERROR_NONE;
}

grpc_error* grpc_chttp2_window_update_parser_parse(void* parser,
                                                   grpc_chttp2_transport* t,
                                                   grpc_chttp2_stream* s,
                                                   const grpc_slice& slice,
                                                   int is_last) {
  const uint8_t* const beg = GRPC_SLICE_START_PTR(slice);
  const uint8_t* const end = GRPC_SLICE_END_PTR(slice);
  const uint8_t* cur = beg;
  grpc_chttp2_window_update_parser* p =
      static_cast<grpc_chttp2_window_update_parser*>(parser);

  while (p->byte != 4 && cur != end) {
    p->amount |= (static_cast<uint32_t>(*cur)) << (8 * (3 - p->byte));
    cur++;
    p->byte++;
  }

  if (s != nullptr) {
    s->stats.incoming.framing_bytes += static_cast<uint32_t>(end - cur);
  }

  if (p->byte == 4) {
    // top bit is reserved and must be ignored.
    uint32_t received_update = p->amount & 0x7fffffffu;
    if (received_update == 0) {
      char* msg;
      gpr_asprintf(&msg, "invalid window update bytes: %d", p->amount);
      grpc_error* err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
      gpr_free(msg);
      return err;
    }
    GPR_ASSERT(is_last);

    if (t->incoming_stream_id != 0) {
      if (s != nullptr) {
        s->flow_control->RecvUpdate(received_update);
        if (grpc_chttp2_list_remove_stalled_by_stream(t, s)) {
          grpc_chttp2_mark_stream_writable(t, s);
          grpc_chttp2_initiate_write(
              t, GRPC_CHTTP2_INITIATE_WRITE_FLOW_CONTROL_UNSTALLED_BY_UPDATE);
        }
      }
    } else {
      bool was_zero = t->flow_control->remote_window() <= 0;
      t->flow_control->RecvUpdate(received_update);
      bool is_zero = t->flow_control->remote_window() <= 0;
      if (was_zero && !is_zero) {
        grpc_chttp2_initiate_write(
            t, GRPC_CHTTP2_INITIATE_WRITE_TRANSPORT_FLOW_CONTROL_UNSTALLED);
      }
    }
  }

  return GRPC_ERROR_NONE;
}
