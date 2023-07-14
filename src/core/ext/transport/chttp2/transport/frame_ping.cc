//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "src/core/ext/transport/chttp2/transport/frame_ping.h"

#include <grpc/support/port_platform.h>
#include <string.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <algorithm>
#include <initializer_list>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/ext/transport/chttp2/transport/ping_abuse_policy.h"

grpc_slice grpc_chttp2_ping_create(uint8_t ack, uint64_t opaque_8bytes) {
  grpc_slice slice = GRPC_SLICE_MALLOC(9 + 8);
  uint8_t* p = GRPC_SLICE_START_PTR(slice);

  *p++ = 0;
  *p++ = 0;
  *p++ = 8;
  *p++ = GRPC_CHTTP2_FRAME_PING;
  *p++ = ack ? 1 : 0;
  *p++ = 0;
  *p++ = 0;
  *p++ = 0;
  *p++ = 0;
  *p++ = static_cast<uint8_t>(opaque_8bytes >> 56);
  *p++ = static_cast<uint8_t>(opaque_8bytes >> 48);
  *p++ = static_cast<uint8_t>(opaque_8bytes >> 40);
  *p++ = static_cast<uint8_t>(opaque_8bytes >> 32);
  *p++ = static_cast<uint8_t>(opaque_8bytes >> 24);
  *p++ = static_cast<uint8_t>(opaque_8bytes >> 16);
  *p++ = static_cast<uint8_t>(opaque_8bytes >> 8);
  *p++ = static_cast<uint8_t>(opaque_8bytes);

  return slice;
}

grpc_error_handle grpc_chttp2_ping_parser_begin_frame(
    grpc_chttp2_ping_parser* parser, uint32_t length, uint8_t flags) {
  if (flags & 0xfe || length != 8) {
    return GRPC_ERROR_CREATE(
        absl::StrFormat("invalid ping: length=%d, flags=%02x", length, flags));
  }
  parser->byte = 0;
  parser->is_ack = flags;
  parser->opaque_8bytes = 0;
  return absl::OkStatus();
}

grpc_error_handle grpc_chttp2_ping_parser_parse(void* parser,
                                                grpc_chttp2_transport* t,
                                                grpc_chttp2_stream* /*s*/,
                                                const grpc_slice& slice,
                                                int is_last) {
  const uint8_t* const beg = GRPC_SLICE_START_PTR(slice);
  const uint8_t* const end = GRPC_SLICE_END_PTR(slice);
  const uint8_t* cur = beg;
  grpc_chttp2_ping_parser* p = static_cast<grpc_chttp2_ping_parser*>(parser);

  while (p->byte != 8 && cur != end) {
    p->opaque_8bytes |= ((static_cast<uint64_t>(*cur)) << (56 - 8 * p->byte));
    cur++;
    p->byte++;
  }

  if (p->byte == 8) {
    GPR_ASSERT(is_last);
    if (p->is_ack) {
      grpc_chttp2_ack_ping(t, p->opaque_8bytes);
    } else {
      if (!t->is_client) {
        const bool transport_idle =
            t->keepalive_permit_without_calls == 0 && t->stream_map.empty();
        if (t->ping_abuse_policy.ReceivedOnePing(transport_idle)) {
          grpc_chttp2_exceeded_ping_strikes(t);
        }
      }
      if (t->ack_pings) {
        if (t->ping_ack_count == t->ping_ack_capacity) {
          t->ping_ack_capacity =
              std::max(t->ping_ack_capacity * 3 / 2, size_t{3});
          t->ping_acks = static_cast<uint64_t*>(gpr_realloc(
              t->ping_acks, t->ping_ack_capacity * sizeof(*t->ping_acks)));
        }
        t->num_pending_induced_frames++;
        t->ping_acks[t->ping_ack_count++] = p->opaque_8bytes;
        grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_PING_RESPONSE);
      }
    }
  }

  return absl::OkStatus();
}
