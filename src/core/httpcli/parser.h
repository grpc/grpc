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

#ifndef GRPC_INTERNAL_CORE_HTTPCLI_PARSER_H
#define GRPC_INTERNAL_CORE_HTTPCLI_PARSER_H

#include "src/core/httpcli/httpcli.h"
#include <grpc/support/port_platform.h>
#include <grpc/support/slice.h>

typedef enum {
  GRPC_HTTPCLI_INITIAL_RESPONSE,
  GRPC_HTTPCLI_HEADERS,
  GRPC_HTTPCLI_BODY
} grpc_httpcli_parser_state;

typedef struct {
  grpc_httpcli_parser_state state;

  grpc_httpcli_response r;
  size_t body_capacity;
  size_t hdr_capacity;

  gpr_uint8 cur_line[GRPC_HTTPCLI_MAX_HEADER_LENGTH];
  size_t cur_line_length;
} grpc_httpcli_parser;

void grpc_httpcli_parser_init(grpc_httpcli_parser* parser);
void grpc_httpcli_parser_destroy(grpc_httpcli_parser* parser);

int grpc_httpcli_parser_parse(grpc_httpcli_parser* parser, gpr_slice slice);
int grpc_httpcli_parser_eof(grpc_httpcli_parser* parser);

#endif /* GRPC_INTERNAL_CORE_HTTPCLI_PARSER_H */
