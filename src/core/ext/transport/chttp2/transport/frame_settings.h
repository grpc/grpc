/*
 *
 * Copyright 2015, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FRAME_SETTINGS_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FRAME_SETTINGS_H

#include <grpc/slice.h>
#include <grpc/support/port_platform.h>
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
grpc_slice grpc_chttp2_settings_create(uint32_t *old, const uint32_t *newval,
                                       uint32_t force_mask, size_t count);
/* Create an ack settings frame */
grpc_slice grpc_chttp2_settings_ack_create(void);

grpc_error *grpc_chttp2_settings_parser_begin_frame(
    grpc_chttp2_settings_parser *parser, uint32_t length, uint8_t flags,
    uint32_t *settings);
grpc_error *grpc_chttp2_settings_parser_parse(grpc_exec_ctx *exec_ctx,
                                              void *parser,
                                              grpc_chttp2_transport *t,
                                              grpc_chttp2_stream *s,
                                              grpc_slice slice, int is_last);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FRAME_SETTINGS_H */
