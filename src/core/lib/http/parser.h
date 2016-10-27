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

#ifndef GRPC_CORE_LIB_HTTP_PARSER_H
#define GRPC_CORE_LIB_HTTP_PARSER_H

#include <grpc/slice.h>
#include <grpc/support/port_platform.h>
#include "src/core/lib/iomgr/error.h"

/* Maximum length of a header string of the form 'Key: Value\r\n' */
#define GRPC_HTTP_PARSER_MAX_HEADER_LENGTH 4096

/* A single header to be passed in a request */
typedef struct grpc_http_header {
  char *key;
  char *value;
} grpc_http_header;

typedef enum {
  GRPC_HTTP_FIRST_LINE,
  GRPC_HTTP_HEADERS,
  GRPC_HTTP_BODY
} grpc_http_parser_state;

typedef enum {
  GRPC_HTTP_HTTP10,
  GRPC_HTTP_HTTP11,
  GRPC_HTTP_HTTP20,
} grpc_http_version;

typedef enum {
  GRPC_HTTP_RESPONSE,
  GRPC_HTTP_REQUEST,
} grpc_http_type;

/* A request */
typedef struct grpc_http_request {
  /* Method of the request (e.g. GET, POST) */
  char *method;
  /* The path of the resource to fetch */
  char *path;
  /* HTTP version to use */
  grpc_http_version version;
  /* Headers attached to the request */
  size_t hdr_count;
  grpc_http_header *hdrs;
  /* Body: length and contents; contents are NOT null-terminated */
  size_t body_length;
  char *body;
} grpc_http_request;

/* A response */
typedef struct grpc_http_response {
  /* HTTP status code */
  int status;
  /* Headers: count and key/values */
  size_t hdr_count;
  grpc_http_header *hdrs;
  /* Body: length and contents; contents are NOT null-terminated */
  size_t body_length;
  char *body;
} grpc_http_response;

typedef struct {
  grpc_http_parser_state state;
  grpc_http_type type;

  union {
    grpc_http_response *response;
    grpc_http_request *request;
    void *request_or_response;
  } http;
  size_t body_capacity;
  size_t hdr_capacity;

  uint8_t cur_line[GRPC_HTTP_PARSER_MAX_HEADER_LENGTH];
  size_t cur_line_length;
  size_t cur_line_end_length;
} grpc_http_parser;

void grpc_http_parser_init(grpc_http_parser *parser, grpc_http_type type,
                           void *request_or_response);
void grpc_http_parser_destroy(grpc_http_parser *parser);

/* Sets \a start_of_body to the offset in \a slice of the start of the body. */
grpc_error *grpc_http_parser_parse(grpc_http_parser *parser, grpc_slice slice,
                                   size_t *start_of_body);
grpc_error *grpc_http_parser_eof(grpc_http_parser *parser);

void grpc_http_request_destroy(grpc_http_request *request);
void grpc_http_response_destroy(grpc_http_response *response);

extern int grpc_http1_trace;

#endif /* GRPC_CORE_LIB_HTTP_PARSER_H */
