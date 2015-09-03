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

#ifndef GRPC_INTERNAL_CORE_TRANSPORT_CHTTP2_FRAME_WINDOW_UPDATE_H
#define GRPC_INTERNAL_CORE_TRANSPORT_CHTTP2_FRAME_WINDOW_UPDATE_H

#include <grpc/support/slice.h>
#include "src/core/transport/chttp2/frame.h"

typedef struct
{
  gpr_uint8 byte;
  gpr_uint8 is_connection_update;
  gpr_uint32 amount;
} grpc_chttp2_window_update_parser;

gpr_slice grpc_chttp2_window_update_create (gpr_uint32 id,
					    gpr_uint32 window_delta);

grpc_chttp2_parse_error
grpc_chttp2_window_update_parser_begin_frame (grpc_chttp2_window_update_parser
					      * parser, gpr_uint32 length,
					      gpr_uint8 flags);
grpc_chttp2_parse_error grpc_chttp2_window_update_parser_parse (void *parser,
								grpc_chttp2_transport_parsing
								*
								transport_parsing,
								grpc_chttp2_stream_parsing
								*
								stream_parsing,
								gpr_slice
								slice,
								int is_last);

#endif /* GRPC_INTERNAL_CORE_TRANSPORT_CHTTP2_FRAME_WINDOW_UPDATE_H */
