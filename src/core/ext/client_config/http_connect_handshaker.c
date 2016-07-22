/*
 *
 * Copyright 2016, Google Inc.
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

#include <string.h>

#include <grpc/impl/codegen/alloc.h>
#include <grpc/impl/codegen/log.h>
#include <grpc/impl/codegen/slice_buffer.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/http/format_request.h"
#include "src/core/lib/http/parser.h"
#include "src/core/ext/client_config/http_connect_handshaker.h"

typedef struct http_connect_handshaker {
  // Base class.  Must be first.
  grpc_handshaker base;

  // These pointers are borrowed, we don't own them.
  char* proxy_server;
  char* server_name;

  // State saved while performing the handshake.
  grpc_endpoint* endpoint;
  grpc_channel_args* args;
  grpc_handshaker_done_cb cb;
  void* user_data;

  // Objects for processing the HTTP CONNECT request and response.
  gpr_slice_buffer request_buffer;
  grpc_closure request_done_closure;
  gpr_slice_buffer response_buffer;
  grpc_closure response_read_closure;
  grpc_http_parser http_parser;
  grpc_http_response http_response;
} http_connect_handshaker;

// Callback invoked when finished writing HTTP CONNECT request.
static void on_write_done(grpc_exec_ctx* exec_ctx, void* arg,
                          grpc_error* error) {
  http_connect_handshaker* h = arg;
  grpc_endpoint_read(exec_ctx, h->endpoint, &h->response_buffer,
                     &h->response_read_closure);
}

// Callback invoked for reading HTTP CONNECT response.
static void on_read_done(grpc_exec_ctx* exec_ctx, void* arg,
                         grpc_error* error) {
  http_connect_handshaker* h = arg;
  if (error == GRPC_ERROR_NONE) {
    for (size_t i = 0; i < h->response_buffer.count; ++i) {
      if (GPR_SLICE_LENGTH(h->response_buffer.slices[i]) > 0) {
        error = grpc_http_parser_parse(
            &h->http_parser, h->response_buffer.slices[i]);
        if (error != GRPC_ERROR_NONE)
          goto done;
      }
    }
    // If we're not done reading the response, read more data.
    // TODO(roth): In practice, I suspect that the response to a CONNECT
    // request will never include a body, in which case this check is
    // sufficient.  However, the language of RFC-2817 doesn't explicitly
    // forbid the response from including a body.  If there is a body,
    // it's possible that we might have parsed part but not all of the
    // body, in which case this check will cause us to fail to parse the
    // remainder of the body.  If that ever becomes an issue, we may
    // need to fix the HTTP parser to understand when the body is
    // complete (e.g., handling chunked transfer encoding or looking
    // at the Content-Length: header).
    if (h->http_parser.state != GRPC_HTTP_BODY) {
      grpc_endpoint_read(exec_ctx, h->endpoint, &h->response_buffer,
                         &h->response_read_closure);
      return;
    }
  }
 done:
  // Invoke handshake-done callback.
  h->cb(exec_ctx, h->endpoint, h->args, h->user_data, GRPC_ERROR_REF(error));
}

//
// Public handshaker methods
//

static void http_connect_handshaker_destroy(grpc_exec_ctx* exec_ctx,
                                            grpc_handshaker* handshaker) {
  http_connect_handshaker* h = (http_connect_handshaker*)handshaker;
  gpr_slice_buffer_destroy(&h->request_buffer);
  gpr_slice_buffer_destroy(&h->response_buffer);
  grpc_http_parser_destroy(&h->http_parser);
  grpc_http_response_destroy(&h->http_response);
  gpr_free(h);
}

static void http_connect_handshaker_shutdown(grpc_exec_ctx* exec_ctx,
                                             grpc_handshaker* handshaker) {
}

static void http_connect_handshaker_do_handshake(
    grpc_exec_ctx* exec_ctx, grpc_handshaker* handshaker,
    grpc_endpoint* endpoint, grpc_channel_args* args, gpr_timespec deadline,
    grpc_tcp_server_acceptor* acceptor, grpc_handshaker_done_cb cb,
    void* user_data) {
  http_connect_handshaker* h = (http_connect_handshaker*)handshaker;
  // Save state in the handshaker object.
  h->endpoint = endpoint;
  h->args = args;
  h->cb = cb;
  h->user_data = user_data;
  // Initialize fields.
  gpr_slice_buffer_init(&h->request_buffer);
  grpc_closure_init(&h->request_done_closure, on_write_done, h);
  gpr_slice_buffer_init(&h->response_buffer);
  grpc_closure_init(&h->response_read_closure, on_read_done, h);
  grpc_http_parser_init(&h->http_parser, GRPC_HTTP_RESPONSE,
                        &h->http_response);
  // Send HTTP CONNECT request.
  grpc_httpcli_request request;
  memset(&request, 0, sizeof(request));
  request.host = gpr_strdup(h->proxy_server);
  request.http.method = gpr_strdup("CONNECT");
  request.http.path = gpr_strdup(h->server_name);
  request.handshaker = &grpc_httpcli_plaintext;
  gpr_slice request_slice = grpc_httpcli_format_connect_request(&request);
  gpr_slice_buffer_add(&h->request_buffer, request_slice);
  grpc_endpoint_write(exec_ctx, endpoint, &h->request_buffer,
                      &h->request_done_closure);
}

static const struct grpc_handshaker_vtable http_connect_handshaker_vtable = {
    http_connect_handshaker_destroy, http_connect_handshaker_shutdown,
    http_connect_handshaker_do_handshake};

char* grpc_get_http_connect_proxy_server_from_args(grpc_channel_args* args) {
  for (size_t i = 0; i < args->num_args; ++i) {
    if (strcmp(args->args[i].key, GRPC_ARG_HTTP_CONNECT_PROXY_SERVER) == 0) {
      if (args->args[i].type != GRPC_ARG_STRING) {
        gpr_log(GPR_ERROR, "%s: must be a string",
                GRPC_ARG_HTTP_CONNECT_PROXY_SERVER);
        break;
      }
      return args->args[i].value.string;
    }
  }
  return NULL;
}

grpc_handshaker* grpc_http_connect_handshaker_create(char* proxy_server,
                                                     char* server_name) {
  http_connect_handshaker* handshaker =
      gpr_malloc(sizeof(http_connect_handshaker));
  memset(handshaker, 0, sizeof(*handshaker));
  grpc_handshaker_init(&http_connect_handshaker_vtable, &handshaker->base);
  handshaker->proxy_server = proxy_server;
  handshaker->server_name = server_name;
  return (grpc_handshaker*)handshaker;
}
