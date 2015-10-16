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

#include "src/core/httpcli/parser.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

static int handle_response_line(grpc_httpcli_parser *parser) {
  gpr_uint8 *beg = parser->cur_line;
  gpr_uint8 *cur = beg;
  gpr_uint8 *end = beg + parser->cur_line_length;

  if (cur == end || *cur++ != 'H') goto error;
  if (cur == end || *cur++ != 'T') goto error;
  if (cur == end || *cur++ != 'T') goto error;
  if (cur == end || *cur++ != 'P') goto error;
  if (cur == end || *cur++ != '/') goto error;
  if (cur == end || *cur++ != '1') goto error;
  if (cur == end || *cur++ != '.') goto error;
  if (cur == end || *cur < '0' || *cur++ > '1') goto error;
  if (cur == end || *cur++ != ' ') goto error;
  if (cur == end || *cur < '1' || *cur++ > '9') goto error;
  if (cur == end || *cur < '0' || *cur++ > '9') goto error;
  if (cur == end || *cur < '0' || *cur++ > '9') goto error;
  parser->r.status =
      (cur[-3] - '0') * 100 + (cur[-2] - '0') * 10 + (cur[-1] - '0');
  if (cur == end || *cur++ != ' ') goto error;

  /* we don't really care about the status code message */

  return 1;

error:
  gpr_log(GPR_ERROR, "Failed parsing response line");
  return 0;
}

static char *buf2str(void *buffer, size_t length) {
  char *out = gpr_malloc(length + 1);
  memcpy(out, buffer, length);
  out[length] = 0;
  return out;
}

static int add_header(grpc_httpcli_parser *parser) {
  gpr_uint8 *beg = parser->cur_line;
  gpr_uint8 *cur = beg;
  gpr_uint8 *end = beg + parser->cur_line_length;
  grpc_httpcli_header hdr = {NULL, NULL};

  GPR_ASSERT(cur != end);

  if (*cur == ' ' || *cur == '\t') {
    gpr_log(GPR_ERROR, "Continued header lines not supported yet");
    goto error;
  }

  while (cur != end && *cur != ':') {
    cur++;
  }
  if (cur == end) {
    gpr_log(GPR_ERROR, "Didn't find ':' in header string");
    goto error;
  }
  GPR_ASSERT(cur >= beg);
  hdr.key = buf2str(beg, (size_t)(cur - beg));
  cur++; /* skip : */

  while (cur != end && (*cur == ' ' || *cur == '\t')) {
    cur++;
  }
  GPR_ASSERT(end - cur >= 2);
  hdr.value = buf2str(cur, (size_t)(end - cur) - 2);

  if (parser->r.hdr_count == parser->hdr_capacity) {
    parser->hdr_capacity =
        GPR_MAX(parser->hdr_capacity + 1, parser->hdr_capacity * 3 / 2);
    parser->r.hdrs = gpr_realloc(
        parser->r.hdrs, parser->hdr_capacity * sizeof(*parser->r.hdrs));
  }
  parser->r.hdrs[parser->r.hdr_count++] = hdr;
  return 1;

error:
  gpr_free(hdr.key);
  gpr_free(hdr.value);
  return 0;
}

static int finish_line(grpc_httpcli_parser *parser) {
  switch (parser->state) {
    case GRPC_HTTPCLI_INITIAL_RESPONSE:
      if (!handle_response_line(parser)) {
        return 0;
      }
      parser->state = GRPC_HTTPCLI_HEADERS;
      break;
    case GRPC_HTTPCLI_HEADERS:
      if (parser->cur_line_length == 2) {
        parser->state = GRPC_HTTPCLI_BODY;
        break;
      }
      if (!add_header(parser)) {
        return 0;
      }
      break;
    case GRPC_HTTPCLI_BODY:
      GPR_UNREACHABLE_CODE(return 0);
  }

  parser->cur_line_length = 0;
  return 1;
}

static int addbyte(grpc_httpcli_parser *parser, gpr_uint8 byte) {
  switch (parser->state) {
    case GRPC_HTTPCLI_INITIAL_RESPONSE:
    case GRPC_HTTPCLI_HEADERS:
      if (parser->cur_line_length >= GRPC_HTTPCLI_MAX_HEADER_LENGTH) {
        gpr_log(GPR_ERROR, "HTTP client max line length (%d) exceeded",
                GRPC_HTTPCLI_MAX_HEADER_LENGTH);
        return 0;
      }
      parser->cur_line[parser->cur_line_length] = byte;
      parser->cur_line_length++;
      if (parser->cur_line_length >= 2 &&
          parser->cur_line[parser->cur_line_length - 2] == '\r' &&
          parser->cur_line[parser->cur_line_length - 1] == '\n') {
        return finish_line(parser);
      } else {
        return 1;
      }
      GPR_UNREACHABLE_CODE(return 0);
    case GRPC_HTTPCLI_BODY:
      if (parser->r.body_length == parser->body_capacity) {
        parser->body_capacity = GPR_MAX(8, parser->body_capacity * 3 / 2);
        parser->r.body =
            gpr_realloc((void *)parser->r.body, parser->body_capacity);
      }
      parser->r.body[parser->r.body_length] = (char)byte;
      parser->r.body_length++;
      return 1;
  }
  GPR_UNREACHABLE_CODE(return 0);
}

void grpc_httpcli_parser_init(grpc_httpcli_parser *parser) {
  memset(parser, 0, sizeof(*parser));
  parser->state = GRPC_HTTPCLI_INITIAL_RESPONSE;
  parser->r.status = 500;
}

void grpc_httpcli_parser_destroy(grpc_httpcli_parser *parser) {
  size_t i;
  gpr_free(parser->r.body);
  for (i = 0; i < parser->r.hdr_count; i++) {
    gpr_free(parser->r.hdrs[i].key);
    gpr_free(parser->r.hdrs[i].value);
  }
  gpr_free(parser->r.hdrs);
}

int grpc_httpcli_parser_parse(grpc_httpcli_parser *parser, gpr_slice slice) {
  size_t i;

  for (i = 0; i < GPR_SLICE_LENGTH(slice); i++) {
    if (!addbyte(parser, GPR_SLICE_START_PTR(slice)[i])) {
      return 0;
    }
  }

  return 1;
}

int grpc_httpcli_parser_eof(grpc_httpcli_parser *parser) {
  return parser->state == GRPC_HTTPCLI_BODY;
}
