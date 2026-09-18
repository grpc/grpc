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

#include "src/core/ext/transport/chttp2/transport/frame_window_update.h"

#include <grpc/support/port_platform.h>
#include <stddef.h>

#include <cstdint>

#include "src/core/ext/transport/chttp2/transport/call_tracer_wrapper.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/http2_ztrace_collector.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/ext/transport/chttp2/transport/stream_lists.h"
#include "src/core/lib/slice/byte_source.h"
#include "src/core/telemetry/stats.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/time.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

grpc_slice grpc_chttp2_window_update_create(
    uint32_t id, uint32_t window_delta,
    grpc_core::CallTracerInterface* call_tracer) {
  static const size_t frame_size = 13;
  grpc_slice slice = GRPC_SLICE_MALLOC(frame_size);
  if (call_tracer != nullptr) {
    call_tracer->RecordOutgoingBytes({frame_size, 0, 0});
  }

  GRPC_CHECK(window_delta);

  grpc_core::ByteSink sink(GRPC_SLICE_START_PTR(slice),
                           GRPC_SLICE_LENGTH(slice));
  GRPC_CHECK(sink.WriteU24BE(4));
  GRPC_CHECK(sink.WriteU8(GRPC_CHTTP2_FRAME_WINDOW_UPDATE));
  GRPC_CHECK(sink.WriteU8(0));  // flags
  GRPC_CHECK(sink.WriteU32BE(id));
  GRPC_CHECK(sink.WriteU31BE(window_delta));
  GRPC_CHECK(sink.empty());

  return slice;
}

grpc_error_handle grpc_chttp2_window_update_parser_begin_frame(
    grpc_chttp2_window_update_parser* parser, uint32_t length, uint8_t flags) {
  if (flags || length != 4) {
    return GRPC_ERROR_CREATE(absl::StrFormat(
        "invalid window update: length=%d, flags=%02x", length, flags));
  }
  parser->byte = 0;
  parser->amount = 0;
  return absl::OkStatus();
}

grpc_error_handle grpc_chttp2_window_update_parser_parse(
    void* parser, grpc_chttp2_transport* t, grpc_chttp2_stream* s,
    const grpc_slice& slice, int is_last) {
  grpc_chttp2_window_update_parser* p =
      static_cast<grpc_chttp2_window_update_parser*>(parser);

  grpc_core::ByteSource src(slice);
  while (p->byte != 4) {
    auto v = src.ReadU8();
    if (!v.has_value()) break;
    p->amount |= static_cast<uint32_t>(*v) << (8 * (3 - p->byte));
    ++p->byte;
  }

  if (s != nullptr) {
    uint64_t framing_bytes = 4u - src.remaining();
    s->call_tracer_wrapper.RecordIncomingBytes({framing_bytes, 0, 0});
  }

  if (p->byte == 4) {
    // top bit is reserved and must be ignored.
    uint32_t received_update = p->amount & 0x7fffffffu;
    if (received_update == 0) {
      return GRPC_ERROR_CREATE(
          absl::StrCat("invalid window update bytes: ", p->amount));
    }
    GRPC_CHECK(is_last);

    t->http2_ztrace_collector.Append(grpc_core::H2WindowUpdateTrace<true>{
        t->incoming_stream_id, received_update});

    if (t->incoming_stream_id != 0) {
      if (s != nullptr) {
        grpc_core::Timestamp now = grpc_core::Timestamp::Now();
        if (s->last_window_update_time != grpc_core::Timestamp::InfPast()) {
          t->http2_stats->IncrementHttp2StreamWindowUpdatePeriod(
              (now - s->last_window_update_time).millis());
        }
        s->last_window_update_time = now;
        grpc_core::chttp2::StreamFlowControl::OutgoingUpdateContext(
            &s->flow_control)
            .RecvUpdate(received_update);
        t->http2_stats->IncrementHttp2StreamRemoteWindowUpdate(received_update);
        if (grpc_chttp2_list_remove_stalled_by_stream(t, s)) {
          grpc_chttp2_mark_stream_writable(t, s);
          grpc_chttp2_initiate_write(
              t, GRPC_CHTTP2_INITIATE_WRITE_FLOW_CONTROL_UNSTALLED_BY_UPDATE);
        }
      }
    } else {
      grpc_core::chttp2::TransportFlowControl::OutgoingUpdateContext upd(
          &t->flow_control);
      grpc_core::Timestamp now = grpc_core::Timestamp::Now();
      if (t->last_window_update_time != grpc_core::Timestamp::InfPast()) {
        t->http2_stats->IncrementHttp2TransportWindowUpdatePeriod(
            (now - t->last_window_update_time).millis());
      }
      t->last_window_update_time = now;
      t->http2_stats->IncrementHttp2TransportRemoteWindowUpdate(
          received_update);
      upd.RecvUpdate(received_update);
      if (upd.Finish() == grpc_core::chttp2::StallEdge::kUnstalled) {
        grpc_chttp2_initiate_write(
            t, GRPC_CHTTP2_INITIATE_WRITE_TRANSPORT_FLOW_CONTROL_UNSTALLED);
      }
    }
  }

  return absl::OkStatus();
}
