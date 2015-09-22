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

#include "src/core/transport/chttp2/frame_settings.h"
#include "src/core/transport/chttp2/internal.h"

#include <string.h>

#include "src/core/debug/trace.h"
#include "src/core/transport/chttp2/frame.h"
#include "src/core/transport/chttp2_transport.h"
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

/* HTTP/2 mandated initial connection settings */
const grpc_chttp2_setting_parameters
    grpc_chttp2_settings_parameters[GRPC_CHTTP2_NUM_SETTINGS] = {
        {NULL, 0, 0, 0, GRPC_CHTTP2_DISCONNECT_ON_INVALID_VALUE},
        {"HEADER_TABLE_SIZE", 4096, 0, 0xffffffff,
         GRPC_CHTTP2_CLAMP_INVALID_VALUE},
        {"ENABLE_PUSH", 1, 0, 1, GRPC_CHTTP2_DISCONNECT_ON_INVALID_VALUE},
        {"MAX_CONCURRENT_STREAMS", 0xffffffffu, 0, 0xffffffffu,
         GRPC_CHTTP2_DISCONNECT_ON_INVALID_VALUE},
        {"INITIAL_WINDOW_SIZE", 65535, 0, 0xffffffffu,
         GRPC_CHTTP2_DISCONNECT_ON_INVALID_VALUE},
        {"MAX_FRAME_SIZE", 16384, 16384, 16777215,
         GRPC_CHTTP2_DISCONNECT_ON_INVALID_VALUE},
        {"MAX_HEADER_LIST_SIZE", 0xffffffffu, 0, 0xffffffffu,
         GRPC_CHTTP2_CLAMP_INVALID_VALUE},
};

static gpr_uint8 *fill_header(gpr_uint8 *out, gpr_uint32 length,
                              gpr_uint8 flags) {
  *out++ = (gpr_uint8)(length >> 16);
  *out++ = (gpr_uint8)(length >> 8);
  *out++ = (gpr_uint8)(length);
  *out++ = GRPC_CHTTP2_FRAME_SETTINGS;
  *out++ = flags;
  *out++ = 0;
  *out++ = 0;
  *out++ = 0;
  *out++ = 0;
  return out;
}

gpr_slice grpc_chttp2_settings_create(gpr_uint32 *old, const gpr_uint32 *new,
                                      gpr_uint32 force_mask, size_t count) {
  size_t i;
  gpr_uint32 n = 0;
  gpr_slice output;
  gpr_uint8 *p;

  for (i = 0; i < count; i++) {
    n += (new[i] != old[i] || (force_mask & (1u << i)) != 0);
  }

  output = gpr_slice_malloc(9 + 6 * n);
  p = fill_header(GPR_SLICE_START_PTR(output), 6 * n, 0);

  for (i = 0; i < count; i++) {
    if (new[i] != old[i] || (force_mask & (1u << i)) != 0) {
      GPR_ASSERT(i);
      *p++ = (gpr_uint8)(i >> 8);
      *p++ = (gpr_uint8)(i);
      *p++ = (gpr_uint8)(new[i] >> 24);
      *p++ = (gpr_uint8)(new[i] >> 16);
      *p++ = (gpr_uint8)(new[i] >> 8);
      *p++ = (gpr_uint8)(new[i]);
      old[i] = new[i];
    }
  }

  GPR_ASSERT(p == GPR_SLICE_END_PTR(output));

  return output;
}

gpr_slice grpc_chttp2_settings_ack_create(void) {
  gpr_slice output = gpr_slice_malloc(9);
  fill_header(GPR_SLICE_START_PTR(output), 0, GRPC_CHTTP2_FLAG_ACK);
  return output;
}

grpc_chttp2_parse_error grpc_chttp2_settings_parser_begin_frame(
    grpc_chttp2_settings_parser *parser, gpr_uint32 length, gpr_uint8 flags,
    gpr_uint32 *settings) {
  parser->target_settings = settings;
  memcpy(parser->incoming_settings, settings,
         GRPC_CHTTP2_NUM_SETTINGS * sizeof(gpr_uint32));
  parser->is_ack = 0;
  parser->state = GRPC_CHTTP2_SPS_ID0;
  if (flags == GRPC_CHTTP2_FLAG_ACK) {
    parser->is_ack = 1;
    if (length != 0) {
      gpr_log(GPR_ERROR, "non-empty settings ack frame received");
      return GRPC_CHTTP2_CONNECTION_ERROR;
    }
    return GRPC_CHTTP2_PARSE_OK;
  } else if (flags != 0) {
    gpr_log(GPR_ERROR, "invalid flags on settings frame");
    return GRPC_CHTTP2_CONNECTION_ERROR;
  } else if (length % 6 != 0) {
    gpr_log(GPR_ERROR, "settings frames must be a multiple of six bytes");
    return GRPC_CHTTP2_CONNECTION_ERROR;
  } else {
    return GRPC_CHTTP2_PARSE_OK;
  }
}

grpc_chttp2_parse_error grpc_chttp2_settings_parser_parse(
    grpc_exec_ctx *exec_ctx, void *p,
    grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_parsing *stream_parsing, gpr_slice slice, int is_last) {
  grpc_chttp2_settings_parser *parser = p;
  const gpr_uint8 *cur = GPR_SLICE_START_PTR(slice);
  const gpr_uint8 *end = GPR_SLICE_END_PTR(slice);

  if (parser->is_ack) {
    return GRPC_CHTTP2_PARSE_OK;
  }

  for (;;) {
    switch (parser->state) {
      case GRPC_CHTTP2_SPS_ID0:
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_ID0;
          if (is_last) {
            transport_parsing->settings_updated = 1;
            memcpy(parser->target_settings, parser->incoming_settings,
                   GRPC_CHTTP2_NUM_SETTINGS * sizeof(gpr_uint32));
            gpr_slice_buffer_add(&transport_parsing->qbuf,
                                 grpc_chttp2_settings_ack_create());
          }
          return GRPC_CHTTP2_PARSE_OK;
        }
        parser->id = (gpr_uint16)(((gpr_uint16)*cur) << 8);
        cur++;
      /* fallthrough */
      case GRPC_CHTTP2_SPS_ID1:
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_ID1;
          return GRPC_CHTTP2_PARSE_OK;
        }
        parser->id = (gpr_uint16)(parser->id | (*cur));
        cur++;
      /* fallthrough */
      case GRPC_CHTTP2_SPS_VAL0:
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_VAL0;
          return GRPC_CHTTP2_PARSE_OK;
        }
        parser->value = ((gpr_uint32)*cur) << 24;
        cur++;
      /* fallthrough */
      case GRPC_CHTTP2_SPS_VAL1:
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_VAL1;
          return GRPC_CHTTP2_PARSE_OK;
        }
        parser->value |= ((gpr_uint32)*cur) << 16;
        cur++;
      /* fallthrough */
      case GRPC_CHTTP2_SPS_VAL2:
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_VAL2;
          return GRPC_CHTTP2_PARSE_OK;
        }
        parser->value |= ((gpr_uint32)*cur) << 8;
        cur++;
      /* fallthrough */
      case GRPC_CHTTP2_SPS_VAL3:
        if (cur == end) {
          parser->state = GRPC_CHTTP2_SPS_VAL3;
          return GRPC_CHTTP2_PARSE_OK;
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
                gpr_log(GPR_ERROR, "invalid value %u passed for %s",
                        parser->value, sp->name);
                return GRPC_CHTTP2_CONNECTION_ERROR;
            }
          }
          if (parser->id == GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE &&
              parser->incoming_settings[parser->id] != parser->value) {
            transport_parsing->initial_window_update =
                (gpr_int64)parser->value -
                parser->incoming_settings[parser->id];
            gpr_log(GPR_DEBUG, "adding %d for initial_window change",
                    (int)transport_parsing->initial_window_update);
          }
          parser->incoming_settings[parser->id] = parser->value;
          if (grpc_http_trace) {
            gpr_log(GPR_DEBUG, "CHTTP2:%s: got setting %d = %d",
                    transport_parsing->is_client ? "CLI" : "SVR", parser->id,
                    parser->value);
          }
        } else {
          gpr_log(GPR_ERROR, "CHTTP2: Ignoring unknown setting %d (value %d)",
                  parser->id, parser->value);
        }
        break;
    }
  }
}
