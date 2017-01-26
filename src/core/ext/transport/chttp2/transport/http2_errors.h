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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_ERRORS_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_ERRORS_H

/* error codes for RST_STREAM from http2 draft 14 section 7 */
typedef enum {
  GRPC_CHTTP2_NO_ERROR = 0x0,
  GRPC_CHTTP2_PROTOCOL_ERROR = 0x1,
  GRPC_CHTTP2_INTERNAL_ERROR = 0x2,
  GRPC_CHTTP2_FLOW_CONTROL_ERROR = 0x3,
  GRPC_CHTTP2_SETTINGS_TIMEOUT = 0x4,
  GRPC_CHTTP2_STREAM_CLOSED = 0x5,
  GRPC_CHTTP2_FRAME_SIZE_ERROR = 0x6,
  GRPC_CHTTP2_REFUSED_STREAM = 0x7,
  GRPC_CHTTP2_CANCEL = 0x8,
  GRPC_CHTTP2_COMPRESSION_ERROR = 0x9,
  GRPC_CHTTP2_CONNECT_ERROR = 0xa,
  GRPC_CHTTP2_ENHANCE_YOUR_CALM = 0xb,
  GRPC_CHTTP2_INADEQUATE_SECURITY = 0xc,
  /* force use of a default clause */
  GRPC_CHTTP2__ERROR_DO_NOT_USE = -1
} grpc_chttp2_error_code;

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_ERRORS_H */
