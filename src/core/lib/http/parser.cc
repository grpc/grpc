/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/lib/http/parser.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/useful.h"

grpc_core::TraceFlag grpc_http1_trace(false, "http1");

static char* buf2str(void* buffer, size_t length) {
  char* out = static_cast<char*>(gpr_malloc(length + 1));
  memcpy(out, buffer, length);
  out[length] = 0;
  return out;
}

static grpc_error* handle_response_line(grpc_http_parser* parser) {
  uint8_t* beg = parser->cur_line;
  uint8_t* cur = beg;
  uint8_t* end = beg + parser->cur_line_length;

  if (cur == end || *cur++ != 'H')
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Expected 'H'");
  if (cur == end || *cur++ != 'T')
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Expected 'T'");
  if (cur == end || *cur++ != 'T')
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Expected 'T'");
  if (cur == end || *cur++ != 'P')
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Expected 'P'");
  if (cur == end || *cur++ != '/')
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Expected '/'");
  if (cur == end || *cur++ != '1')
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Expected '1'");
  if (cur == end || *cur++ != '.')
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Expected '.'");
  if (cur == end || *cur < '0' || *cur++ > '1') {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Expected HTTP/1.0 or HTTP/1.1");
  }
  if (cur == end || *cur++ != ' ')
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Expected ' '");
  if (cur == end || *cur < '1' || *cur++ > '9')
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Expected status code");
  if (cur == end || *cur < '0' || *cur++ > '9')
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Expected status code");
  if (cur == end || *cur < '0' || *cur++ > '9')
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Expected status code");
  parser->http.response->status =
      (cur[-3] - '0') * 100 + (cur[-2] - '0') * 10 + (cur[-1] - '0');
  if (cur == end || *cur++ != ' ')
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Expected ' '");

  /* we don't really care about the status code message */

  return GRPC_ERROR_NONE;
}

static grpc_error* handle_request_line(grpc_http_parser* parser) {
  uint8_t* beg = parser->cur_line;
  uint8_t* cur = beg;
  uint8_t* end = beg + parser->cur_line_length;
  uint8_t vers_major = 0;
  uint8_t vers_minor = 0;

  while (cur != end && *cur++ != ' ')
    ;
  if (cur == end)
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "No method on HTTP request line");
  parser->http.request->method =
      buf2str(beg, static_cast<size_t>(cur - beg - 1));

  beg = cur;
  while (cur != end && *cur++ != ' ')
    ;
  if (cur == end)
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("No path on HTTP request line");
  parser->http.request->path = buf2str(beg, static_cast<size_t>(cur - beg - 1));

  if (cur == end || *cur++ != 'H')
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Expected 'H'");
  if (cur == end || *cur++ != 'T')
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Expected 'T'");
  if (cur == end || *cur++ != 'T')
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Expected 'T'");
  if (cur == end || *cur++ != 'P')
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Expected 'P'");
  if (cur == end || *cur++ != '/')
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Expected '/'");
  vers_major = static_cast<uint8_t>(*cur++ - '1' + 1);
  ++cur;
  if (cur == end)
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "End of line in HTTP version string");
  vers_minor = static_cast<uint8_t>(*cur++ - '1' + 1);

  if (vers_major == 1) {
    if (vers_minor == 0) {
      parser->http.request->version = GRPC_HTTP_HTTP10;
    } else if (vers_minor == 1) {
      parser->http.request->version = GRPC_HTTP_HTTP11;
    } else {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Expected one of HTTP/1.0, HTTP/1.1, or HTTP/2.0");
    }
  } else if (vers_major == 2) {
    if (vers_minor == 0) {
      parser->http.request->version = GRPC_HTTP_HTTP20;
    } else {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Expected one of HTTP/1.0, HTTP/1.1, or HTTP/2.0");
    }
  } else {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Expected one of HTTP/1.0, HTTP/1.1, or HTTP/2.0");
  }

  return GRPC_ERROR_NONE;
}

static grpc_error* handle_first_line(grpc_http_parser* parser) {
  switch (parser->type) {
    case GRPC_HTTP_REQUEST:
      return handle_request_line(parser);
    case GRPC_HTTP_RESPONSE:
      return handle_response_line(parser);
  }
  GPR_UNREACHABLE_CODE(
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Should never reach here"));
}

static grpc_error* add_header(grpc_http_parser* parser) {
  uint8_t* beg = parser->cur_line;
  uint8_t* cur = beg;
  uint8_t* end = beg + parser->cur_line_length;
  size_t* hdr_count = nullptr;
  grpc_http_header** hdrs = nullptr;
  grpc_http_header hdr = {nullptr, nullptr};
  grpc_error* error = GRPC_ERROR_NONE;

  GPR_ASSERT(cur != end);

  if (*cur == ' ' || *cur == '\t') {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Continued header lines not supported yet");
    goto done;
  }

  while (cur != end && *cur != ':') {
    cur++;
  }
  if (cur == end) {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Didn't find ':' in header string");
    goto done;
  }
  GPR_ASSERT(cur >= beg);
  hdr.key = buf2str(beg, static_cast<size_t>(cur - beg));
  cur++; /* skip : */

  while (cur != end && (*cur == ' ' || *cur == '\t')) {
    cur++;
  }
  GPR_ASSERT((size_t)(end - cur) >= parser->cur_line_end_length);
  hdr.value = buf2str(
      cur, static_cast<size_t>(end - cur) - parser->cur_line_end_length);

  switch (parser->type) {
    case GRPC_HTTP_RESPONSE:
      hdr_count = &parser->http.response->hdr_count;
      hdrs = &parser->http.response->hdrs;
      break;
    case GRPC_HTTP_REQUEST:
      hdr_count = &parser->http.request->hdr_count;
      hdrs = &parser->http.request->hdrs;
      break;
  }

  if (*hdr_count == parser->hdr_capacity) {
    parser->hdr_capacity =
        GPR_MAX(parser->hdr_capacity + 1, parser->hdr_capacity * 3 / 2);
    *hdrs = static_cast<grpc_http_header*>(
        gpr_realloc(*hdrs, parser->hdr_capacity * sizeof(**hdrs)));
  }
  (*hdrs)[(*hdr_count)++] = hdr;

done:
  if (error != GRPC_ERROR_NONE) {
    gpr_free(hdr.key);
    gpr_free(hdr.value);
  }
  return error;
}

static grpc_error* finish_line(grpc_http_parser* parser,
                               bool* found_body_start) {
  grpc_error* err;
  switch (parser->state) {
    case GRPC_HTTP_FIRST_LINE:
      err = handle_first_line(parser);
      if (err != GRPC_ERROR_NONE) return err;
      parser->state = GRPC_HTTP_HEADERS;
      break;
    case GRPC_HTTP_HEADERS:
      if (parser->cur_line_length == parser->cur_line_end_length) {
        parser->state = GRPC_HTTP_BODY;
        *found_body_start = true;
        break;
      }
      err = add_header(parser);
      if (err != GRPC_ERROR_NONE) {
        return err;
      }
      break;
    case GRPC_HTTP_BODY:
      GPR_UNREACHABLE_CODE(return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Should never reach here"));
  }

  parser->cur_line_length = 0;
  return GRPC_ERROR_NONE;
}

static grpc_error* addbyte_body(grpc_http_parser* parser, uint8_t byte) {
  size_t* body_length = nullptr;
  char** body = nullptr;

  if (parser->type == GRPC_HTTP_RESPONSE) {
    body_length = &parser->http.response->body_length;
    body = &parser->http.response->body;
  } else if (parser->type == GRPC_HTTP_REQUEST) {
    body_length = &parser->http.request->body_length;
    body = &parser->http.request->body;
  } else {
    GPR_UNREACHABLE_CODE(
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Should never reach here"));
  }

  if (*body_length == parser->body_capacity) {
    parser->body_capacity = GPR_MAX(8, parser->body_capacity * 3 / 2);
    *body =
        static_cast<char*>(gpr_realloc((void*)*body, parser->body_capacity));
  }
  (*body)[*body_length] = static_cast<char>(byte);
  (*body_length)++;

  return GRPC_ERROR_NONE;
}

static bool check_line(grpc_http_parser* parser) {
  if (parser->cur_line_length >= 2 &&
      parser->cur_line[parser->cur_line_length - 2] == '\r' &&
      parser->cur_line[parser->cur_line_length - 1] == '\n') {
    return true;
  }

  // HTTP request with \n\r line termiantors.
  else if (parser->cur_line_length >= 2 &&
           parser->cur_line[parser->cur_line_length - 2] == '\n' &&
           parser->cur_line[parser->cur_line_length - 1] == '\r') {
    return true;
  }

  // HTTP request with only \n line terminators.
  else if (parser->cur_line_length >= 1 &&
           parser->cur_line[parser->cur_line_length - 1] == '\n') {
    parser->cur_line_end_length = 1;
    return true;
  }

  return false;
}

static grpc_error* addbyte(grpc_http_parser* parser, uint8_t byte,
                           bool* found_body_start) {
  switch (parser->state) {
    case GRPC_HTTP_FIRST_LINE:
    case GRPC_HTTP_HEADERS:
      if (parser->cur_line_length >= GRPC_HTTP_PARSER_MAX_HEADER_LENGTH) {
        if (grpc_http1_trace.enabled())
          gpr_log(GPR_ERROR, "HTTP header max line length (%d) exceeded",
                  GRPC_HTTP_PARSER_MAX_HEADER_LENGTH);
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "HTTP header max line length exceeded");
      }
      parser->cur_line[parser->cur_line_length] = byte;
      parser->cur_line_length++;
      if (check_line(parser)) {
        return finish_line(parser, found_body_start);
      }
      return GRPC_ERROR_NONE;
    case GRPC_HTTP_BODY:
      return addbyte_body(parser, byte);
  }
  GPR_UNREACHABLE_CODE(return GRPC_ERROR_NONE);
}

void grpc_http_parser_init(grpc_http_parser* parser, grpc_http_type type,
                           void* request_or_response) {
  memset(parser, 0, sizeof(*parser));
  parser->state = GRPC_HTTP_FIRST_LINE;
  parser->type = type;
  parser->http.request_or_response = request_or_response;
  parser->cur_line_end_length = 2;
}

void grpc_http_parser_destroy(grpc_http_parser* parser) {}

void grpc_http_request_destroy(grpc_http_request* request) {
  size_t i;
  gpr_free(request->body);
  for (i = 0; i < request->hdr_count; i++) {
    gpr_free(request->hdrs[i].key);
    gpr_free(request->hdrs[i].value);
  }
  gpr_free(request->hdrs);
  gpr_free(request->method);
  gpr_free(request->path);
}

void grpc_http_response_destroy(grpc_http_response* response) {
  size_t i;
  gpr_free(response->body);
  for (i = 0; i < response->hdr_count; i++) {
    gpr_free(response->hdrs[i].key);
    gpr_free(response->hdrs[i].value);
  }
  gpr_free(response->hdrs);
}

grpc_error* grpc_http_parser_parse(grpc_http_parser* parser,
                                   const grpc_slice& slice,
                                   size_t* start_of_body) {
  for (size_t i = 0; i < GRPC_SLICE_LENGTH(slice); i++) {
    bool found_body_start = false;
    grpc_error* err =
        addbyte(parser, GRPC_SLICE_START_PTR(slice)[i], &found_body_start);
    if (err != GRPC_ERROR_NONE) return err;
    if (found_body_start && start_of_body != nullptr) *start_of_body = i + 1;
  }
  return GRPC_ERROR_NONE;
}

grpc_error* grpc_http_parser_eof(grpc_http_parser* parser) {
  if (parser->state != GRPC_HTTP_BODY) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Did not finish headers");
  }
  return GRPC_ERROR_NONE;
}
