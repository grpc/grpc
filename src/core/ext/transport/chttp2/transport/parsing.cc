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

#include <string.h>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/slice/slice_utils.h"
#include "src/core/lib/transport/http2_errors.h"
#include "src/core/lib/transport/status_conversion.h"
#include "src/core/lib/transport/timeout_encoding.h"

using grpc_core::HPackParser;

static grpc_error_handle init_frame_parser(grpc_chttp2_transport* t);
static grpc_error_handle init_header_frame_parser(grpc_chttp2_transport* t,
                                                  int is_continuation);
static grpc_error_handle init_data_frame_parser(grpc_chttp2_transport* t);
static grpc_error_handle init_rst_stream_parser(grpc_chttp2_transport* t);
static grpc_error_handle init_settings_frame_parser(grpc_chttp2_transport* t);
static grpc_error_handle init_window_update_frame_parser(
    grpc_chttp2_transport* t);
static grpc_error_handle init_ping_parser(grpc_chttp2_transport* t);
static grpc_error_handle init_goaway_parser(grpc_chttp2_transport* t);
static grpc_error_handle init_non_header_skip_frame_parser(
    grpc_chttp2_transport* t);

static grpc_error_handle parse_frame_slice(grpc_chttp2_transport* t,
                                           const grpc_slice& slice,
                                           int is_last);

grpc_error_handle grpc_chttp2_perform_read(grpc_chttp2_transport* t,
                                           const grpc_slice& slice) {
  const uint8_t* beg = GRPC_SLICE_START_PTR(slice);
  const uint8_t* end = GRPC_SLICE_END_PTR(slice);
  const uint8_t* cur = beg;
  grpc_error_handle err;

  if (cur == end) return GRPC_ERROR_NONE;

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
          return GRPC_ERROR_CREATE_FROM_CPP_STRING(absl::StrFormat(
              "Connect string mismatch: expected '%c' (%d) got '%c' (%d) "
              "at byte %d",
              GRPC_CHTTP2_CLIENT_CONNECT_STRING[t->deframe_state],
              static_cast<int>(static_cast<uint8_t>(
                  GRPC_CHTTP2_CLIENT_CONNECT_STRING[t->deframe_state])),
              *cur, static_cast<int>(*cur), t->deframe_state));
        }
        ++cur;
        // NOLINTNEXTLINE(bugprone-misplaced-widening-cast)
        t->deframe_state = static_cast<grpc_chttp2_deframe_transport_state>(
            1 + static_cast<int>(t->deframe_state));
      }
      if (cur == end) {
        return GRPC_ERROR_NONE;
      }
    dts_fh_0:
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_DTS_FH_0:
      GPR_DEBUG_ASSERT(cur < end);
      t->incoming_frame_size = (static_cast<uint32_t>(*cur)) << 16;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_1;
        return GRPC_ERROR_NONE;
      }
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_DTS_FH_1:
      GPR_DEBUG_ASSERT(cur < end);
      t->incoming_frame_size |= (static_cast<uint32_t>(*cur)) << 8;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_2;
        return GRPC_ERROR_NONE;
      }
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_DTS_FH_2:
      GPR_DEBUG_ASSERT(cur < end);
      t->incoming_frame_size |= *cur;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_3;
        return GRPC_ERROR_NONE;
      }
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_DTS_FH_3:
      GPR_DEBUG_ASSERT(cur < end);
      t->incoming_frame_type = *cur;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_4;
        return GRPC_ERROR_NONE;
      }
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_DTS_FH_4:
      GPR_DEBUG_ASSERT(cur < end);
      t->incoming_frame_flags = *cur;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_5;
        return GRPC_ERROR_NONE;
      }
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_DTS_FH_5:
      GPR_DEBUG_ASSERT(cur < end);
      t->incoming_stream_id = ((static_cast<uint32_t>(*cur)) & 0x7f) << 24;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_6;
        return GRPC_ERROR_NONE;
      }
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_DTS_FH_6:
      GPR_DEBUG_ASSERT(cur < end);
      t->incoming_stream_id |= (static_cast<uint32_t>(*cur)) << 16;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_7;
        return GRPC_ERROR_NONE;
      }
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_DTS_FH_7:
      GPR_DEBUG_ASSERT(cur < end);
      t->incoming_stream_id |= (static_cast<uint32_t>(*cur)) << 8;
      if (++cur == end) {
        t->deframe_state = GRPC_DTS_FH_8;
        return GRPC_ERROR_NONE;
      }
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_DTS_FH_8:
      GPR_DEBUG_ASSERT(cur < end);
      t->incoming_stream_id |= (static_cast<uint32_t>(*cur));
      t->deframe_state = GRPC_DTS_FRAME;
      err = init_frame_parser(t);
      if (err != GRPC_ERROR_NONE) {
        return err;
      }
      if (t->incoming_frame_size == 0) {
        err = parse_frame_slice(t, grpc_empty_slice(), 1);
        if (err != GRPC_ERROR_NONE) {
          return err;
        }
        t->incoming_stream = nullptr;
        if (++cur == end) {
          t->deframe_state = GRPC_DTS_FH_0;
          return GRPC_ERROR_NONE;
        }
        goto dts_fh_0; /* loop */
      } else if (t->flow_control->flow_control_enabled() &&
                 t->incoming_frame_size >
                     t->settings[GRPC_ACKED_SETTINGS]
                                [GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE]) {
        return GRPC_ERROR_CREATE_FROM_CPP_STRING(
            absl::StrFormat("Frame size %d is larger than max frame size %d",
                            t->incoming_frame_size,
                            t->settings[GRPC_ACKED_SETTINGS]
                                       [GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE]));
      }
      if (++cur == end) {
        return GRPC_ERROR_NONE;
      }
      ABSL_FALLTHROUGH_INTENDED;
    case GRPC_DTS_FRAME:
      GPR_DEBUG_ASSERT(cur < end);
      if (static_cast<uint32_t>(end - cur) == t->incoming_frame_size) {
        err = parse_frame_slice(
            t,
            grpc_slice_sub_no_ref(slice, static_cast<size_t>(cur - beg),
                                  static_cast<size_t>(end - beg)),
            1);
        if (err != GRPC_ERROR_NONE) {
          return err;
        }
        t->deframe_state = GRPC_DTS_FH_0;
        t->incoming_stream = nullptr;
        return GRPC_ERROR_NONE;
      } else if (static_cast<uint32_t>(end - cur) > t->incoming_frame_size) {
        size_t cur_offset = static_cast<size_t>(cur - beg);
        err = parse_frame_slice(
            t,
            grpc_slice_sub_no_ref(slice, cur_offset,
                                  cur_offset + t->incoming_frame_size),
            1);
        if (err != GRPC_ERROR_NONE) {
          return err;
        }
        cur += t->incoming_frame_size;
        t->incoming_stream = nullptr;
        goto dts_fh_0; /* loop */
      } else {
        err = parse_frame_slice(
            t,
            grpc_slice_sub_no_ref(slice, static_cast<size_t>(cur - beg),
                                  static_cast<size_t>(end - beg)),
            0);
        if (err != GRPC_ERROR_NONE) {
          return err;
        }
        t->incoming_frame_size -= static_cast<uint32_t>(end - cur);
        return GRPC_ERROR_NONE;
      }
      GPR_UNREACHABLE_CODE(return GRPC_ERROR_NONE);
  }

  GPR_UNREACHABLE_CODE(return GRPC_ERROR_NONE);
}

static grpc_error_handle init_frame_parser(grpc_chttp2_transport* t) {
  if (t->is_first_frame &&
      t->incoming_frame_type != GRPC_CHTTP2_FRAME_SETTINGS) {
    return GRPC_ERROR_CREATE_FROM_CPP_STRING(absl::StrCat(
        "Expected SETTINGS frame as the first frame, got frame type ",
        t->incoming_frame_type));
  }
  t->is_first_frame = false;
  if (t->expect_continuation_stream_id != 0) {
    if (t->incoming_frame_type != GRPC_CHTTP2_FRAME_CONTINUATION) {
      return GRPC_ERROR_CREATE_FROM_CPP_STRING(
          absl::StrFormat("Expected CONTINUATION frame, got frame type %02x",
                          t->incoming_frame_type));
    }
    if (t->expect_continuation_stream_id != t->incoming_stream_id) {
      return GRPC_ERROR_CREATE_FROM_CPP_STRING(absl::StrFormat(
          "Expected CONTINUATION frame for grpc_chttp2_stream %08x, got "
          "grpc_chttp2_stream %08x",
          t->expect_continuation_stream_id, t->incoming_stream_id));
    }
    return init_header_frame_parser(t, 1);
  }
  switch (t->incoming_frame_type) {
    case GRPC_CHTTP2_FRAME_DATA:
      return init_data_frame_parser(t);
    case GRPC_CHTTP2_FRAME_HEADER:
      return init_header_frame_parser(t, 0);
    case GRPC_CHTTP2_FRAME_CONTINUATION:
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Unexpected CONTINUATION frame");
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
    default:
      if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
        gpr_log(GPR_ERROR, "Unknown frame type %02x", t->incoming_frame_type);
      }
      return init_non_header_skip_frame_parser(t);
  }
}

static grpc_error_handle skip_parser(void* /*parser*/,
                                     grpc_chttp2_transport* /*t*/,
                                     grpc_chttp2_stream* /*s*/,
                                     const grpc_slice& /*slice*/,
                                     int /*is_last*/) {
  return GRPC_ERROR_NONE;
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
    grpc_chttp2_transport* t, HPackParser::Priority priority_type) {
  bool is_eoh = t->expect_continuation_stream_id != 0;
  t->parser = grpc_chttp2_header_parser_parse;
  t->parser_data = &t->hpack_parser;
  t->hpack_parser.BeginFrame(
      nullptr,
      t->settings[GRPC_ACKED_SETTINGS]
                 [GRPC_CHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE],
      hpack_boundary_type(t, is_eoh), priority_type,
      hpack_parser_log_info(t, HPackParser::LogInfo::kDontKnow));
  return GRPC_ERROR_NONE;
}

static grpc_error_handle init_non_header_skip_frame_parser(
    grpc_chttp2_transport* t) {
  t->parser = skip_parser;
  return GRPC_ERROR_NONE;
}

void grpc_chttp2_parsing_become_skip_parser(grpc_chttp2_transport* t) {
  if (t->parser == grpc_chttp2_header_parser_parse) {
    t->hpack_parser.StopBufferingFrame();
  } else {
    t->parser = skip_parser;
  }
}

static grpc_error_handle init_data_frame_parser(grpc_chttp2_transport* t) {
  // Update BDP accounting since we have received a data frame.
  grpc_core::BdpEstimator* bdp_est = t->flow_control->bdp_estimator();
  if (bdp_est) {
    if (t->bdp_ping_blocked) {
      t->bdp_ping_blocked = false;
      GRPC_CHTTP2_REF_TRANSPORT(t, "bdp_ping");
      schedule_bdp_ping_locked(t);
    }
    bdp_est->AddIncomingBytes(t->incoming_frame_size);
  }
  grpc_chttp2_stream* s =
      grpc_chttp2_parsing_lookup_stream(t, t->incoming_stream_id);
  grpc_error_handle err = GRPC_ERROR_NONE;
  grpc_core::chttp2::FlowControlAction action;
  if (s == nullptr) {
    err = t->flow_control->RecvData(t->incoming_frame_size);
    action = t->flow_control->MakeAction();
  } else {
    err = s->flow_control->RecvData(t->incoming_frame_size);
    action = s->flow_control->MakeAction();
  }
  grpc_chttp2_act_on_flowctl_action(action, t, s);
  if (err != GRPC_ERROR_NONE) {
    goto error_handler;
  }
  if (s == nullptr) {
    return init_non_header_skip_frame_parser(t);
  }
  s->received_bytes += t->incoming_frame_size;
  s->stats.incoming.framing_bytes += 9;
  if (err == GRPC_ERROR_NONE && s->read_closed) {
    return init_non_header_skip_frame_parser(t);
  }
  if (err == GRPC_ERROR_NONE) {
    err = grpc_chttp2_data_parser_begin_frame(
        &s->data_parser, t->incoming_frame_flags, s->id, s);
  }
error_handler:
  intptr_t unused;
  if (err == GRPC_ERROR_NONE) {
    t->incoming_stream = s;
    /* t->parser = grpc_chttp2_data_parser_parse;*/
    t->parser = grpc_chttp2_data_parser_parse;
    t->parser_data = &s->data_parser;
    t->ping_state.last_ping_sent_time = GRPC_MILLIS_INF_PAST;
    return GRPC_ERROR_NONE;
  } else if (grpc_error_get_int(err, GRPC_ERROR_INT_STREAM_ID, &unused)) {
    /* handle stream errors by closing the stream */
    if (s != nullptr) {
      grpc_chttp2_mark_stream_closed(t, s, true, false, err);
    }
    grpc_chttp2_add_rst_stream_to_next_write(t, t->incoming_stream_id,
                                             GRPC_HTTP2_PROTOCOL_ERROR,
                                             &s->stats.outgoing);
    return init_non_header_skip_frame_parser(t);
  } else {
    return err;
  }
}

static grpc_error_handle init_header_frame_parser(grpc_chttp2_transport* t,
                                                  int is_continuation) {
  const bool is_eoh =
      (t->incoming_frame_flags & GRPC_CHTTP2_DATA_FLAG_END_HEADERS) != 0;
  grpc_chttp2_stream* s;

  /* TODO(ctiller): when to increment header_frames_received? */

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

  t->ping_state.last_ping_sent_time = GRPC_MILLIS_INF_PAST;

  /* could be a new grpc_chttp2_stream or an existing grpc_chttp2_stream */
  s = grpc_chttp2_parsing_lookup_stream(t, t->incoming_stream_id);
  if (s == nullptr) {
    if (GPR_UNLIKELY(is_continuation)) {
      GRPC_CHTTP2_IF_TRACING(
          gpr_log(GPR_ERROR,
                  "grpc_chttp2_stream disbanded before CONTINUATION received"));
      return init_header_skip_frame_parser(t, priority_type);
    }
    if (t->is_client) {
      if (GPR_LIKELY((t->incoming_stream_id & 1) &&
                     t->incoming_stream_id < t->next_stream_id)) {
        /* this is an old (probably cancelled) grpc_chttp2_stream */
      } else {
        GRPC_CHTTP2_IF_TRACING(gpr_log(
            GPR_ERROR, "ignoring new grpc_chttp2_stream creation on client"));
      }
      return init_header_skip_frame_parser(t, priority_type);
    } else if (GPR_UNLIKELY(t->last_new_stream_id >= t->incoming_stream_id)) {
      GRPC_CHTTP2_IF_TRACING(gpr_log(
          GPR_ERROR,
          "ignoring out of order new grpc_chttp2_stream request on server; "
          "last grpc_chttp2_stream "
          "id=%d, new grpc_chttp2_stream id=%d",
          t->last_new_stream_id, t->incoming_stream_id));
      return init_header_skip_frame_parser(t, priority_type);
    } else if (GPR_UNLIKELY((t->incoming_stream_id & 1) == 0)) {
      GRPC_CHTTP2_IF_TRACING(gpr_log(
          GPR_ERROR,
          "ignoring grpc_chttp2_stream with non-client generated index %d",
          t->incoming_stream_id));
      return init_header_skip_frame_parser(t, priority_type);
    } else if (GPR_UNLIKELY(
                   grpc_chttp2_stream_map_size(&t->stream_map) >=
                   t->settings[GRPC_ACKED_SETTINGS]
                              [GRPC_CHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS])) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Max stream count exceeded");
    }
    t->last_new_stream_id = t->incoming_stream_id;
    s = t->incoming_stream =
        grpc_chttp2_parsing_accept_stream(t, t->incoming_stream_id);
    if (GPR_UNLIKELY(s == nullptr)) {
      GRPC_CHTTP2_IF_TRACING(
          gpr_log(GPR_ERROR, "grpc_chttp2_stream not accepted"));
      return init_header_skip_frame_parser(t, priority_type);
    }
    if (t->channelz_socket != nullptr) {
      t->channelz_socket->RecordStreamStartedFromRemote();
    }
  } else {
    t->incoming_stream = s;
  }
  GPR_DEBUG_ASSERT(s != nullptr);
  s->stats.incoming.framing_bytes += 9;
  if (GPR_UNLIKELY(s->read_closed)) {
    GRPC_CHTTP2_IF_TRACING(gpr_log(
        GPR_ERROR, "skipping already closed grpc_chttp2_stream header"));
    t->incoming_stream = nullptr;
    return init_header_skip_frame_parser(t, priority_type);
  }
  t->parser = grpc_chttp2_header_parser_parse;
  t->parser_data = &t->hpack_parser;
  if (t->header_eof) {
    s->eos_received = true;
  }
  grpc_metadata_batch* incoming_metadata_buffer = nullptr;
  HPackParser::LogInfo::Type frame_type = HPackParser::LogInfo::kDontKnow;
  switch (s->header_frames_received) {
    case 0:
      if (t->is_client && t->header_eof) {
        GRPC_CHTTP2_IF_TRACING(gpr_log(GPR_INFO, "parsing Trailers-Only"));
        if (s->trailing_metadata_available != nullptr) {
          *s->trailing_metadata_available = true;
        }
        incoming_metadata_buffer = &s->trailing_metadata_buffer;
        frame_type = HPackParser::LogInfo::kTrailers;
      } else {
        GRPC_CHTTP2_IF_TRACING(gpr_log(GPR_INFO, "parsing initial_metadata"));
        incoming_metadata_buffer = &s->initial_metadata_buffer;
        frame_type = HPackParser::LogInfo::kHeaders;
      }
      break;
    case 1:
      GRPC_CHTTP2_IF_TRACING(gpr_log(GPR_INFO, "parsing trailing_metadata"));
      incoming_metadata_buffer = &s->trailing_metadata_buffer;
      frame_type = HPackParser::LogInfo::kTrailers;
      break;
    case 2:
      gpr_log(GPR_ERROR, "too many header frames received");
      return init_header_skip_frame_parser(t, priority_type);
  }
  t->hpack_parser.BeginFrame(
      incoming_metadata_buffer,
      t->settings[GRPC_ACKED_SETTINGS]
                 [GRPC_CHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE],
      hpack_boundary_type(t, is_eoh), priority_type,
      hpack_parser_log_info(t, frame_type));
  return GRPC_ERROR_NONE;
}

static grpc_error_handle init_window_update_frame_parser(
    grpc_chttp2_transport* t) {
  grpc_error_handle err = grpc_chttp2_window_update_parser_begin_frame(
      &t->simple.window_update, t->incoming_frame_size,
      t->incoming_frame_flags);
  if (err != GRPC_ERROR_NONE) return err;
  if (t->incoming_stream_id != 0) {
    grpc_chttp2_stream* s = t->incoming_stream =
        grpc_chttp2_parsing_lookup_stream(t, t->incoming_stream_id);
    if (s == nullptr) {
      return init_non_header_skip_frame_parser(t);
    }
    s->stats.incoming.framing_bytes += 9;
  }
  t->parser = grpc_chttp2_window_update_parser_parse;
  t->parser_data = &t->simple.window_update;
  return GRPC_ERROR_NONE;
}

static grpc_error_handle init_ping_parser(grpc_chttp2_transport* t) {
  grpc_error_handle err = grpc_chttp2_ping_parser_begin_frame(
      &t->simple.ping, t->incoming_frame_size, t->incoming_frame_flags);
  if (err != GRPC_ERROR_NONE) return err;
  t->parser = grpc_chttp2_ping_parser_parse;
  t->parser_data = &t->simple.ping;
  return GRPC_ERROR_NONE;
}

static grpc_error_handle init_rst_stream_parser(grpc_chttp2_transport* t) {
  grpc_error_handle err = grpc_chttp2_rst_stream_parser_begin_frame(
      &t->simple.rst_stream, t->incoming_frame_size, t->incoming_frame_flags);
  if (err != GRPC_ERROR_NONE) return err;
  grpc_chttp2_stream* s = t->incoming_stream =
      grpc_chttp2_parsing_lookup_stream(t, t->incoming_stream_id);
  if (!t->incoming_stream) {
    return init_non_header_skip_frame_parser(t);
  }
  s->stats.incoming.framing_bytes += 9;
  t->parser = grpc_chttp2_rst_stream_parser_parse;
  t->parser_data = &t->simple.rst_stream;
  return GRPC_ERROR_NONE;
}

static grpc_error_handle init_goaway_parser(grpc_chttp2_transport* t) {
  grpc_error_handle err = grpc_chttp2_goaway_parser_begin_frame(
      &t->goaway_parser, t->incoming_frame_size, t->incoming_frame_flags);
  if (err != GRPC_ERROR_NONE) return err;
  t->parser = grpc_chttp2_goaway_parser_parse;
  t->parser_data = &t->goaway_parser;
  return GRPC_ERROR_NONE;
}

static grpc_error_handle init_settings_frame_parser(grpc_chttp2_transport* t) {
  if (t->incoming_stream_id != 0) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Settings frame received for grpc_chttp2_stream");
  }

  grpc_error_handle err = grpc_chttp2_settings_parser_begin_frame(
      &t->simple.settings, t->incoming_frame_size, t->incoming_frame_flags,
      t->settings[GRPC_PEER_SETTINGS]);
  if (err != GRPC_ERROR_NONE) {
    return err;
  }
  if (t->incoming_frame_flags & GRPC_CHTTP2_FLAG_ACK) {
    memcpy(t->settings[GRPC_ACKED_SETTINGS], t->settings[GRPC_SENT_SETTINGS],
           GRPC_CHTTP2_NUM_SETTINGS * sizeof(uint32_t));
    t->hpack_parser.hpack_table()->SetMaxBytes(
        t->settings[GRPC_ACKED_SETTINGS]
                   [GRPC_CHTTP2_SETTINGS_HEADER_TABLE_SIZE]);
    t->sent_local_settings = false;
  }
  t->parser = grpc_chttp2_settings_parser_parse;
  t->parser_data = &t->simple.settings;
  return GRPC_ERROR_NONE;
}

static grpc_error_handle parse_frame_slice(grpc_chttp2_transport* t,
                                           const grpc_slice& slice,
                                           int is_last) {
  grpc_chttp2_stream* s = t->incoming_stream;
  grpc_error_handle err = t->parser(t->parser_data, t, s, slice, is_last);
  intptr_t unused;
  if (GPR_LIKELY(err == GRPC_ERROR_NONE)) {
    return err;
  } else if (grpc_error_get_int(err, GRPC_ERROR_INT_STREAM_ID, &unused)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
      gpr_log(GPR_ERROR, "%s", grpc_error_std_string(err).c_str());
    }
    grpc_chttp2_parsing_become_skip_parser(t);
    if (s) {
      s->forced_close_error = err;
      grpc_chttp2_add_rst_stream_to_next_write(t, t->incoming_stream_id,
                                               GRPC_HTTP2_PROTOCOL_ERROR,
                                               &s->stats.outgoing);
    } else {
      GRPC_ERROR_UNREF(err);
    }
  }
  return err;
}
