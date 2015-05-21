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

#ifndef GRPC_INTERNAL_CORE_TRANSPORT_CHTTP2_FRAME_H
#define GRPC_INTERNAL_CORE_TRANSPORT_CHTTP2_FRAME_H

#include <grpc/support/port_platform.h>
#include <grpc/support/slice.h>

/* Common definitions for frame handling in the chttp2 transport */

typedef enum {
  GRPC_CHTTP2_PARSE_OK,
  GRPC_CHTTP2_STREAM_ERROR,
  GRPC_CHTTP2_CONNECTION_ERROR
} grpc_chttp2_parse_error;

typedef struct {
  gpr_uint8 end_of_stream;
  gpr_uint8 need_flush_reads;
  gpr_uint8 metadata_boundary;
  gpr_uint8 ack_settings;
  gpr_uint8 send_ping_ack;
  gpr_uint8 process_ping_reply;
  gpr_uint8 goaway;
  gpr_uint8 rst_stream;

  gpr_int64 initial_window_update;
  gpr_uint32 window_update;
  gpr_uint32 goaway_last_stream_index;
  gpr_uint32 goaway_error;
  gpr_slice goaway_text;
  gpr_uint32 rst_stream_reason;
} grpc_chttp2_parse_state;

#define GRPC_CHTTP2_FRAME_DATA 0
#define GRPC_CHTTP2_FRAME_HEADER 1
#define GRPC_CHTTP2_FRAME_CONTINUATION 9
#define GRPC_CHTTP2_FRAME_RST_STREAM 3
#define GRPC_CHTTP2_FRAME_SETTINGS 4
#define GRPC_CHTTP2_FRAME_PING 6
#define GRPC_CHTTP2_FRAME_GOAWAY 7
#define GRPC_CHTTP2_FRAME_WINDOW_UPDATE 8

#define GRPC_CHTTP2_MAX_PAYLOAD_LENGTH ((1 << 14) - 1)

#define GRPC_CHTTP2_DATA_FLAG_END_STREAM 1
#define GRPC_CHTTP2_FLAG_ACK 1
#define GRPC_CHTTP2_DATA_FLAG_END_HEADERS 4
#define GRPC_CHTTP2_DATA_FLAG_PADDED 8
#define GRPC_CHTTP2_FLAG_HAS_PRIORITY 0x20

#endif  /* GRPC_INTERNAL_CORE_TRANSPORT_CHTTP2_FRAME_H */
