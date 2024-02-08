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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/frame_settings.h"

#include <string.h>

#include <string>

#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

#include <grpc/slice_buffer.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame_goaway.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http_trace.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/ext/transport/chttp2/transport/legacy_frame.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice.h"

static uint8_t* fill_header(uint8_t* out, uint32_t length, uint8_t flags) {
  *out++ = static_cast<uint8_t>(length >> 16);
  *out++ = static_cast<uint8_t>(length >> 8);
  *out++ = static_cast<uint8_t>(length);
  *out++ = GRPC_CHTTP2_FRAME_SETTINGS;
  *out++ = flags;
  *out++ = 0;
  *out++ = 0;
  *out++ = 0;
  *out++ = 0;
  return out;
}

grpc_slice grpc_chttp2_settings_ack_create(void) {
  grpc_slice output = GRPC_SLICE_MALLOC(9);
  fill_header(GRPC_SLICE_START_PTR(output), 0, GRPC_CHTTP2_FLAG_ACK);
  return output;
}

grpc_error_handle grpc_chttp2_settings_parser_begin_frame(
    grpc_chttp2_settings_parser* parser, uint32_t length, uint8_t flags,
    grpc_core::Http2Settings& settings) {
  parser->target_settings = &settings;
  parser->incoming_settings.Init(settings);
  parser->is_ack = 0;
  parser->state = GRPC_CHTTP2_SPS_ID0;
  if (flags == GRPC_CHTTP2_FLAG_ACK) {
    parser->is_ack = 1;
    if (length != 0) {
      return GRPC_ERROR_CREATE("non-empty settings ack frame received");
    }
    return absl::OkStatus();
  } else if (flags != 0) {
    return GRPC_ERROR_CREATE("invalid flags on settings frame");
  } else if (length % 6 != 0) {
    return GRPC_ERROR_CREATE("settings frames must be a multiple of six bytes");
  } else {
    return absl::OkStatus();
  }
}

grpc_error_handle grpc_chttp2_settings_parser_parse(void* p,
                                                    grpc_chttp2_transport* t,
                                                    grpc_chttp2_stream* /*s*/,
                                                    const grpc_slice& slice,
                                                    int is_last) {
  grpc_chttp2_settings_parser* parser =
      static_cast<grpc_chttp2_settings_parser*>(p);
  const uint8_t* cur = GRPC_SLICE_START_PTR(slice);
  const uint8_t* end = GRPC_SLICE_END_PTR(slice);

  if (parser->is_ack) {
    return absl::OkStatus();
  }

  for (;;) {
    switch (parser->state) {
      case GRPC_CHTTP2_SPS_ID0:
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_ID0;
          if (is_last) {
            *parser->target_settings = *parser->incoming_settings;
            t->num_pending_induced_frames++;
            grpc_slice_buffer_add(&t->qbuf, grpc_chttp2_settings_ack_create());
            grpc_chttp2_initiate_write(t,
                                       GRPC_CHTTP2_INITIATE_WRITE_SETTINGS_ACK);
            if (t->notify_on_receive_settings != nullptr) {
              grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                                      t->notify_on_receive_settings,
                                      absl::OkStatus());
              t->notify_on_receive_settings = nullptr;
            }
          }
          return absl::OkStatus();
        }
        parser->id = static_cast<uint16_t>((static_cast<uint16_t>(*cur)) << 8);
        cur++;
        ABSL_FALLTHROUGH_INTENDED;
      case GRPC_CHTTP2_SPS_ID1:
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_ID1;
          return absl::OkStatus();
        }
        parser->id = static_cast<uint16_t>(parser->id | (*cur));
        cur++;
        ABSL_FALLTHROUGH_INTENDED;
      case GRPC_CHTTP2_SPS_VAL0:
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_VAL0;
          return absl::OkStatus();
        }
        parser->value = (static_cast<uint32_t>(*cur)) << 24;
        cur++;
        ABSL_FALLTHROUGH_INTENDED;
      case GRPC_CHTTP2_SPS_VAL1:
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_VAL1;
          return absl::OkStatus();
        }
        parser->value |= (static_cast<uint32_t>(*cur)) << 16;
        cur++;
        ABSL_FALLTHROUGH_INTENDED;
      case GRPC_CHTTP2_SPS_VAL2:
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_VAL2;
          return absl::OkStatus();
        }
        parser->value |= (static_cast<uint32_t>(*cur)) << 8;
        cur++;
        ABSL_FALLTHROUGH_INTENDED;
      case GRPC_CHTTP2_SPS_VAL3: {
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_VAL3;
          return absl::OkStatus();
        } else {
          parser->state = GRPC_CHTTP2_SPS_ID0;
        }
        parser->value |= *cur;
        cur++;

        if (parser->id == grpc_core::Http2Settings::kInitialWindowSizeWireId) {
          t->initial_window_update +=
              static_cast<int64_t>(parser->value) -
              parser->incoming_settings->initial_window_size();
          if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace) ||
              GRPC_TRACE_FLAG_ENABLED(grpc_flowctl_trace)) {
            gpr_log(GPR_INFO, "%p[%s] adding %d for initial_window change", t,
                    t->is_client ? "cli" : "svr",
                    static_cast<int>(t->initial_window_update));
          }
        }
        auto error =
            parser->incoming_settings->Apply(parser->id, parser->value);
        if (error != GRPC_HTTP2_NO_ERROR) {
          grpc_chttp2_goaway_append(
              t->last_new_stream_id, error,
              grpc_slice_from_static_string("HTTP2 settings error"), &t->qbuf);
          return GRPC_ERROR_CREATE(absl::StrFormat(
              "invalid value %u passed for %s", parser->value,
              grpc_core::Http2Settings::WireIdToName(parser->id).c_str()));
        }
        if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
          gpr_log(GPR_INFO, "CHTTP2:%s:%s: got setting %s = %d",
                  t->is_client ? "CLI" : "SVR",
                  std::string(t->peer_string.as_string_view()).c_str(),
                  grpc_core::Http2Settings::WireIdToName(parser->id).c_str(),
                  parser->value);
        }
      } break;
    }
  }
}
