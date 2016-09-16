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

#include "src/core/ext/client_config/http_connect_handshaker.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice_buffer.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/client_config/uri_parser.h"
#include "src/core/lib/http/format_request.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/support/env.h"

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
  gpr_slice_buffer* read_buffer;  // Ownership passes through this object.
  grpc_closure request_done_closure;
  grpc_closure response_read_closure;
  grpc_http_parser http_parser;
  grpc_http_response http_response;
  grpc_timer timeout_timer;

  gpr_refcount refcount;
} http_connect_handshaker;

// Unref and clean up handshaker.
static void http_connect_handshaker_unref(http_connect_handshaker* handshaker) {
  if (gpr_unref(&handshaker->refcount)) {
    gpr_free(handshaker->proxy_server);
    gpr_free(handshaker->server_name);
    gpr_slice_buffer_destroy(&handshaker->write_buffer);
    grpc_http_parser_destroy(&handshaker->http_parser);
    grpc_http_response_destroy(&handshaker->http_response);
    gpr_free(handshaker);
  }
}

// Callback invoked when deadline is exceeded.
static void on_timeout(grpc_exec_ctx* exec_ctx, void* arg, grpc_error* error) {
  http_connect_handshaker* handshaker = arg;
  if (error == GRPC_ERROR_NONE) {  // Timer fired, rather than being cancelled.
    grpc_endpoint_shutdown(exec_ctx, handshaker->endpoint);
  }
  http_connect_handshaker_unref(handshaker);
}

// Callback invoked when finished writing HTTP CONNECT request.
static void on_write_done(grpc_exec_ctx* exec_ctx, void* arg,
                          grpc_error* error) {
  http_connect_handshaker* handshaker = arg;
  if (error != GRPC_ERROR_NONE) {
    // If the write failed, invoke the callback immediately with the error.
    handshaker->cb(exec_ctx, handshaker->endpoint, handshaker->args,
                   handshaker->read_buffer, handshaker->user_data,
                   GRPC_ERROR_REF(error));
  } else {
    // Otherwise, read the response.
    grpc_endpoint_read(exec_ctx, handshaker->endpoint, handshaker->read_buffer,
                       &handshaker->response_read_closure);
  }
}

// Callback invoked for reading HTTP CONNECT response.
static void on_read_done(grpc_exec_ctx* exec_ctx, void* arg,
                         grpc_error* error) {
  http_connect_handshaker* handshaker = arg;
  if (error != GRPC_ERROR_NONE) {
    GRPC_ERROR_REF(error);  // Take ref to pass to the handshake-done callback.
    goto done;
  }
  // Add buffer to parser.
  for (size_t i = 0; i < handshaker->read_buffer->count; ++i) {
    if (GPR_SLICE_LENGTH(handshaker->read_buffer->slices[i]) > 0) {
      size_t body_start_offset = 0;
      error = grpc_http_parser_parse(&handshaker->http_parser,
                                     handshaker->read_buffer->slices[i],
                                     &body_start_offset);
      if (error != GRPC_ERROR_NONE) goto done;
      if (handshaker->http_parser.state == GRPC_HTTP_BODY) {
        // We've gotten back a successul response, so stop the timeout timer.
        grpc_timer_cancel(exec_ctx, &handshaker->timeout_timer);
        // Remove the data we've already read from the read buffer,
        // leaving only the leftover bytes (if any).
        gpr_slice_buffer tmp_buffer;
        gpr_slice_buffer_init(&tmp_buffer);
        if (body_start_offset <
            GPR_SLICE_LENGTH(handshaker->read_buffer->slices[i])) {
          gpr_slice_buffer_add(
              &tmp_buffer,
              gpr_slice_split_tail(&handshaker->read_buffer->slices[i],
                                   body_start_offset));
        }
        gpr_slice_buffer_addn(&tmp_buffer,
                              &handshaker->read_buffer->slices[i + 1],
                              handshaker->read_buffer->count - i - 1);
        gpr_slice_buffer_swap(handshaker->read_buffer, &tmp_buffer);
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
  if (handshaker->http_parser.state != GRPC_HTTP_BODY) {
    gpr_slice_buffer_reset_and_unref(handshaker->read_buffer);
    grpc_endpoint_read(exec_ctx, handshaker->endpoint, handshaker->read_buffer,
                       &handshaker->response_read_closure);
    return;
  }
  // Make sure we got a 2xx response.
  if (handshaker->http_response.status < 200 ||
      handshaker->http_response.status >= 300) {
    char* msg;
    gpr_asprintf(&msg, "HTTP proxy returned response code %d",
                 handshaker->http_response.status);
    error = GRPC_ERROR_CREATE(msg);
    gpr_free(msg);
  }
done:
  // Invoke handshake-done callback.
  handshaker->cb(exec_ctx, handshaker->endpoint, handshaker->args,
                 handshaker->read_buffer, handshaker->user_data, error);
}

//
// Public handshaker methods
//

static void http_connect_handshaker_destroy(grpc_exec_ctx* exec_ctx,
                                            grpc_handshaker* handshaker_in) {
  http_connect_handshaker* handshaker = (http_connect_handshaker*)handshaker_in;
  http_connect_handshaker_unref(handshaker);
}

static void http_connect_handshaker_shutdown(grpc_exec_ctx* exec_ctx,
                                             grpc_handshaker* handshaker) {}

static void http_connect_handshaker_do_handshake(
    grpc_exec_ctx* exec_ctx, grpc_handshaker* handshaker_in,
    grpc_endpoint* endpoint, grpc_channel_args* args,
    gpr_slice_buffer* read_buffer, gpr_timespec deadline,
    grpc_tcp_server_acceptor* acceptor, grpc_handshaker_done_cb cb,
    void* user_data) {
  http_connect_handshaker* handshaker = (http_connect_handshaker*)handshaker_in;
  // Save state in the handshaker object.
  handshaker->endpoint = endpoint;
  handshaker->args = args;
  handshaker->cb = cb;
  handshaker->user_data = user_data;
  handshaker->read_buffer = read_buffer;
  // Send HTTP CONNECT request.
  gpr_log(GPR_INFO, "Connecting to server %s via HTTP proxy %s",
          handshaker->server_name, handshaker->proxy_server);
  grpc_httpcli_request request;
  memset(&request, 0, sizeof(request));
  request.host = handshaker->proxy_server;
  request.http.method = "CONNECT";
  request.http.path = handshaker->server_name;
  request.handshaker = &grpc_httpcli_plaintext;
  gpr_slice request_slice = grpc_httpcli_format_connect_request(&request);
  gpr_slice_buffer_add(&handshaker->write_buffer, request_slice);
  grpc_endpoint_write(exec_ctx, endpoint, &handshaker->write_buffer,
                      &handshaker->request_done_closure);
  // Set timeout timer.  The timer gets a reference to the handshaker.
  gpr_ref(&handshaker->refcount);
  grpc_timer_init(exec_ctx, &handshaker->timeout_timer,
                  gpr_convert_clock_type(deadline, GPR_CLOCK_MONOTONIC),
                  on_timeout, handshaker, gpr_now(GPR_CLOCK_MONOTONIC));
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
  gpr_slice_buffer_init(&handshaker->write_buffer);
  grpc_closure_init(&handshaker->request_done_closure, on_write_done,
                    handshaker);
  grpc_closure_init(&handshaker->response_read_closure, on_read_done,
                    handshaker);
  grpc_http_parser_init(&handshaker->http_parser, GRPC_HTTP_RESPONSE,
                        &handshaker->http_response);
  gpr_ref_init(&handshaker->refcount, 1);
  return &handshaker->base;
}

char* grpc_get_http_proxy_server() {
  char* uri_str = gpr_getenv("http_proxy");
  if (uri_str == NULL) return NULL;
  grpc_uri* uri = grpc_uri_parse(uri_str, false /* suppress_errors */);
  char* proxy_name = NULL;
  if (uri == NULL || uri->authority == NULL) {
    gpr_log(GPR_ERROR, "cannot parse value of 'http_proxy' env var");
    goto done;
  }
  if (strcmp(uri->scheme, "http") != 0) {
    gpr_log(GPR_ERROR, "'%s' scheme not supported in proxy URI", uri->scheme);
    goto done;
  }
  if (strchr(uri->authority, '@') != NULL) {
    gpr_log(GPR_ERROR, "userinfo not supported in proxy URI");
    goto done;
  }
  proxy_name = gpr_strdup(uri->authority);
done:
  gpr_free(uri_str);
  grpc_uri_destroy(uri);
  return proxy_name;
}
