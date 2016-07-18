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

#include "src/core/ext/client_config/http_connect_handshaker.h"

typedef struct http_connect_handshaker {
  // Base class.  Must be first.
  grpc_handshaker base;

  // State saved while performing the handshake.
  grpc_endpoint* endpoint;
  grpc_channel_args* args;
  grpc_handshaker_done_cb cb;
  void* user_data;

  // Objects for processing the HTTP CONNECT request and response.
  grpc_slice_buffer request_buffer;
  grpc_closure request_done_closure;
  grpc_slice_buffer response_buffer;
  grpc_closure response_read_closure;
} http_connect_handshaker;

static void http_connect_handshaker_destroy(grpc_exec_ctx* exec_ctx,
                                            grpc_handshaker* handshaker) {
}

static void http_connect_handshaker_shutdown(grpc_exec_ctx* exec_ctx,
                                             grpc_handshaker* handshaker) {
}

// Callback invoked for reading HTTP CONNECT response.
static void on_read_done(grpc_exec_ctx* exec_ctx, void* arg,
                         grpc_error* error) {
  http_connect_handshaker* h = arg;
// FIXME: process response; on failure, figure out how to abort

  // Invoke handshake-done callback.
  h->cb(exec_ctx, h->endpoint, h->args, h->user_data);
}

// Callback invoked when finished writing HTTP CONNECT request.
static void on_write_done(grpc_exec_ctx* exec_ctx, void* arg,
                          grpc_error* error) {
  http_connect_handshaker* h = arg;
  // Read HTTP CONNECT response.
  gpr_slice_buffer_init(&h->response_buffer);
  grpc_closure_init(&h->response_read_closure, on_read_done, h);
  grpc_endpoint_read(exec_ctx, h->endpoint, &h->response_buffer,
                     &h->response_read_closure);
}

static void http_connect_handshaker_do_handshake(grpc_exec_ctx* exec_ctx,
                                                 grpc_handshaker* handshaker,
                                                 grpc_endpoint* endpoint,
                                                 grpc_channel_args* args,
                                                 gpr_timespec deadline,
                                                 grpc_handshaker_done_cb cb,
                                                 void* user_data) {
  http_connect_handshaker* h = (http_connect_handshaker*)handshaker;
  // Save state in the handshaker object.
  h->endpoint = endpoint;
  h->args = args;
  h->cb = cb;
  h->user_data = user_data;
  // Send HTTP CONNECT request.
  gpr_slice_buffer_init(&h->request_buffer);
  gpr_slice_buffer_add(&h->request_buffer, "HTTP CONNECT ");
// FIXME: get server name from somewhere...
  gpr_slice_buffer_add(&h->request_buffer, WHEE);
// FIXME: add headers as needed?
  gpr_slice_buffer_add(&h->request_buffer, "\n\n");
  grpc_closure_init(&h->request_done_closure, on_write_done, h);
  grpc_endpoint_write(exec_ctx, endpoint, &h->request_buffer,
                      &h->request_done_closure);
}

static const struct grpc_handshaker_vtable http_connect_handshaker_vtable = {
    http_connect_handshaker_destroy, http_connect_handshaker_shutdown,
    http_connect_handshaker_do_handshake};

grpc_handshaker* grpc_http_connect_handshaker_create() {
  http_connect_handshaker* handshaker =
      gpr_malloc(sizeof(http_connect_handshaker));
  memset(handshaker, 0, sizeof(*handshaker));
  grpc_handshaker_init(http_connect_handshaker_vtable, &handshaker->base);
  return (grpc_handshaker*)handshaker;
}
