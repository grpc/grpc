/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/core/ext/transport/chttp2/transport/frame_settings.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_errors.h"
#include "src/core/lib/debug/trace.h"

#define MAX_MAX_HEADER_LIST_SIZE (1024 * 1024 * 1024)

/* HTTP/2 mandated initial connection settings */
const grpc_chttp2_setting_parameters
    grpc_chttp2_settings_parameters[GRPC_CHTTP2_NUM_SETTINGS] = {
        {NULL, 0, 0, 0, GRPC_CHTTP2_DISCONNECT_ON_INVALID_VALUE,
         GRPC_CHTTP2_PROTOCOL_ERROR},
        {"HEADER_TABLE_SIZE", 4096, 0, 0xffffffff,
         GRPC_CHTTP2_CLAMP_INVALID_VALUE, GRPC_CHTTP2_PROTOCOL_ERROR},
        {"ENABLE_PUSH", 1, 0, 1, GRPC_CHTTP2_DISCONNECT_ON_INVALID_VALUE,
         GRPC_CHTTP2_PROTOCOL_ERROR},
        {"MAX_CONCURRENT_STREAMS", 0xffffffffu, 0, 0xffffffffu,
         GRPC_CHTTP2_DISCONNECT_ON_INVALID_VALUE, GRPC_CHTTP2_PROTOCOL_ERROR},
        {"INITIAL_WINDOW_SIZE", 65535, 0, 0x7fffffffu,
         GRPC_CHTTP2_DISCONNECT_ON_INVALID_VALUE,
         GRPC_CHTTP2_FLOW_CONTROL_ERROR},
        {"MAX_FRAME_SIZE", 16384, 16384, 16777215,
         GRPC_CHTTP2_DISCONNECT_ON_INVALID_VALUE, GRPC_CHTTP2_PROTOCOL_ERROR},
        {"MAX_HEADER_LIST_SIZE", MAX_MAX_HEADER_LIST_SIZE, 0,
         MAX_MAX_HEADER_LIST_SIZE, GRPC_CHTTP2_CLAMP_INVALID_VALUE,
         GRPC_CHTTP2_PROTOCOL_ERROR},
};

static uint8_t *fill_header(uint8_t *out, uint32_t length, uint8_t flags) {
  *out++ = (uint8_t)(length >> 16);
  *out++ = (uint8_t)(length >> 8);
  *out++ = (uint8_t)(length);
  *out++ = GRPC_CHTTP2_FRAME_SETTINGS;
  *out++ = flags;
  *out++ = 0;
  *out++ = 0;
  *out++ = 0;
  *out++ = 0;
  return out;
}

grpc_slice grpc_chttp2_settings_create(uint32_t *old, const uint32_t *new,
                                       uint32_t force_mask, size_t count) {
  size_t i;
  uint32_t n = 0;
  grpc_slice output;
  uint8_t *p;

  for (i = 0; i < count; i++) {
    n += (new[i] != old[i] || (force_mask & (1u << i)) != 0);
  }

  output = grpc_slice_malloc(9 + 6 * n);
  p = fill_header(GRPC_SLICE_START_PTR(output), 6 * n, 0);

  for (i = 0; i < count; i++) {
    if (new[i] != old[i] || (force_mask & (1u << i)) != 0) {
      GPR_ASSERT(i);
      *p++ = (uint8_t)(i >> 8);
      *p++ = (uint8_t)(i);
      *p++ = (uint8_t)(new[i] >> 24);
      *p++ = (uint8_t)(new[i] >> 16);
      *p++ = (uint8_t)(new[i] >> 8);
      *p++ = (uint8_t)(new[i]);
      old[i] = new[i];
    }
  }

  GPR_ASSERT(p == GRPC_SLICE_END_PTR(output));

  return output;
}

grpc_slice grpc_chttp2_settings_ack_create(void) {
  grpc_slice output = grpc_slice_malloc(9);
  fill_header(GRPC_SLICE_START_PTR(output), 0, GRPC_CHTTP2_FLAG_ACK);
  return output;
}

grpc_error *grpc_chttp2_settings_parser_begin_frame(
    grpc_chttp2_settings_parser *parser, uint32_t length, uint8_t flags,
    uint32_t *settings) {
  parser->target_settings = settings;
  memcpy(parser->incoming_settings, settings,
         GRPC_CHTTP2_NUM_SETTINGS * sizeof(uint32_t));
  parser->is_ack = 0;
  parser->state = GRPC_CHTTP2_SPS_ID0;
  if (flags == GRPC_CHTTP2_FLAG_ACK) {
    parser->is_ack = 1;
    if (length != 0) {
      return GRPC_ERROR_CREATE("non-empty settings ack frame received");
    }
    return GRPC_ERROR_NONE;
  } else if (flags != 0) {
    return GRPC_ERROR_CREATE("invalid flags on settings frame");
  } else if (length % 6 != 0) {
    return GRPC_ERROR_CREATE("settings frames must be a multiple of six bytes");
  } else {
    return GRPC_ERROR_NONE;
  }
}

grpc_error *grpc_chttp2_settings_parser_parse(grpc_exec_ctx *exec_ctx, void *p,
                                              grpc_chttp2_transport *t,
                                              grpc_chttp2_stream *s,
                                              grpc_slice slice, int is_last) {
  grpc_chttp2_settings_parser *parser = p;
  const uint8_t *cur = GRPC_SLICE_START_PTR(slice);
  const uint8_t *end = GRPC_SLICE_END_PTR(slice);
  char *msg;

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
            grpc_slice_buffer_add(&t->qbuf, grpc_chttp2_settings_ack_create());
          }
          return GRPC_ERROR_NONE;
        }
        parser->id = (uint16_t)(((uint16_t)*cur) << 8);
        cur++;
      /* fallthrough */
      case GRPC_CHTTP2_SPS_ID1:
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_ID1;
          return GRPC_ERROR_NONE;
        }
        parser->id = (uint16_t)(parser->id | (*cur));
        cur++;
      /* fallthrough */
      case GRPC_CHTTP2_SPS_VAL0:
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_VAL0;
          return GRPC_ERROR_NONE;
        }
        parser->value = ((uint32_t)*cur) << 24;
        cur++;
      /* fallthrough */
      case GRPC_CHTTP2_SPS_VAL1:
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_VAL1;
          return GRPC_ERROR_NONE;
        }
        parser->value |= ((uint32_t)*cur) << 16;
        cur++;
      /* fallthrough */
      case GRPC_CHTTP2_SPS_VAL2:
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_VAL2;
          return GRPC_ERROR_NONE;
        }
        parser->value |= ((uint32_t)*cur) << 8;
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

        if (parser->id > 0 && parser->id < GRPC_CHTTP2_NUM_SETTINGS) {
          const grpc_chttp2_setting_parameters *sp =
              &grpc_chttp2_settings_parameters[parser->id];
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
                gpr_asprintf(&msg, "invalid value %u passed for %s",
                             parser->value, sp->name);
                grpc_error *err = GRPC_ERROR_CREATE(msg);
                gpr_free(msg);
                return err;
            }
          }
          if (parser->id == GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE &&
              parser->incoming_settings[parser->id] != parser->value) {
            t->initial_window_update =
                (int64_t)parser->value - parser->incoming_settings[parser->id];
            if (grpc_http_trace) {
              gpr_log(GPR_DEBUG, "adding %d for initial_window change",
                      (int)t->initial_window_update);
            }
          }
          parser->incoming_settings[parser->id] = parser->value;
          if (grpc_http_trace) {
            gpr_log(GPR_DEBUG, "CHTTP2:%s: got setting %d = %d",
                    t->is_client ? "CLI" : "SVR", parser->id, parser->value);
          }
        } else if (grpc_http_trace) {
          gpr_log(GPR_ERROR, "CHTTP2: Ignoring unknown setting %d (value %d)",
                  parser->id, parser->value);
        }
        break;
    }
  }
}
