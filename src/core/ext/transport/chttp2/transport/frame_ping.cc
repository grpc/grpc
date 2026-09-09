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

#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <inttypes.h>
#include <string.h>

#include <algorithm>
#include <cstdint>

#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/ext/transport/chttp2/transport/ping_abuse_policy.h"
#include "src/core/ext/transport/chttp2/transport/ping_callbacks.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/slice/byte_source.h"
#include "src/core/util/grpc_check.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

grpc_slice grpc_chttp2_ping_create(uint8_t ack, uint64_t opaque_8bytes) {
  grpc_slice slice = GRPC_SLICE_MALLOC(9 + 8);
  grpc_core::ByteSink sink(GRPC_SLICE_START_PTR(slice),
                           GRPC_SLICE_LENGTH(slice));
  GRPC_CHECK(sink.WriteU24BE(8));
  GRPC_CHECK(sink.WriteU8(GRPC_CHTTP2_FRAME_PING));
  GRPC_CHECK(sink.WriteU8(ack ? 1 : 0));
  GRPC_CHECK(sink.Skip(4));  // stream id = 0
  GRPC_CHECK(sink.WriteU64BE(opaque_8bytes));
  GRPC_CHECK(sink.empty());
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
  grpc_chttp2_ping_parser* p = static_cast<grpc_chttp2_ping_parser*>(parser);

  grpc_core::ByteSource src(slice);
  while (p->byte != 8) {
    auto v = src.ReadU8();
    if (!v.has_value()) break;
    p->opaque_8bytes |= (static_cast<uint64_t>(*v) << (56 - 8 * p->byte));
    ++p->byte;
  }

  if (p->byte == 8) {
    GRPC_CHECK(is_last);
    t->http2_ztrace_collector.Append(
        grpc_core::H2PingTrace<true>{p->is_ack != 0, p->opaque_8bytes});
    if (p->is_ack) {
      GRPC_TRACE_LOG(http2_ping, INFO)
          << (t->is_client ? "CLIENT" : "SERVER") << "[" << t
          << "]: received ping ack " << p->opaque_8bytes;
      grpc_chttp2_ack_ping(t, p->opaque_8bytes);
    } else {
      if (!t->is_client) {
        const bool transport_idle =
            t->keepalive_permit_without_calls == 0 && t->stream_map.empty();
        if (GRPC_TRACE_FLAG_ENABLED(http_keepalive) ||
            GRPC_TRACE_FLAG_ENABLED(http)) {
          LOG(INFO) << "SERVER[" << t << "]: received ping " << p->opaque_8bytes
                    << ": "
                    << t->ping_abuse_policy.GetDebugString(transport_idle);
        }
        if (t->ping_abuse_policy.ReceivedOnePing(transport_idle)) {
          grpc_chttp2_exceeded_ping_strikes(t);
        }
      } else {
        GRPC_TRACE_LOG(http2_ping, INFO)
            << "CLIENT[" << t << "]: received ping " << p->opaque_8bytes;
      }
      if (t->ack_pings) {
        grpc_error_handle error =
            grpc_chttp2_increase_num_pending_induced_frames(t);
        if (GPR_UNLIKELY(!error.ok())) return error;

        if (t->ping_ack_count == t->ping_ack_capacity) {
          t->ping_ack_capacity =
              std::max(t->ping_ack_capacity * 3 / 2, size_t{3});
          t->ping_acks = static_cast<uint64_t*>(gpr_realloc(
              t->ping_acks, t->ping_ack_capacity * sizeof(*t->ping_acks)));
        }
        t->ping_acks[t->ping_ack_count++] = p->opaque_8bytes;
        grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_PING_RESPONSE);
      }
    }
  }

  return absl::OkStatus();
}
