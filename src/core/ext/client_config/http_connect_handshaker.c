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

  char* proxy_server;
  char* server_name;

  // State saved while performing the handshake.
  grpc_endpoint* endpoint;
  grpc_channel_args* args;
  grpc_handshaker_done_cb cb;
  void* user_data;

  // Objects for processing the HTTP CONNECT request and response.
  gpr_slice_buffer write_buffer;
  gpr_slice_buffer* read_buffer;
  grpc_closure request_done_closure;
  grpc_closure response_read_closure;
  grpc_http_parser http_parser;
  grpc_http_response http_response;
} http_connect_handshaker;

// Callback invoked when finished writing HTTP CONNECT request.
static void on_write_done(grpc_exec_ctx* exec_ctx, void* arg,
                          grpc_error* error) {
  http_connect_handshaker* h = arg;
  if (error != GRPC_ERROR_NONE) {
    // If the write failed, invoke the callback immediately with the error.
    h->cb(exec_ctx, h->endpoint, h->args, h->read_buffer, h->user_data,
          GRPC_ERROR_REF(error));
  } else {
    // Otherwise, read the response.
    grpc_endpoint_read(exec_ctx, h->endpoint, h->read_buffer,
                       &h->response_read_closure);
  }
}

// Callback invoked for reading HTTP CONNECT response.
static void on_read_done(grpc_exec_ctx* exec_ctx, void* arg,
                         grpc_error* error) {
  http_connect_handshaker* h = arg;
  if (error != GRPC_ERROR_NONE) {
    GRPC_ERROR_REF(error);  // Take ref to pass to the handshake-done callback.
    goto done;
  }
  // Add buffer to parser.
  for (size_t i = 0; i < h->read_buffer->count; ++i) {
    if (GPR_SLICE_LENGTH(h->read_buffer->slices[i]) > 0) {
      size_t body_start_offset = 0;
      error = grpc_http_parser_parse(
          &h->http_parser, h->read_buffer->slices[i], &body_start_offset);
      if (error != GRPC_ERROR_NONE)
        goto done;
      if (h->http_parser.state == GRPC_HTTP_BODY) {
        // Remove the data we've already read from the read buffer,
        // leaving only the leftover bytes (if any).
        gpr_slice_buffer tmp_buffer;
        gpr_slice_buffer_init(&tmp_buffer);
        if (body_start_offset < GPR_SLICE_LENGTH(h->read_buffer->slices[i])) {
          gpr_slice_buffer_add(&tmp_buffer,
                               gpr_slice_split_tail(&h->read_buffer->slices[i],
                                                    body_start_offset));
        }
        gpr_slice_buffer_addn(&tmp_buffer, &h->read_buffer->slices[i + 1],
                              h->read_buffer->count - i - 1);
        gpr_slice_buffer_swap(h->read_buffer, &tmp_buffer);
        gpr_slice_buffer_destroy(&tmp_buffer);
        break;
      }
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
    gpr_slice_buffer_reset_and_unref(h->read_buffer);
    grpc_endpoint_read(exec_ctx, h->endpoint, h->read_buffer,
                       &h->response_read_closure);
    return;
  }
  // Make sure we got a 2xx response.
  if (h->http_response.status < 200 || h->http_response.status >= 300) {
    char* msg;
    gpr_asprintf(&msg, "HTTP proxy returned response code %d",
                 h->http_response.status);
    error = GRPC_ERROR_CREATE(msg);
    gpr_free(msg);
  }
 done:
  // Invoke handshake-done callback.
  h->cb(exec_ctx, h->endpoint, h->args, h->read_buffer, h->user_data, error);
}

//
// Public handshaker methods
//

static void http_connect_handshaker_destroy(grpc_exec_ctx* exec_ctx,
                                            grpc_handshaker* handshaker) {
  http_connect_handshaker* h = (http_connect_handshaker*)handshaker;
  gpr_free(h->proxy_server);
  gpr_free(h->server_name);
  gpr_slice_buffer_destroy(&h->write_buffer);
  grpc_http_parser_destroy(&h->http_parser);
  grpc_http_response_destroy(&h->http_response);
  gpr_free(h);
}

static void http_connect_handshaker_shutdown(grpc_exec_ctx* exec_ctx,
                                             grpc_handshaker* handshaker) {
}

// FIXME BEFORE MERGING: apply deadline
static void http_connect_handshaker_do_handshake(
    grpc_exec_ctx* exec_ctx, grpc_handshaker* handshaker,
    grpc_endpoint* endpoint, grpc_channel_args* args,
    gpr_slice_buffer* read_buffer, gpr_timespec deadline,
    grpc_tcp_server_acceptor* acceptor, grpc_handshaker_done_cb cb,
    void* user_data) {
  http_connect_handshaker* h = (http_connect_handshaker*)handshaker;
  // Save state in the handshaker object.
  h->endpoint = endpoint;
  h->args = args;
  h->cb = cb;
  h->user_data = user_data;
  // Initialize fields.
  gpr_slice_buffer_init(&h->write_buffer);
  h->read_buffer = read_buffer;
  grpc_closure_init(&h->request_done_closure, on_write_done, h);
  grpc_closure_init(&h->response_read_closure, on_read_done, h);
  grpc_http_parser_init(&h->http_parser, GRPC_HTTP_RESPONSE,
                        &h->http_response);
  // Send HTTP CONNECT request.
  gpr_log(GPR_INFO, "Connecting to server %s via HTTP proxy %s",
          h->server_name, h->proxy_server);
  grpc_httpcli_request request;
  memset(&request, 0, sizeof(request));
  request.host = h->proxy_server;
  request.http.method = "CONNECT";
  request.http.path = h->server_name;
  request.handshaker = &grpc_httpcli_plaintext;
  gpr_slice request_slice = grpc_httpcli_format_connect_request(&request);
  gpr_slice_buffer_add(&h->write_buffer, request_slice);
  grpc_endpoint_write(exec_ctx, endpoint, &h->write_buffer,
                      &h->request_done_closure);
}

static const struct grpc_handshaker_vtable http_connect_handshaker_vtable = {
    http_connect_handshaker_destroy, http_connect_handshaker_shutdown,
    http_connect_handshaker_do_handshake};

grpc_handshaker* grpc_http_connect_handshaker_create(const char* proxy_server,
                                                     const char* server_name) {
  GPR_ASSERT(proxy_server != NULL);
  GPR_ASSERT(server_name != NULL);
  http_connect_handshaker* handshaker =
      gpr_malloc(sizeof(http_connect_handshaker));
  memset(handshaker, 0, sizeof(*handshaker));
  grpc_handshaker_init(&http_connect_handshaker_vtable, &handshaker->base);
  handshaker->proxy_server = gpr_strdup(proxy_server);
  handshaker->server_name = gpr_strdup(server_name);
  return (grpc_handshaker*)handshaker;
}
