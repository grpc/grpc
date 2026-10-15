```cpp
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

#include "src/core/ext/transport/chttp2/transport/frame_settings.h"

#include <grpc/support/port_platform.h>
#include <cstring>

#include <cstdint>

#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/legacy_frame.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/byte_source.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/telemetry/stats.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/grpc_log.h"
#include "src/core/util/grpc_realloc.h"

using grpc_core::http2::Http2ErrorCode;

static void fill_header(grpc_core::ByteSink& sink, uint32_t length,
                        uint8_t flags) {
  GRPC_CHECK(sink.WriteU24BE(length));
  GRPC_CHECK(sink.WriteU8(GRPC_CHTTP2_FRAME_SETTINGS));
  GRPC_CHECK(sink.WriteU8(flags));
  GRPC_CHECK(sink.WriteU32BE(0));  // stream id = 0
}

grpc_slice grpc_chttp2_settings_ack_create(void) {
  grpc_slice output = GRPC_SLICE_MALLOC(9);
  grpc_core::ByteSink sink(GRPC_SLICE_START_PTR(output),
                           GRPC_SLICE_LENGTH(output));
  fill_header(sink, 0, GRPC_CHTTP2_FLAG_ACK);
  GRPC_CHECK(sink.empty());
  return output;
}

grpc_error_handle grpc_chttp2_settings_parser_parse(
    void* p, grpc_chttp2_transport* t, const grpc_slice& slice, int is_last) {
  grpc_chttp2_settings_parser* parser =
      static_cast<grpc_chttp2_settings_parser*>(p);

  if (parser->is_ack) {
    t->http2_ztrace_collector.Append(
        grpc_core::H2SettingsTrace<true>{true, {}});
    return absl::OkStatus();
  }

  grpc_core::ByteSource src(slice);

  for (;;) {
    switch (parser->state) {
      case GRPC_CHTTP2_SPS_ID0: {
        auto v = src.ReadU8();
        if (!v.has_value()) {
          parser->state = GRPC_CHTTP2_SPS_ID0;
          if (is_last) {
            grpc_core::Http2Settings* target_settings = &t->peer_settings;
            target_settings->set_initial_window_size(
                grpc_core::Http2Settings::kDefaultInitialWindowSize);
            t->http2_ztrace_collector.Append(
                grpc_core::H2SettingsTrace<true>{
                    false, target_settings->ToMap()});
          }
          return absl::OkStatus();
        }
        parser->id = static_cast<uint16_t>(
            static_cast<uint16_t>(*v) << 8);
        [[fallthrough]];
      }

      case GRPC_CHTTP2_SPS_ID1: {
        auto v = src.ReadU8();
        if (!v.has_value()) {
          parser->state = GRPC_CHTTP2_SPS_ID1;
          return absl::OkStatus();
        }
        parser->id |= static_cast<uint16_t>(*v);
        [[fallthrough]];
      }

      case GRPC_CHTTP2_SPS_VAL0: {
        auto v = src.ReadU8();
        if (!v.has_value()) {
          parser->state = GRPC_CHTTP2_SPS_VAL0;
          return absl::OkStatus();
        }
        parser->value = static_cast<uint32_t>(*v) << 24;
        [[fallthrough]];
      }

      case GRPC_CHTTP2_SPS_VAL1: {
        auto v = src.ReadU8();
        if (!v.has_value()) {
          parser->state = GRPC_CHTTP2_SPS_VAL1;
          return absl::OkStatus();
        }
        parser->value |= static_cast<uint32_t>(*v) << 16;
        [[fallthrough]];
      }

      case GRPC_CHTTP2_SPS_VAL2: {
        auto v = src.ReadU8();
        if (!v.has_value()) {
          parser->state = GRPC_CHTTP2_SPS_VAL2;
          return absl::OkStatus();
        }
        parser->value |= static_cast<uint32_t>(*v) << 8;
        [[fallthrough]];
      }

      case GRPC_CHTTP2_SPS_VAL3: {
        auto v = src.ReadU8();
        if (!v.has_value()) {
          parser->state = GRPC_CHTTP2_SPS_VAL3;
          return absl::OkStatus();
        }
        parser->value |= static_cast<uint32_t>(*v);

        if (parser->id ==
            grpc_core::Http2Settings::kInitialWindowSizeWireId) {
          t->initial_window_update +=
              static_cast<int64_t>(static_cast<int32_t>(parser->value)) -
              static_cast<int64_t>(
                  t->peer_settings.initial_window_size());
        }

        if (parser->id ==
            grpc_core::Http2Settings::kInitialWindowSizeWireId) {
          // Defer updating peer settings window to the frame completion so we
          // don't affect flow control calculations while parsing a single
          // SETTINGS frame.
          t->peer_settings.set_initial_window_size(parser->value);
        }

        t->http2_ztrace_collector.Append(
            grpc_core::H2SettingsTrace<false>{
                false, {{parser->id, parser->value}}});

        parser->state = GRPC_CHTTP2_SPS_ID0;
      } break;
    }
  }
}
```
