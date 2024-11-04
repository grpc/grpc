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

#include <grpc/event_engine/event_engine.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/port_platform.h>
#include <inttypes.h>
#include <string.h>

#include <atomic>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/variant.h"
#include "src/core/channelz/channelz.h"
#include "src/core/ext/transport/chttp2/transport/call_tracer_wrapper.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame_data.h"
#include "src/core/ext/transport/chttp2/transport/frame_goaway.h"
#include "src/core/ext/transport/chttp2/transport/frame_ping.h"
#include "src/core/ext/transport/chttp2/transport/frame_rst_stream.h"
#include "src/core/ext/transport/chttp2/transport/frame_security.h"
#include "src/core/ext/transport/chttp2/transport/frame_settings.h"
#include "src/core/ext/transport/chttp2/transport/frame_window_update.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser_table.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/ext/transport/chttp2/transport/legacy_frame.h"
#include "src/core/ext/transport/chttp2/transport/ping_rate_policy.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/bdp_estimator.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/http2_errors.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/telemetry/call_tracer.h"
#include "src/core/telemetry/stats.h"
#include "src/core/telemetry/stats_data.h"
#include "src/core/util/random_early_detection.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/status_helper.h"

using grpc_core::HPackParser;

static grpc_error_handle init_frame_parser(grpc_chttp2_transport* t,
                                           size_t& requests_started);
static grpc_error_handle init_header_frame_parser(grpc_chttp2_transport* t,
                                                  int is_continuation,
                                                  size_t& requests_started);
static grpc_error_handle init_data_frame_parser(grpc_chttp2_transport* t);
static grpc_error_handle init_rst_stream_parser(grpc_chttp2_transport* t);
static grpc_error_handle init_settings_frame_parser(grpc_chttp2_transport* t);
static grpc_error_handle init_window_update_frame_parser(
    grpc_chttp2_transport* t);
static grpc_error_handle init_ping_parser(grpc_chttp2_transport* t);
static grpc_error_handle init_goaway_parser(grpc_chttp2_transport* t);
static grpc_error_handle init_security_frame_parser(grpc_chttp2_transport* t);
static grpc_error_handle init_non_header_skip_frame_parser(
    grpc_chttp2_transport* t);

static grpc_error_handle parse_frame_slice(grpc_chttp2_transport* t,
                                           const grpc_slice& slice,
                                           int is_last);

static char get_utf8_safe_char(char c) {
  return static_cast<unsigned char>(c) < 128 ? c : 32;
}

uint32_t grpc_chttp2_min_read_progress_size(grpc_chttp2_transport* t) {
  switch (t->deframe_state) {
    case GRPC_DTS_CLIENT_PREFIX_0:
    case GRPC_DTS_CLIENT_PREFIX_1:
    case GRPC_DTS_CLIENT_PREFIX_2:
    case GRPC_DTS_CLIENT_PREFIX_3:
    case GRPC_DTS_CLIENT_PREFIX_4:
    case GRPC_DTS_CLIENT_PREFIX_5:
    case GRPC_DTS_CLIENT_PREFIX_6:
    case GRPC_DTS_CLIENT_PREFIX_7:
    case GRPC_DTS_CLIENT_PREFIX_8:
    case GRPC_DTS_CLIENT_PREFIX_9:
    case GRPC_DTS_CLIENT_PREFIX_10:
    case GRPC_DTS_CLIENT_PREFIX_11:
    case GRPC_DTS_CLIENT_PREFIX_12:
    case GRPC_DTS_CLIENT_PREFIX_13:
    case GRPC_DTS_CLIENT_PREFIX_14:
    case GRPC_DTS_CLIENT_PREFIX_15:
    case GRPC_DTS_CLIENT_PREFIX_16:
    case GRPC_DTS_CLIENT_PREFIX_17:
    case GRPC_DTS_CLIENT_PREFIX_18:
    case GRPC_DTS_CLIENT_PREFIX_19:
    case GRPC_DTS_CLIENT_PREFIX_20:
    case GRPC_DTS_CLIENT_PREFIX_21:
    case GRPC_DTS_CLIENT_PREFIX_22:
    case GRPC_DTS_CLIENT_PREFIX_23:
      // Need the client prefix *and* the first fixed header to make progress.
      return 9 + 24 - (t->deframe_state - GRPC_DTS_CLIENT_PREFIX_0);
    case GRPC_DTS_FH_0:
    case GRPC_DTS_FH_1:
    case GRPC_DTS_FH_2:
    case GRPC_DTS_FH_3:
    case GRPC_DTS_FH_4:
    case GRPC_DTS_FH_5:
    case GRPC_DTS_FH_6:
    case GRPC_DTS_FH_7:
    case GRPC_DTS_FH_8:
      return 9 - (t->deframe_state - GRPC_DTS_FH_0);
    case GRPC_DTS_FRAME:
      return t->incoming_frame_size;
  }
  GPR_UNREACHABLE_CODE(return 1);
}

namespace {
struct KnownFlag {
  uint8_t flag;
  absl::string_view name;
};

std::string MakeFrameTypeString(absl::string_view frame_type, uint8_t flags,
                                std::initializer_list<KnownFlag> known_flags) {
  std::string result(frame_type);
  for (const KnownFlag& known_flag : known_flags) {
    if (flags & known_flag.flag) {
      absl::StrAppend(&result, ":", known_flag.name);
      flags &= ~known_flag.flag;
    }
  }
  if (flags != 0) {
    absl::StrAppend(&result, ":UNKNOWN_FLAGS=0x",
                    absl::Hex(flags, absl::kZeroPad2));
  }
  return result;
}

std::string FrameTypeString(uint8_t frame_type, uint8_t flags) {
  switch (frame_type) {
    case GRPC_CHTTP2_FRAME_DATA:
      return MakeFrameTypeString(
          "DATA", flags, {{GRPC_CHTTP2_DATA_FLAG_END_STREAM, "END_STREAM"}});
    case GRPC_CHTTP2_FRAME_HEADER:
      return MakeFrameTypeString(
          "HEADERS", flags,
          {{GRPC_CHTTP2_DATA_FLAG_END_STREAM, "END_STREAM"},
           {GRPC_CHTTP2_DATA_FLAG_END_HEADERS, "END_HEADERS"},
           {GRPC_CHTTP2_FLAG_HAS_PRIORITY, "PRIORITY"}});
    case GRPC_CHTTP2_FRAME_CONTINUATION:
      return MakeFrameTypeString(
          "HEADERS", flags,
          {{GRPC_CHTTP2_DATA_FLAG_END_STREAM, "END_STREAM"},
           {GRPC_CHTTP2_DATA_FLAG_END_HEADERS, "END_HEADERS"},
           {GRPC_CHTTP2_FLAG_HAS_PRIORITY, "PRIORITY"}});
    case GRPC_CHTTP2_FRAME_RST_STREAM:
      return MakeFrameTypeString("RST_STREAM", flags, {});
    case GRPC_CHTTP2_FRAME_SETTINGS:
      return MakeFrameTypeString("SETTINGS", flags,
                                 {{GRPC_CHTTP2_FLAG_ACK, "ACK"}});
    case GRPC_CHTTP2_FRAME_PING:
      return MakeFrameTypeString("PING", flags,
                                 {{GRPC_CHTTP2_FLAG_ACK, "ACK"}});
    case GRPC_CHTTP2_FRAME_GOAWAY:
      return MakeFrameTypeString("GOAWAY", flags, {});
    case GRPC_CHTTP2_FRAME_WINDOW_UPDATE:
      return MakeFrameTypeString("WINDOW_UPDATE", flags, {});
    case GRPC_CHTTP2_FRAME_SECURITY:
      return MakeFrameTypeString("SECURITY", flags, {});
    default:
      return MakeFrameTypeString(
          absl::StrCat("UNKNOWN_FRAME_TYPE_", static_cast<int>(frame_type)),
          flags, {});
  }
}
}  // namespace

absl::variant<size_t, absl::Status> grpc_chttp2_perform_read(
    grpc_chttp2_transport* t, const grpc_slice& slice,
    size_t& requests_started) {
  GRPC_LATENT_SEE_INNER_SCOPE("grpc_chttp2_perform_read");

  const uint8_t* beg = GRPC_SLICE_START_PTR(slice);
  const uint8_t* end = GRPC_SLICE_END_PTR(slice);
  const uint8_t* cur = beg;
  grpc_error_handle err;

  if (cur == end) return absl::OkStatus();

  switch (t->deframe_state) {
    case GRPC_DTS_CLIENT_PREFIX_0:
    case GRPC_DTS_CLIENT_PREFIX_1:
    case GRPC_DTS_CLIENT_PREFIX_2:
    case GRPC_DTS_CLIENT_PREFIX_3:
    case GRPC_DTS_CLIENT_PREFIX_4:
    case GRPC_DTS_CLIENT_PREFIX_5:
    case GRPC_DTS_CLIENT_PREFIX_6:
    case GRPC_DTS_CLIENT_PREFIX_7:
    case GRPC_DTS_CLIENT_PREFIX_8:
    case GRPC_DTS_CLIENT_PREFIX_9:
    case GRPC_DTS_CLIENT_PREFIX_10:
    case GRPC_DTS_CLIENT_PREFIX_11:
    case GRPC_DTS_CLIENT_PREFIX_12:
    case GRPC_DTS_CLIENT_PREFIX_13:
    case GRPC_DTS_CLIENT_PREFIX_14:
    case GRPC_DTS_CLIENT_PREFIX_15:
    case GRPC_DTS_CLIENT_PREFIX_16:
    case GRPC_DTS_CLIENT_PREFIX_17:
    case GRPC_DTS_CLIENT_PREFIX_18:
    case GRPC_DTS_CLIENT_PREFIX_19:
    case GRPC_DTS_CLIENT_PREFIX_20:
    case GRPC_DTS_CLIENT_PREFIX_21:
    case GRPC_DTS_CLIENT_PREFIX_22:
    case GRPC_DTS_CLIENT_PREFIX_23:
      while (cur != end && t->deframe_state != GRPC_DTS_FH_0) {
        if (*cur != GRPC_CHTTP2_CLIENT_CONNECT_STRING[t->deframe_state]) {
          return GRPC_ERROR_CREATE(absl::StrFormat(
              "Connect string mismatch: expected '%c' (%d) got '%c' (%d) "
              "at byte %d",
              get_utf8_safe_char(
                  GRPC_CHTTP2_CLIENT_CONNECT_STRING[t->deframe_state]),
              static_cast<int>(static_cast<uint8_t>(
                  GRPC_CHTTP2_CLIENT_CONNECT_STRING[t->deframe_state])),
              get_utf8_safe_char(*cur), static_cast<int>(*cur),
              t->deframe_state));
        }
        ++cur;
        // NOLINTNEXTLINE(bugprone-misplaced-widening-cast)
        t->deframe_state = static_cast<grpc_chttp2_deframe_transport_state>(
            1 + static_cast<int>(t->deframe_state));
      }
      if (cur == end) {
        return absl::OkStatus();
      }
    dts_fh_0:
      if (requests_started >= t->max_requests_per_read) {
        t->deframe_state = GRPC_DTS_FH_0;
        return static_cast<size_t>(cur - beg);
      }
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_DTS_FH_0:
      DCHECK_LT(cur, end);
      t->incoming_frame_size = (static_cast<uint32_t>(*cur)) << 16;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_1;
        return absl::OkStatus();
      }
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_DTS_FH_1:
      DCHECK_LT(cur, end);
      t->incoming_frame_size |= (static_cast<uint32_t>(*cur)) << 8;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_2;
        return absl::OkStatus();
      }
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_DTS_FH_2:
      DCHECK_LT(cur, end);
      t->incoming_frame_size |= *cur;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_3;
        return absl::OkStatus();
      }
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_DTS_FH_3:
      DCHECK_LT(cur, end);
      t->incoming_frame_type = *cur;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_4;
        return absl::OkStatus();
      }
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_DTS_FH_4:
      DCHECK_LT(cur, end);
      t->incoming_frame_flags = *cur;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_5;
        return absl::OkStatus();
      }
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_DTS_FH_5:
      DCHECK_LT(cur, end);
      t->incoming_stream_id = ((static_cast<uint32_t>(*cur)) & 0x7f) << 24;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_6;
        return absl::OkStatus();
      }
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_DTS_FH_6:
      DCHECK_LT(cur, end);
      t->incoming_stream_id |= (static_cast<uint32_t>(*cur)) << 16;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_7;
        return absl::OkStatus();
      }
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_DTS_FH_7:
      DCHECK_LT(cur, end);
      t->incoming_stream_id |= (static_cast<uint32_t>(*cur)) << 8;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_8;
        return absl::OkStatus();
      }
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_DTS_FH_8:
      DCHECK_LT(cur, end);
      t->incoming_stream_id |= (static_cast<uint32_t>(*cur));
      GRPC_TRACE_LOG(http, INFO)
          << "INCOMING[" << t << "]: "
          << FrameTypeString(t->incoming_frame_type, t->incoming_frame_flags)
          << " len:" << t->incoming_frame_size
          << absl::StrFormat(" id:0x%08x", t->incoming_stream_id);
      t->deframe_state = GRPC_DTS_FRAME;
      err = init_frame_parser(t, requests_started);
      if (!err.ok()) {
        return err;
      }
      if (t->incoming_frame_size == 0) {
        err = parse_frame_slice(t, grpc_empty_slice(), 1);
        if (!err.ok()) {
          return err;
        }
        t->incoming_stream = nullptr;
        if (++cur == end) {
          t->deframe_state = GRPC_DTS_FH_0;
          return absl::OkStatus();
        }
        goto dts_fh_0;  // loop
      } else if (t->incoming_frame_size >
                 t->settings.acked().max_frame_size()) {
        return GRPC_ERROR_CREATE(absl::StrFormat(
            "Frame size %d is larger than max frame size %d",
            t->incoming_frame_size, t->settings.acked().max_frame_size()));
      }
      if (++cur == end) {
        return absl::OkStatus();
      }
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_DTS_FRAME:
      DCHECK_LT(cur, end);
      if (static_cast<uint32_t>(end - cur) == t->incoming_frame_size) {
        err = parse_frame_slice(
            t,
            grpc_slice_sub_no_ref(slice, static_cast<size_t>(cur - beg),
                                  static_cast<size_t>(end - beg)),
            1);
        if (!err.ok()) {
          return err;
        }
        t->deframe_state = GRPC_DTS_FH_0;
        t->incoming_stream = nullptr;
        return absl::OkStatus();
      } else if (static_cast<uint32_t>(end - cur) > t->incoming_frame_size) {
        size_t cur_offset = static_cast<size_t>(cur - beg);
        err = parse_frame_slice(
            t,
            grpc_slice_sub_no_ref(slice, cur_offset,
                                  cur_offset + t->incoming_frame_size),
            1);
        if (!err.ok()) {
          return err;
        }
        cur += t->incoming_frame_size;
        t->incoming_stream = nullptr;
        if (t->incoming_frame_type == GRPC_CHTTP2_FRAME_RST_STREAM) {
          requests_started = std::numeric_limits<size_t>::max();
        }
        goto dts_fh_0;  // loop
      } else {
        err = parse_frame_slice(
            t,
            grpc_slice_sub_no_ref(slice, static_cast<size_t>(cur - beg),
                                  static_cast<size_t>(end - beg)),
            0);
        if (!err.ok()) {
          return err;
        }
        t->incoming_frame_size -= static_cast<uint32_t>(end - cur);
        return absl::OkStatus();
      }
      GPR_UNREACHABLE_CODE(return absl::OkStatus());
  }

  GPR_UNREACHABLE_CODE(return absl::OkStatus());
}

static grpc_error_handle init_frame_parser(grpc_chttp2_transport* t,
                                           size_t& requests_started) {
  if (t->is_first_frame &&
      t->incoming_frame_type != GRPC_CHTTP2_FRAME_SETTINGS) {
    return GRPC_ERROR_CREATE(absl::StrCat(
        "Expected SETTINGS frame as the first frame, got frame type ",
        t->incoming_frame_type));
  }
  t->is_first_frame = false;
  if (t->expect_continuation_stream_id != 0) {
    if (t->incoming_frame_type != GRPC_CHTTP2_FRAME_CONTINUATION) {
      return GRPC_ERROR_CREATE(
          absl::StrFormat("Expected CONTINUATION frame, got frame type %02x",
                          t->incoming_frame_type));
    }
    if (t->expect_continuation_stream_id != t->incoming_stream_id) {
      return GRPC_ERROR_CREATE(absl::StrFormat(
          "Expected CONTINUATION frame for grpc_chttp2_stream %08x, got "
          "grpc_chttp2_stream %08x",
          t->expect_continuation_stream_id, t->incoming_stream_id));
    }
    return init_header_frame_parser(t, 1, requests_started);
  }
  switch (t->incoming_frame_type) {
    case GRPC_CHTTP2_FRAME_DATA:
      return init_data_frame_parser(t);
    case GRPC_CHTTP2_FRAME_HEADER:
      return init_header_frame_parser(t, 0, requests_started);
    case GRPC_CHTTP2_FRAME_CONTINUATION:
      return GRPC_ERROR_CREATE("Unexpected CONTINUATION frame");
    case GRPC_CHTTP2_FRAME_RST_STREAM:
      return init_rst_stream_parser(t);
    case GRPC_CHTTP2_FRAME_SETTINGS:
      return init_settings_frame_parser(t);
    case GRPC_CHTTP2_FRAME_WINDOW_UPDATE:
      return init_window_update_frame_parser(t);
    case GRPC_CHTTP2_FRAME_PING:
      return init_ping_parser(t);
    case GRPC_CHTTP2_FRAME_GOAWAY:
      return init_goaway_parser(t);
    case GRPC_CHTTP2_FRAME_SECURITY:
      if (!t->settings.peer().allow_security_frame()) {
        if (GRPC_TRACE_FLAG_ENABLED(http)) {
          LOG(ERROR) << "Security frame received but not allowed, ignoring";
        }
        return init_non_header_skip_frame_parser(t);
      }
      return init_security_frame_parser(t);
    default:
      GRPC_TRACE_LOG(http, ERROR)
          << "Unknown frame type "
          << absl::StrFormat("%02x", t->incoming_frame_type);
      return init_non_header_skip_frame_parser(t);
  }
}

static grpc_error_handle skip_parser(void* /*parser*/,
                                     grpc_chttp2_transport* /*t*/,
                                     grpc_chttp2_stream* /*s*/,
                                     const grpc_slice& /*slice*/,
                                     int /*is_last*/) {
  return absl::OkStatus();
}

static HPackParser::Boundary hpack_boundary_type(grpc_chttp2_transport* t,
                                                 bool is_eoh) {
  if (is_eoh) {
    if (t->header_eof) {
      return HPackParser::Boundary::EndOfStream;
    } else {
      return HPackParser::Boundary::EndOfHeaders;
    }
  } else {
    return HPackParser::Boundary::None;
  }
}

static HPackParser::LogInfo hpack_parser_log_info(
    grpc_chttp2_transport* t, HPackParser::LogInfo::Type type) {
  return HPackParser::LogInfo{
      t->incoming_stream_id,
      type,
      t->is_client,
  };
}

static grpc_error_handle init_header_skip_frame_parser(
    grpc_chttp2_transport* t, HPackParser::Priority priority_type,
    bool is_eoh) {
  t->parser = grpc_chttp2_transport::Parser{
      "header", grpc_chttp2_header_parser_parse, &t->hpack_parser};
  t->hpack_parser.BeginFrame(
      nullptr,
      /*metadata_size_soft_limit=*/
      t->max_header_list_size_soft_limit,
      /*metadata_size_hard_limit=*/
      t->settings.acked().max_header_list_size(),
      hpack_boundary_type(t, is_eoh), priority_type,
      hpack_parser_log_info(t, HPackParser::LogInfo::kDontKnow));
  return absl::OkStatus();
}

static grpc_error_handle init_non_header_skip_frame_parser(
    grpc_chttp2_transport* t) {
  t->parser =
      grpc_chttp2_transport::Parser{"skip_parser", skip_parser, nullptr};
  return absl::OkStatus();
}

void grpc_chttp2_parsing_become_skip_parser(grpc_chttp2_transport* t) {
  if (t->parser.parser == grpc_chttp2_header_parser_parse) {
    t->hpack_parser.StopBufferingFrame();
  } else {
    t->parser =
        grpc_chttp2_transport::Parser{"skip_parser", skip_parser, nullptr};
  }
}

static grpc_error_handle init_data_frame_parser(grpc_chttp2_transport* t) {
  // Update BDP accounting since we have received a data frame.
  grpc_core::BdpEstimator* bdp_est = t->flow_control.bdp_estimator();
  if (bdp_est) {
    if (t->bdp_ping_blocked) {
      t->bdp_ping_blocked = false;
      schedule_bdp_ping_locked(t->Ref());
    }
    bdp_est->AddIncomingBytes(t->incoming_frame_size);
  }
  grpc_chttp2_stream* s =
      grpc_chttp2_parsing_lookup_stream(t, t->incoming_stream_id);
  absl::Status status;
  grpc_core::chttp2::FlowControlAction action;
  if (s == nullptr) {
    grpc_core::chttp2::TransportFlowControl::IncomingUpdateContext upd(
        &t->flow_control);
    status = upd.RecvData(t->incoming_frame_size);
    action = upd.MakeAction();
  } else {
    grpc_core::chttp2::StreamFlowControl::IncomingUpdateContext upd(
        &s->flow_control);
    status = upd.RecvData(t->incoming_frame_size);
    action = upd.MakeAction();
  }
  grpc_chttp2_act_on_flowctl_action(action, t, s);
  if (!status.ok()) {
    goto error_handler;
  }
  if (s == nullptr) {
    return init_non_header_skip_frame_parser(t);
  }
  s->received_bytes += t->incoming_frame_size;
  s->call_tracer_wrapper.RecordIncomingBytes({9, 0, 0});
  if (s->read_closed) {
    return init_non_header_skip_frame_parser(t);
  }
  status =
      grpc_chttp2_data_parser_begin_frame(t->incoming_frame_flags, s->id, s);
error_handler:
  if (status.ok()) {
    t->incoming_stream = s;
    t->parser = grpc_chttp2_transport::Parser{
        "data", grpc_chttp2_data_parser_parse, nullptr};
    t->ping_rate_policy.ReceivedDataFrame();
    return absl::OkStatus();
  } else if (s != nullptr) {
    // handle stream errors by closing the stream
    grpc_chttp2_mark_stream_closed(t, s, true, false,
                                   absl_status_to_grpc_error(status));
    grpc_chttp2_add_rst_stream_to_next_write(t, t->incoming_stream_id,
                                             GRPC_HTTP2_PROTOCOL_ERROR,
                                             &s->call_tracer_wrapper);
    return init_non_header_skip_frame_parser(t);
  } else {
    return absl_status_to_grpc_error(status);
  }
}

static grpc_error_handle init_header_frame_parser(grpc_chttp2_transport* t,
                                                  int is_continuation,
                                                  size_t& requests_started) {
  const bool is_eoh =
      (t->incoming_frame_flags & GRPC_CHTTP2_DATA_FLAG_END_HEADERS) != 0;
  grpc_chttp2_stream* s;

  // TODO(ctiller): when to increment header_frames_received?

  if (is_eoh) {
    t->expect_continuation_stream_id = 0;
  } else {
    t->expect_continuation_stream_id = t->incoming_stream_id;
  }

  if (!is_continuation) {
    t->header_eof =
        (t->incoming_frame_flags & GRPC_CHTTP2_DATA_FLAG_END_STREAM) != 0;
  }

  const auto priority_type = !is_continuation && (t->incoming_frame_flags &
                                                  GRPC_CHTTP2_FLAG_HAS_PRIORITY)
                                 ? HPackParser::Priority::Included
                                 : HPackParser::Priority::None;

  t->ping_rate_policy.ReceivedDataFrame();

  // could be a new grpc_chttp2_stream or an existing grpc_chttp2_stream
  s = grpc_chttp2_parsing_lookup_stream(t, t->incoming_stream_id);
  if (s == nullptr) {
    if (GPR_UNLIKELY(is_continuation)) {
      GRPC_CHTTP2_IF_TRACING(ERROR)
          << "grpc_chttp2_stream disbanded before CONTINUATION received";
      return init_header_skip_frame_parser(t, priority_type, is_eoh);
    }
    if (t->is_client) {
      if (GPR_LIKELY((t->incoming_stream_id & 1) &&
                     t->incoming_stream_id < t->next_stream_id)) {
        // this is an old (probably cancelled) grpc_chttp2_stream
      } else {
        GRPC_CHTTP2_IF_TRACING(ERROR)
            << "ignoring new grpc_chttp2_stream creation on client";
      }
      return init_header_skip_frame_parser(t, priority_type, is_eoh);
    } else if (GPR_UNLIKELY(t->last_new_stream_id >= t->incoming_stream_id)) {
      GRPC_CHTTP2_IF_TRACING(ERROR)
          << "ignoring out of order new grpc_chttp2_stream request on server; "
             "last grpc_chttp2_stream id="
          << t->last_new_stream_id
          << ", new grpc_chttp2_stream id=" << t->incoming_stream_id;
      return init_header_skip_frame_parser(t, priority_type, is_eoh);
    } else if (GPR_UNLIKELY((t->incoming_stream_id & 1) == 0)) {
      GRPC_CHTTP2_IF_TRACING(ERROR)
          << "ignoring grpc_chttp2_stream with non-client generated index "
          << t->incoming_stream_id;
      return init_header_skip_frame_parser(t, priority_type, is_eoh);
    } else if (GPR_UNLIKELY(t->stream_map.size() + t->extra_streams >=
                            t->settings.acked().max_concurrent_streams())) {
      ++t->num_pending_induced_frames;
      grpc_slice_buffer_add(&t->qbuf, grpc_chttp2_rst_stream_create(
                                          t->incoming_stream_id,
                                          GRPC_HTTP2_REFUSED_STREAM, nullptr));
      grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_RST_STREAM);
      return init_header_skip_frame_parser(t, priority_type, is_eoh);
    } else if (grpc_core::IsRqFastRejectEnabled() &&
               GPR_UNLIKELY(t->memory_owner.IsMemoryPressureHigh())) {
      // We have more streams allocated than we'd like, so apply some pushback
      // by refusing this stream.
      grpc_core::global_stats().IncrementRqCallsRejected();
      ++t->num_pending_induced_frames;
      grpc_slice_buffer_add(
          &t->qbuf,
          grpc_chttp2_rst_stream_create(t->incoming_stream_id,
                                        GRPC_HTTP2_ENHANCE_YOUR_CALM, nullptr));
      grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_RST_STREAM);
      return init_header_skip_frame_parser(t, priority_type, is_eoh);
    } else if (GPR_UNLIKELY(
                   t->max_concurrent_streams_overload_protection &&
                   t->streams_allocated.load(std::memory_order_relaxed) >
                       t->settings.local().max_concurrent_streams())) {
      // We have more streams allocated than we'd like, so apply some pushback
      // by refusing this stream.
      ++t->num_pending_induced_frames;
      grpc_slice_buffer_add(&t->qbuf, grpc_chttp2_rst_stream_create(
                                          t->incoming_stream_id,
                                          GRPC_HTTP2_REFUSED_STREAM, nullptr));
      grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_RST_STREAM);
      return init_header_skip_frame_parser(t, priority_type, is_eoh);
    } else if (GPR_UNLIKELY(t->stream_map.size() >=
                                t->settings.local().max_concurrent_streams() &&
                            grpc_core::RandomEarlyDetection(
                                t->settings.local().max_concurrent_streams(),
                                t->settings.acked().max_concurrent_streams())
                                .Reject(t->stream_map.size(), t->bitgen))) {
      // We are under the limit of max concurrent streams for the current
      // setting, but are over the next value that will be advertised.
      // Apply some backpressure by randomly not accepting new streams.
      ++t->num_pending_induced_frames;
      grpc_slice_buffer_add(&t->qbuf, grpc_chttp2_rst_stream_create(
                                          t->incoming_stream_id,
                                          GRPC_HTTP2_REFUSED_STREAM, nullptr));
      grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_RST_STREAM);
      return init_header_skip_frame_parser(t, priority_type, is_eoh);
    } else if (t->sent_goaway_state == GRPC_CHTTP2_FINAL_GOAWAY_SENT ||
               t->sent_goaway_state ==
                   GRPC_CHTTP2_FINAL_GOAWAY_SEND_SCHEDULED) {
      GRPC_CHTTP2_IF_TRACING(INFO)
          << "transport:" << t
          << " SERVER peer:" << t->peer_string.as_string_view()
          << " Final GOAWAY sent. Ignoring new grpc_chttp2_stream request "
             "id="
          << t->incoming_stream_id
          << ", last grpc_chttp2_stream id=" << t->last_new_stream_id;
      ;
      return init_header_skip_frame_parser(t, priority_type, is_eoh);
    } else if (t->num_incoming_streams_before_settings_ack == 0) {
      GRPC_CHTTP2_IF_TRACING(ERROR)
          << "transport:" << t
          << " SERVER peer:" << t->peer_string.as_string_view()
          << " rejecting grpc_chttp2_stream id=" << t->incoming_stream_id
          << ", last grpc_chttp2_stream id=" << t->last_new_stream_id
          << " before settings have been acknowledged";
      ++t->num_pending_induced_frames;
      grpc_slice_buffer_add(
          &t->qbuf,
          grpc_chttp2_rst_stream_create(t->incoming_stream_id,
                                        GRPC_HTTP2_ENHANCE_YOUR_CALM, nullptr));
      grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_RST_STREAM);
      t->last_new_stream_id = t->incoming_stream_id;
      return init_header_skip_frame_parser(t, priority_type, is_eoh);
    }
    --t->num_incoming_streams_before_settings_ack;
    t->last_new_stream_id = t->incoming_stream_id;
    s = t->incoming_stream =
        grpc_chttp2_parsing_accept_stream(t, t->incoming_stream_id);
    ++requests_started;
    if (GPR_UNLIKELY(s == nullptr)) {
      GRPC_CHTTP2_IF_TRACING(ERROR) << "grpc_chttp2_stream not accepted";
      return init_header_skip_frame_parser(t, priority_type, is_eoh);
    }
    if (GRPC_TRACE_FLAG_ENABLED(http) ||
        GRPC_TRACE_FLAG_ENABLED(chttp2_new_stream)) {
      LOG(INFO) << "[t:" << t << " fd:" << grpc_endpoint_get_fd(t->ep.get())
                << " peer:" << t->peer_string.as_string_view()
                << "] Accepting new stream; "
                   "num_incoming_streams_before_settings_ack="
                << t->num_incoming_streams_before_settings_ack;
    }
    if (t->channelz_socket != nullptr) {
      t->channelz_socket->RecordStreamStartedFromRemote();
    }
  } else {
    t->incoming_stream = s;
  }
  DCHECK_NE(s, nullptr);
  s->call_tracer_wrapper.RecordIncomingBytes({9, 0, 0});
  if (GPR_UNLIKELY(s->read_closed)) {
    GRPC_CHTTP2_IF_TRACING(ERROR)
        << "skipping already closed grpc_chttp2_stream header";
    t->incoming_stream = nullptr;
    return init_header_skip_frame_parser(t, priority_type, is_eoh);
  }
  t->parser = grpc_chttp2_transport::Parser{
      "header", grpc_chttp2_header_parser_parse, &t->hpack_parser};
  if (t->header_eof) {
    s->eos_received = true;
  }
  grpc_metadata_batch* incoming_metadata_buffer = nullptr;
  HPackParser::LogInfo::Type frame_type = HPackParser::LogInfo::kDontKnow;
  switch (s->header_frames_received) {
    case 0:
      if (t->is_client && t->header_eof) {
        GRPC_CHTTP2_IF_TRACING(INFO) << "parsing Trailers-Only";
        if (s->trailing_metadata_available != nullptr) {
          *s->trailing_metadata_available = true;
        }
        s->parsed_trailers_only = true;
        s->trailing_metadata_buffer.Set(grpc_core::GrpcTrailersOnly(), true);
        s->initial_metadata_buffer.Set(grpc_core::GrpcTrailersOnly(), true);
        incoming_metadata_buffer = &s->trailing_metadata_buffer;
        frame_type = HPackParser::LogInfo::kTrailers;
      } else {
        GRPC_CHTTP2_IF_TRACING(INFO) << "parsing initial_metadata";
        incoming_metadata_buffer = &s->initial_metadata_buffer;
        frame_type = HPackParser::LogInfo::kHeaders;
      }
      break;
    case 1:
      GRPC_CHTTP2_IF_TRACING(INFO) << "parsing trailing_metadata";
      incoming_metadata_buffer = &s->trailing_metadata_buffer;
      frame_type = HPackParser::LogInfo::kTrailers;
      break;
    case 2:
      LOG(ERROR) << "too many header frames received";
      return init_header_skip_frame_parser(t, priority_type, is_eoh);
  }
  if (frame_type == HPackParser::LogInfo::kTrailers && !t->header_eof) {
    return GRPC_ERROR_CREATE(
        "Trailing metadata frame received without an end-o-stream");
  }
  t->hpack_parser.BeginFrame(incoming_metadata_buffer,
                             /*metadata_size_soft_limit=*/
                             t->max_header_list_size_soft_limit,
                             /*metadata_size_hard_limit=*/
                             t->settings.acked().max_header_list_size(),
                             hpack_boundary_type(t, is_eoh), priority_type,
                             hpack_parser_log_info(t, frame_type));
  return absl::OkStatus();
}

static grpc_error_handle init_window_update_frame_parser(
    grpc_chttp2_transport* t) {
  grpc_error_handle err = grpc_chttp2_window_update_parser_begin_frame(
      &t->simple.window_update, t->incoming_frame_size,
      t->incoming_frame_flags);
  if (!err.ok()) return err;
  if (t->incoming_stream_id != 0) {
    grpc_chttp2_stream* s = t->incoming_stream =
        grpc_chttp2_parsing_lookup_stream(t, t->incoming_stream_id);
    if (s == nullptr) {
      GRPC_TRACE_LOG(http, ERROR) << "Stream " << t->incoming_stream_id
                                  << " not found, ignoring WINDOW_UPDATE";
      return init_non_header_skip_frame_parser(t);
    }
    s->call_tracer_wrapper.RecordIncomingBytes({9, 0, 0});
  }
  t->parser = grpc_chttp2_transport::Parser{
      "window_update", grpc_chttp2_window_update_parser_parse,
      &t->simple.window_update};
  return absl::OkStatus();
}

static grpc_error_handle init_ping_parser(grpc_chttp2_transport* t) {
  grpc_error_handle err = grpc_chttp2_ping_parser_begin_frame(
      &t->simple.ping, t->incoming_frame_size, t->incoming_frame_flags);
  if (!err.ok()) return err;
  t->parser = grpc_chttp2_transport::Parser{
      "ping", grpc_chttp2_ping_parser_parse, &t->simple.ping};
  return absl::OkStatus();
}

static grpc_error_handle init_rst_stream_parser(grpc_chttp2_transport* t) {
  grpc_error_handle err = grpc_chttp2_rst_stream_parser_begin_frame(
      &t->simple.rst_stream, t->incoming_frame_size, t->incoming_frame_flags);
  if (!err.ok()) return err;
  grpc_chttp2_stream* s = t->incoming_stream =
      grpc_chttp2_parsing_lookup_stream(t, t->incoming_stream_id);
  if (!t->incoming_stream) {
    return init_non_header_skip_frame_parser(t);
  }
  s->call_tracer_wrapper.RecordIncomingBytes({9, 0, 0});
  t->parser = grpc_chttp2_transport::Parser{
      "rst_stream", grpc_chttp2_rst_stream_parser_parse, &t->simple.rst_stream};
  return absl::OkStatus();
}

static grpc_error_handle init_goaway_parser(grpc_chttp2_transport* t) {
  grpc_error_handle err = grpc_chttp2_goaway_parser_begin_frame(
      &t->goaway_parser, t->incoming_frame_size, t->incoming_frame_flags);
  if (!err.ok()) return err;
  t->parser = grpc_chttp2_transport::Parser{
      "goaway", grpc_chttp2_goaway_parser_parse, &t->goaway_parser};
  return absl::OkStatus();
}

static grpc_error_handle init_settings_frame_parser(grpc_chttp2_transport* t) {
  if (t->incoming_stream_id != 0) {
    return GRPC_ERROR_CREATE("Settings frame received for grpc_chttp2_stream");
  }

  grpc_error_handle err = grpc_chttp2_settings_parser_begin_frame(
      &t->simple.settings, t->incoming_frame_size, t->incoming_frame_flags,
      t->settings.mutable_peer());
  if (!err.ok()) {
    return err;
  }
  if (t->incoming_frame_flags & GRPC_CHTTP2_FLAG_ACK) {
    if (!t->settings.AckLastSend()) {
      return GRPC_ERROR_CREATE("Received unexpected settings ack");
    }
    t->hpack_parser.hpack_table()->SetMaxBytes(
        t->settings.acked().header_table_size());
    grpc_chttp2_act_on_flowctl_action(
        t->flow_control.SetAckedInitialWindow(
            t->settings.acked().initial_window_size()),
        t, nullptr);
    if (t->settings_ack_watchdog !=
        grpc_event_engine::experimental::EventEngine::TaskHandle::kInvalid) {
      t->event_engine->Cancel(std::exchange(
          t->settings_ack_watchdog,
          grpc_event_engine::experimental::EventEngine::TaskHandle::kInvalid));
    }
    // This is more streams than can be started in http2, so setting this
    // effictively removes the limit for the rest of the connection.
    t->num_incoming_streams_before_settings_ack =
        std::numeric_limits<uint32_t>::max();
  }
  t->parser = grpc_chttp2_transport::Parser{
      "settings", grpc_chttp2_settings_parser_parse, &t->simple.settings};
  return absl::OkStatus();
}

static grpc_error_handle init_security_frame_parser(grpc_chttp2_transport* t) {
  grpc_error_handle err =
      grpc_chttp2_security_frame_parser_begin_frame(&t->security_frame_parser);
  if (!err.ok()) return err;
  t->parser = grpc_chttp2_transport::Parser{
      "security_frame", grpc_chttp2_security_frame_parser_parse,
      &t->security_frame_parser};
  return absl::OkStatus();
}

static grpc_error_handle parse_frame_slice(grpc_chttp2_transport* t,
                                           const grpc_slice& slice,
                                           int is_last) {
  grpc_chttp2_stream* s = t->incoming_stream;
  GRPC_TRACE_VLOG(http, 2) << "INCOMING[" << t << ";" << s << "]: Parse "
                           << GRPC_SLICE_LENGTH(slice) << "b "
                           << (is_last ? "last " : "") << "frame fragment with "
                           << t->parser.name;
  grpc_error_handle err =
      t->parser.parser(t->parser.user_data, t, s, slice, is_last);
  intptr_t unused;
  if (GPR_LIKELY(err.ok())) {
    return err;
  }
  GRPC_TRACE_LOG(http, ERROR)
      << "INCOMING[" << t << ";" << s << "]: Parse failed with " << err;
  if (grpc_error_get_int(err, grpc_core::StatusIntProperty::kStreamId,
                         &unused)) {
    grpc_chttp2_parsing_become_skip_parser(t);
    if (s) {
      grpc_chttp2_cancel_stream(t, s, err, true);
    }
    return absl::OkStatus();
  }
  return err;
}

typedef void (*maybe_complete_func_type)(grpc_chttp2_transport* t,
                                         grpc_chttp2_stream* s);
static const maybe_complete_func_type maybe_complete_funcs[] = {
    grpc_chttp2_maybe_complete_recv_initial_metadata,
    grpc_chttp2_maybe_complete_recv_trailing_metadata};

static void force_client_rst_stream(void* sp, grpc_error_handle /*error*/) {
  grpc_chttp2_stream* s = static_cast<grpc_chttp2_stream*>(sp);
  grpc_chttp2_transport* t = s->t.get();
  if (!s->write_closed) {
    grpc_chttp2_add_rst_stream_to_next_write(t, s->id, GRPC_HTTP2_NO_ERROR,
                                             &s->call_tracer_wrapper);
    grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_FORCE_RST_STREAM);
    grpc_chttp2_mark_stream_closed(t, s, true, true, absl::OkStatus());
  }
  GRPC_CHTTP2_STREAM_UNREF(s, "final_rst");
}

grpc_error_handle grpc_chttp2_header_parser_parse(void* hpack_parser,
                                                  grpc_chttp2_transport* t,
                                                  grpc_chttp2_stream* s,
                                                  const grpc_slice& slice,
                                                  int is_last) {
  auto* parser = static_cast<grpc_core::HPackParser*>(hpack_parser);
  grpc_core::CallTracerAnnotationInterface* call_tracer = nullptr;
  if (s != nullptr) {
    s->call_tracer_wrapper.RecordIncomingBytes(
        {0, 0, GRPC_SLICE_LENGTH(slice)});
    call_tracer =
        grpc_core::IsCallTracerInTransportEnabled()
            ? s->arena->GetContext<grpc_core::CallTracerInterface>()
            : s->arena->GetContext<grpc_core::CallTracerAnnotationInterface>();
  }
  grpc_error_handle error = parser->Parse(
      slice, is_last != 0, absl::BitGenRef(t->bitgen), call_tracer);
  if (!error.ok()) {
    return error;
  }
  if (is_last) {
    // need to check for null stream: this can occur if we receive an invalid
    // stream id on a header
    if (s != nullptr) {
      if (parser->is_boundary()) {
        if (s->header_frames_received == 2) {
          return GRPC_ERROR_CREATE("Too many trailer frames");
        }
        s->published_metadata[s->header_frames_received] =
            GRPC_METADATA_PUBLISHED_FROM_WIRE;
        maybe_complete_funcs[s->header_frames_received](t, s);
        s->header_frames_received++;
      }
      if (parser->is_eof()) {
        if (t->is_client && !s->write_closed) {
          // server eof ==> complete closure; we may need to forcefully close
          // the stream. Wait until the combiner lock is ready to be released
          // however -- it might be that we receive a RST_STREAM following this
          // and can avoid the extra write
          GRPC_CHTTP2_STREAM_REF(s, "final_rst");
          t->combiner->FinallyRun(
              GRPC_CLOSURE_CREATE(force_client_rst_stream, s, nullptr),
              absl::OkStatus());
        }
        grpc_chttp2_mark_stream_closed(t, s, true, false, absl::OkStatus());
      }
    }
    parser->FinishFrame();
  }
  return absl::OkStatus();
}
