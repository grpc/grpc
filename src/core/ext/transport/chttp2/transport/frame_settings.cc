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

#include "src/core/ext/transport/chttp2/transport/frame_settings.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"

#include <string.h>

#include "absl/strings/str_format.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/transport/http2_errors.h"

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

grpc_slice grpc_chttp2_settings_create(uint32_t* old_settings,
                                       const uint32_t* new_settings,
                                       uint32_t force_mask, size_t count) {
  size_t i;
  uint32_t n = 0;
  grpc_slice output;
  uint8_t* p;

  for (i = 0; i < count; i++) {
    n += (new_settings[i] != old_settings[i] || (force_mask & (1u << i)) != 0);
  }

  output = GRPC_SLICE_MALLOC(9 + 6 * n);
  p = fill_header(GRPC_SLICE_START_PTR(output), 6 * n, 0);

  for (i = 0; i < count; i++) {
    if (new_settings[i] != old_settings[i] || (force_mask & (1u << i)) != 0) {
      *p++ = static_cast<uint8_t>(grpc_setting_id_to_wire_id[i] >> 8);
      *p++ = static_cast<uint8_t>(grpc_setting_id_to_wire_id[i]);
      *p++ = static_cast<uint8_t>(new_settings[i] >> 24);
      *p++ = static_cast<uint8_t>(new_settings[i] >> 16);
      *p++ = static_cast<uint8_t>(new_settings[i] >> 8);
      *p++ = static_cast<uint8_t>(new_settings[i]);
      old_settings[i] = new_settings[i];
    }
  }

  GPR_ASSERT(p == GRPC_SLICE_END_PTR(output));

  return output;
}

grpc_slice grpc_chttp2_settings_ack_create(void) {
  grpc_slice output = GRPC_SLICE_MALLOC(9);
  fill_header(GRPC_SLICE_START_PTR(output), 0, GRPC_CHTTP2_FLAG_ACK);
  return output;
}

grpc_error_handle grpc_chttp2_settings_parser_begin_frame(
    grpc_chttp2_settings_parser* parser, uint32_t length, uint8_t flags,
    uint32_t* settings) {
  parser->target_settings = settings;
  memcpy(parser->incoming_settings, settings,
         GRPC_CHTTP2_NUM_SETTINGS * sizeof(uint32_t));
  parser->is_ack = 0;
  parser->state = GRPC_CHTTP2_SPS_ID0;
  if (flags == GRPC_CHTTP2_FLAG_ACK) {
    parser->is_ack = 1;
    if (length != 0) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "non-empty settings ack frame received");
    }
    return GRPC_ERROR_NONE;
  } else if (flags != 0) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "invalid flags on settings frame");
  } else if (length % 6 != 0) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "settings frames must be a multiple of six bytes");
  } else {
    return GRPC_ERROR_NONE;
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
  grpc_chttp2_setting_id id;

  if (parser->is_ack) {
    return GRPC_ERROR_NONE;
  }

  for (;;) {
    switch (parser->state) {
      case GRPC_CHTTP2_SPS_ID0:
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_ID0;
          if (is_last) {
            memcpy(parser->target_settings, parser->incoming_settings,
                   GRPC_CHTTP2_NUM_SETTINGS * sizeof(uint32_t));
            t->num_pending_induced_frames++;
            grpc_slice_buffer_add(&t->qbuf, grpc_chttp2_settings_ack_create());
            if (t->notify_on_receive_settings != nullptr) {
              grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                                      t->notify_on_receive_settings,
                                      GRPC_ERROR_NONE);
              t->notify_on_receive_settings = nullptr;
            }
          }
          return GRPC_ERROR_NONE;
        }
        parser->id = static_cast<uint16_t>((static_cast<uint16_t>(*cur)) << 8);
        cur++;
      /* fallthrough */
      case GRPC_CHTTP2_SPS_ID1:
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_ID1;
          return GRPC_ERROR_NONE;
        }
        parser->id = static_cast<uint16_t>(parser->id | (*cur));
        cur++;
      /* fallthrough */
      case GRPC_CHTTP2_SPS_VAL0:
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_VAL0;
          return GRPC_ERROR_NONE;
        }
        parser->value = (static_cast<uint32_t>(*cur)) << 24;
        cur++;
      /* fallthrough */
      case GRPC_CHTTP2_SPS_VAL1:
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_VAL1;
          return GRPC_ERROR_NONE;
        }
        parser->value |= (static_cast<uint32_t>(*cur)) << 16;
        cur++;
      /* fallthrough */
      case GRPC_CHTTP2_SPS_VAL2:
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_VAL2;
          return GRPC_ERROR_NONE;
        }
        parser->value |= (static_cast<uint32_t>(*cur)) << 8;
        cur++;
      /* fallthrough */
      case GRPC_CHTTP2_SPS_VAL3:
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_VAL3;
          return GRPC_ERROR_NONE;
        } else {
          parser->state = GRPC_CHTTP2_SPS_ID0;
        }
        parser->value |= *cur;
        cur++;

        if (grpc_wire_id_to_setting_id(parser->id, &id)) {
          const grpc_chttp2_setting_parameters* sp =
              &grpc_chttp2_settings_parameters[id];
          // If flow control is disabled we skip these.
          if (!t->flow_control->flow_control_enabled() &&
              (id == GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE ||
               id == GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE)) {
            continue;
          }
          if (parser->value < sp->min_value || parser->value > sp->max_value) {
            switch (sp->invalid_value_behavior) {
              case GRPC_CHTTP2_CLAMP_INVALID_VALUE:
                parser->value =
                    GPR_CLAMP(parser->value, sp->min_value, sp->max_value);
                break;
              case GRPC_CHTTP2_DISCONNECT_ON_INVALID_VALUE:
                grpc_chttp2_goaway_append(
                    t->last_new_stream_id, sp->error_value,
                    grpc_slice_from_static_string("HTTP2 settings error"),
                    &t->qbuf);
                return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
                    absl::StrFormat("invalid value %u passed for %s",
                                    parser->value, sp->name)
                        .c_str());
            }
          }
          if (id == GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE &&
              parser->incoming_settings[id] != parser->value) {
            t->initial_window_update += static_cast<int64_t>(parser->value) -
                                        parser->incoming_settings[id];
            if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace) ||
                GRPC_TRACE_FLAG_ENABLED(grpc_flowctl_trace)) {
              gpr_log(GPR_INFO, "%p[%s] adding %d for initial_window change", t,
                      t->is_client ? "cli" : "svr",
                      static_cast<int>(t->initial_window_update));
            }
          }
          parser->incoming_settings[id] = parser->value;
          if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
            gpr_log(GPR_INFO, "CHTTP2:%s:%s: got setting %s = %d",
                    t->is_client ? "CLI" : "SVR", t->peer_string.c_str(),
                    sp->name, parser->value);
          }
        } else if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
          gpr_log(GPR_ERROR, "CHTTP2: Ignoring unknown setting %d (value %d)",
                  parser->id, parser->value);
        }
        break;
    }
  }
}
