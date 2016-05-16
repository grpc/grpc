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

#include "src/core/lib/http/parser.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

int grpc_http1_trace = 0;

static char *buf2str(void *buffer, size_t length) {
  char *out = gpr_malloc(length + 1);
  memcpy(out, buffer, length);
  out[length] = 0;
  return out;
}

static int handle_response_line(grpc_http_parser *parser) {
  uint8_t *beg = parser->cur_line;
  uint8_t *cur = beg;
  uint8_t *end = beg + parser->cur_line_length;

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
  parser->http.response.status =
      (cur[-3] - '0') * 100 + (cur[-2] - '0') * 10 + (cur[-1] - '0');
  if (cur == end || *cur++ != ' ') goto error;

  /* we don't really care about the status code message */

  return 1;

error:
  if (grpc_http1_trace) gpr_log(GPR_ERROR, "Failed parsing response line");
  return 0;
}

static int handle_request_line(grpc_http_parser *parser) {
  uint8_t *beg = parser->cur_line;
  uint8_t *cur = beg;
  uint8_t *end = beg + parser->cur_line_length;
  uint8_t vers_major = 0;
  uint8_t vers_minor = 0;

  while (cur != end && *cur++ != ' ')
    ;
  if (cur == end) goto error;
  parser->http.request.method = buf2str(beg, (size_t)(cur - beg - 1));

  beg = cur;
  while (cur != end && *cur++ != ' ')
    ;
  if (cur == end) goto error;
  parser->http.request.path = buf2str(beg, (size_t)(cur - beg - 1));

  if (cur == end || *cur++ != 'H') goto error;
  if (cur == end || *cur++ != 'T') goto error;
  if (cur == end || *cur++ != 'T') goto error;
  if (cur == end || *cur++ != 'P') goto error;
  if (cur == end || *cur++ != '/') goto error;
  vers_major = (uint8_t)(*cur++ - '1' + 1);
  ++cur;
  if (cur == end) goto error;
  vers_minor = (uint8_t)(*cur++ - '1' + 1);

  if (vers_major == 1) {
    if (vers_minor == 0) {
      parser->http.request.version = GRPC_HTTP_HTTP10;
    } else if (vers_minor == 1) {
      parser->http.request.version = GRPC_HTTP_HTTP11;
    } else {
      goto error;
    }
  } else if (vers_major == 2) {
    if (vers_minor == 0) {
      parser->http.request.version = GRPC_HTTP_HTTP20;
    } else {
      goto error;
    }
  } else {
    goto error;
  }

  return 1;

error:
  if (grpc_http1_trace) gpr_log(GPR_ERROR, "Failed parsing request line");
  return 0;
}

static int handle_first_line(grpc_http_parser *parser) {
  if (parser->cur_line[0] == 'H') {
    parser->type = GRPC_HTTP_RESPONSE;
    return handle_response_line(parser);
  } else {
    parser->type = GRPC_HTTP_REQUEST;
    return handle_request_line(parser);
  }
}

static int add_header(grpc_http_parser *parser) {
  uint8_t *beg = parser->cur_line;
  uint8_t *cur = beg;
  uint8_t *end = beg + parser->cur_line_length;
  size_t *hdr_count = NULL;
  grpc_http_header **hdrs = NULL;
  grpc_http_header hdr = {NULL, NULL};

  GPR_ASSERT(cur != end);

  if (*cur == ' ' || *cur == '\t') {
    if (grpc_http1_trace)
      gpr_log(GPR_ERROR, "Continued header lines not supported yet");
    goto error;
  }

  while (cur != end && *cur != ':') {
    cur++;
  }
  if (cur == end) {
    if (grpc_http1_trace) {
      gpr_log(GPR_ERROR, "Didn't find ':' in header string");
    }
    goto error;
  }
  GPR_ASSERT(cur >= beg);
  hdr.key = buf2str(beg, (size_t)(cur - beg));
  cur++; /* skip : */

  while (cur != end && (*cur == ' ' || *cur == '\t')) {
    cur++;
  }
  GPR_ASSERT((size_t)(end - cur) >= parser->cur_line_end_length);
  hdr.value = buf2str(cur, (size_t)(end - cur) - parser->cur_line_end_length);

  if (parser->type == GRPC_HTTP_RESPONSE) {
    hdr_count = &parser->http.response.hdr_count;
    hdrs = &parser->http.response.hdrs;
  } else if (parser->type == GRPC_HTTP_REQUEST) {
    hdr_count = &parser->http.request.hdr_count;
    hdrs = &parser->http.request.hdrs;
  } else {
    return 0;
  }

  if (*hdr_count == parser->hdr_capacity) {
    parser->hdr_capacity =
        GPR_MAX(parser->hdr_capacity + 1, parser->hdr_capacity * 3 / 2);
    *hdrs = gpr_realloc(*hdrs, parser->hdr_capacity * sizeof(**hdrs));
  }
  (*hdrs)[(*hdr_count)++] = hdr;
  return 1;

error:
  gpr_free(hdr.key);
  gpr_free(hdr.value);
  return 0;
}

static int finish_line(grpc_http_parser *parser) {
  switch (parser->state) {
    case GRPC_HTTP_FIRST_LINE:
      if (!handle_first_line(parser)) {
        return 0;
      }
      parser->state = GRPC_HTTP_HEADERS;
      break;
    case GRPC_HTTP_HEADERS:
      if (parser->cur_line_length == parser->cur_line_end_length) {
        parser->state = GRPC_HTTP_BODY;
        break;
      }
      if (!add_header(parser)) {
        return 0;
      }
      break;
    case GRPC_HTTP_BODY:
      GPR_UNREACHABLE_CODE(return 0);
  }

  parser->cur_line_length = 0;
  return 1;
}

static int addbyte_body(grpc_http_parser *parser, uint8_t byte) {
  size_t *body_length = NULL;
  char **body = NULL;

  if (parser->type == GRPC_HTTP_RESPONSE) {
    body_length = &parser->http.response.body_length;
    body = &parser->http.response.body;
  } else if (parser->type == GRPC_HTTP_REQUEST) {
    body_length = &parser->http.request.body_length;
    body = &parser->http.request.body;
  } else {
    return 0;
  }

  if (*body_length == parser->body_capacity) {
    parser->body_capacity = GPR_MAX(8, parser->body_capacity * 3 / 2);
    *body = gpr_realloc((void *)*body, parser->body_capacity);
  }
  (*body)[*body_length] = (char)byte;
  (*body_length)++;

  return 1;
}

static int check_line(grpc_http_parser *parser) {
  if (parser->cur_line_length >= 2 &&
      parser->cur_line[parser->cur_line_length - 2] == '\r' &&
      parser->cur_line[parser->cur_line_length - 1] == '\n') {
    return 1;
  }

  // HTTP request with \n\r line termiantors.
  else if (parser->cur_line_length >= 2 &&
           parser->cur_line[parser->cur_line_length - 2] == '\n' &&
           parser->cur_line[parser->cur_line_length - 1] == '\r') {
    return 1;
  }

  // HTTP request with only \n line terminators.
  else if (parser->cur_line_length >= 1 &&
           parser->cur_line[parser->cur_line_length - 1] == '\n') {
    parser->cur_line_end_length = 1;
    return 1;
  }

  return 0;
}

static int addbyte(grpc_http_parser *parser, uint8_t byte) {
  switch (parser->state) {
    case GRPC_HTTP_FIRST_LINE:
    case GRPC_HTTP_HEADERS:
      if (parser->cur_line_length >= GRPC_HTTP_PARSER_MAX_HEADER_LENGTH) {
        if (grpc_http1_trace)
          gpr_log(GPR_ERROR, "HTTP client max line length (%d) exceeded",
                  GRPC_HTTP_PARSER_MAX_HEADER_LENGTH);
        return 0;
      }
      parser->cur_line[parser->cur_line_length] = byte;
      parser->cur_line_length++;
      if (check_line(parser)) {
        return finish_line(parser);
      } else {
        return 1;
      }
      GPR_UNREACHABLE_CODE(return 0);
    case GRPC_HTTP_BODY:
      return addbyte_body(parser, byte);
  }
  GPR_UNREACHABLE_CODE(return 0);
}

void grpc_http_parser_init(grpc_http_parser *parser) {
  memset(parser, 0, sizeof(*parser));
  parser->state = GRPC_HTTP_FIRST_LINE;
  parser->type = GRPC_HTTP_UNKNOWN;
  parser->cur_line_end_length = 2;
}

void grpc_http_parser_destroy(grpc_http_parser *parser) {
  size_t i;
  if (parser->type == GRPC_HTTP_RESPONSE) {
    gpr_free(parser->http.response.body);
    for (i = 0; i < parser->http.response.hdr_count; i++) {
      gpr_free(parser->http.response.hdrs[i].key);
      gpr_free(parser->http.response.hdrs[i].value);
    }
    gpr_free(parser->http.response.hdrs);
  } else if (parser->type == GRPC_HTTP_REQUEST) {
    gpr_free(parser->http.request.body);
    for (i = 0; i < parser->http.request.hdr_count; i++) {
      gpr_free(parser->http.request.hdrs[i].key);
      gpr_free(parser->http.request.hdrs[i].value);
    }
    gpr_free(parser->http.request.hdrs);
    gpr_free(parser->http.request.method);
    gpr_free(parser->http.request.path);
  }
}

int grpc_http_parser_parse(grpc_http_parser *parser, gpr_slice slice) {
  size_t i;

  for (i = 0; i < GPR_SLICE_LENGTH(slice); i++) {
    if (!addbyte(parser, GPR_SLICE_START_PTR(slice)[i])) {
      return 0;
    }
  }

  return 1;
}

int grpc_http_parser_eof(grpc_http_parser *parser) {
  return parser->state == GRPC_HTTP_BODY;
}
