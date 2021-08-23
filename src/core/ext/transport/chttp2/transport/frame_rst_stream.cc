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

#include "src/core/ext/transport/chttp2/transport/frame_rst_stream.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/transport/http2_errors.h"

grpc_slice grpc_chttp2_rst_stream_create(uint32_t id, uint32_t code,
                                         grpc_transport_one_way_stats* stats) {
  static const size_t frame_size = 13;
  grpc_slice slice = GRPC_SLICE_MALLOC(frame_size);
  if (stats != nullptr) stats->framing_bytes += frame_size;
  uint8_t* p = GRPC_SLICE_START_PTR(slice);

  // Frame size.
  *p++ = 0;
  *p++ = 0;
  *p++ = 4;
  // Frame type.
  *p++ = GRPC_CHTTP2_FRAME_RST_STREAM;
  // Flags.
  *p++ = 0;
  // Stream ID.
  *p++ = static_cast<uint8_t>(id >> 24);
  *p++ = static_cast<uint8_t>(id >> 16);
  *p++ = static_cast<uint8_t>(id >> 8);
  *p++ = static_cast<uint8_t>(id);
  // Error code.
  *p++ = static_cast<uint8_t>(code >> 24);
  *p++ = static_cast<uint8_t>(code >> 16);
  *p++ = static_cast<uint8_t>(code >> 8);
  *p++ = static_cast<uint8_t>(code);

  return slice;
}

void grpc_chttp2_add_rst_stream_to_next_write(
    grpc_chttp2_transport* t, uint32_t id, uint32_t code,
    grpc_transport_one_way_stats* stats) {
  t->num_pending_induced_frames++;
  grpc_slice_buffer_add(&t->qbuf,
                        grpc_chttp2_rst_stream_create(id, code, stats));
}

grpc_error_handle grpc_chttp2_rst_stream_parser_begin_frame(
    grpc_chttp2_rst_stream_parser* parser, uint32_t length, uint8_t flags) {
  if (length != 4) {
    return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrFormat("invalid rst_stream: length=%d, flags=%02x", length,
                        flags)
            .c_str());
  }
  parser->byte = 0;
  return GRPC_ERROR_NONE;
}

grpc_error_handle grpc_chttp2_rst_stream_parser_parse(void* parser,
                                                      grpc_chttp2_transport* t,
                                                      grpc_chttp2_stream* s,
                                                      const grpc_slice& slice,
                                                      int is_last) {
  const uint8_t* const beg = GRPC_SLICE_START_PTR(slice);
  const uint8_t* const end = GRPC_SLICE_END_PTR(slice);
  const uint8_t* cur = beg;
  grpc_chttp2_rst_stream_parser* p =
      static_cast<grpc_chttp2_rst_stream_parser*>(parser);

  while (p->byte != 4 && cur != end) {
    p->reason_bytes[p->byte] = *cur;
    cur++;
    p->byte++;
  }
  s->stats.incoming.framing_bytes += static_cast<uint64_t>(end - cur);

  if (p->byte == 4) {
    GPR_ASSERT(is_last);
    uint32_t reason = ((static_cast<uint32_t>(p->reason_bytes[0])) << 24) |
                      ((static_cast<uint32_t>(p->reason_bytes[1])) << 16) |
                      ((static_cast<uint32_t>(p->reason_bytes[2])) << 8) |
                      ((static_cast<uint32_t>(p->reason_bytes[3])));
    grpc_error_handle error = GRPC_ERROR_NONE;
    if (reason != GRPC_HTTP2_NO_ERROR || s->metadata_buffer[1].size == 0) {
      error = grpc_error_set_int(
          grpc_error_set_str(
              GRPC_ERROR_CREATE_FROM_STATIC_STRING("RST_STREAM"),
              GRPC_ERROR_STR_GRPC_MESSAGE,
              grpc_slice_from_cpp_string(absl::StrCat(
                  "Received RST_STREAM with error code ", reason))),
          GRPC_ERROR_INT_HTTP2_ERROR, static_cast<intptr_t>(reason));
    }
    grpc_chttp2_mark_stream_closed(t, s, true, true, error);
  }

  return GRPC_ERROR_NONE;
}
