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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/transport/frame.h"
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

grpc_error* grpc_chttp2_rst_stream_parser_begin_frame(
    grpc_chttp2_rst_stream_parser* parser, uint32_t length, uint8_t flags) {
  if (length != 4) {
    char* msg;
    gpr_asprintf(&msg, "invalid rst_stream: length=%d, flags=%02x", length,
                 flags);
    grpc_error* err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    return err;
  }
  parser->byte = 0;
  return GRPC_ERROR_NONE;
}

grpc_error* grpc_chttp2_rst_stream_parser_parse(void* parser,
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
    grpc_error* error = GRPC_ERROR_NONE;
    if (reason != GRPC_HTTP2_NO_ERROR || s->metadata_buffer[1].size == 0) {
      char* message;
      gpr_asprintf(&message, "Received RST_STREAM with error code %d", reason);
      error = grpc_error_set_int(
          grpc_error_set_str(GRPC_ERROR_CREATE_FROM_STATIC_STRING("RST_STREAM"),
                             GRPC_ERROR_STR_GRPC_MESSAGE,
                             grpc_slice_from_copied_string(message)),
          GRPC_ERROR_INT_HTTP2_ERROR, static_cast<intptr_t>(reason));
      gpr_free(message);
    }
    grpc_chttp2_mark_stream_closed(t, s, true, true, error);
  }

  return GRPC_ERROR_NONE;
}
