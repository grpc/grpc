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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FRAME_SETTINGS_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FRAME_SETTINGS_H

#include <grpc/support/port_platform.h>
#include <grpc/support/slice.h>
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/lib/iomgr/exec_ctx.h"

typedef enum {
  GRPC_CHTTP2_SPS_ID0,
  GRPC_CHTTP2_SPS_ID1,
  GRPC_CHTTP2_SPS_VAL0,
  GRPC_CHTTP2_SPS_VAL1,
  GRPC_CHTTP2_SPS_VAL2,
  GRPC_CHTTP2_SPS_VAL3
} grpc_chttp2_settings_parse_state;

/* The things HTTP/2 defines as connection level settings */
typedef enum {
  GRPC_CHTTP2_SETTINGS_HEADER_TABLE_SIZE = 1,
  GRPC_CHTTP2_SETTINGS_ENABLE_PUSH = 2,
  GRPC_CHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS = 3,
  GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE = 4,
  GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE = 5,
  GRPC_CHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE = 6,
  GRPC_CHTTP2_NUM_SETTINGS
} grpc_chttp2_setting_id;

typedef struct {
  grpc_chttp2_settings_parse_state state;
  uint32_t *target_settings;
  uint8_t is_ack;
  uint16_t id;
  uint32_t value;
  uint32_t incoming_settings[GRPC_CHTTP2_NUM_SETTINGS];
} grpc_chttp2_settings_parser;

typedef enum {
  GRPC_CHTTP2_CLAMP_INVALID_VALUE,
  GRPC_CHTTP2_DISCONNECT_ON_INVALID_VALUE
} grpc_chttp2_invalid_value_behavior;

typedef struct {
  const char *name;
  uint32_t default_value;
  uint32_t min_value;
  uint32_t max_value;
  grpc_chttp2_invalid_value_behavior invalid_value_behavior;
  uint32_t error_value;
} grpc_chttp2_setting_parameters;

/* HTTP/2 mandated connection setting parameters */
extern const grpc_chttp2_setting_parameters
    grpc_chttp2_settings_parameters[GRPC_CHTTP2_NUM_SETTINGS];

/* Create a settings frame by diffing old & new, and updating old to be new */
gpr_slice grpc_chttp2_settings_create(uint32_t *old, const uint32_t *new,
                                      uint32_t force_mask, size_t count);
/* Create an ack settings frame */
gpr_slice grpc_chttp2_settings_ack_create(void);

grpc_error *grpc_chttp2_settings_parser_begin_frame(
    grpc_chttp2_settings_parser *parser, uint32_t length, uint8_t flags,
    uint32_t *settings);
grpc_error *grpc_chttp2_settings_parser_parse(
    grpc_exec_ctx *exec_ctx, void *parser,
    grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_parsing *stream_parsing, gpr_slice slice, int is_last);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FRAME_SETTINGS_H */
