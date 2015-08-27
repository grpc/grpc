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

#include "src/core/iomgr/sockaddr.h"
#include "src/core/httpcli/httpcli.h"

#include <string.h>

#include "src/core/iomgr/endpoint.h"
#include "src/core/iomgr/resolve_address.h"
#include "src/core/iomgr/tcp_client.h"
#include "src/core/httpcli/format_request.h"
#include "src/core/httpcli/parser.h"
#include "src/core/support/string.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

typedef struct {
  gpr_slice request_text;
  grpc_httpcli_parser parser;
  grpc_resolved_addresses *addresses;
  size_t next_address;
  grpc_endpoint *ep;
  char *host;
  gpr_timespec deadline;
  int have_read_byte;
  const grpc_httpcli_handshaker *handshaker;
  grpc_httpcli_response_cb on_response;
  void *user_data;
  grpc_httpcli_context *context;
  grpc_pollset *pollset;
  grpc_iomgr_object iomgr_obj;
  gpr_slice_buffer incoming;
  gpr_slice_buffer outgoing;
  grpc_iomgr_closure on_read;
  grpc_iomgr_closure done_write;
} internal_request;

static grpc_httpcli_get_override g_get_override = NULL;
static grpc_httpcli_post_override g_post_override = NULL;

static void plaintext_handshake(void *arg, grpc_endpoint *endpoint,
                                const char *host,
                                void (*on_done)(void *arg,
                                                grpc_endpoint *endpoint)) {
  on_done(arg, endpoint);
}

const grpc_httpcli_handshaker grpc_httpcli_plaintext = {"http",
                                                        plaintext_handshake};

void grpc_httpcli_context_init(grpc_httpcli_context *context) {
  grpc_pollset_set_init(&context->pollset_set);
}

void grpc_httpcli_context_destroy(grpc_httpcli_context *context) {
  grpc_pollset_set_destroy(&context->pollset_set);
}

static void next_address(internal_request *req);

static void finish(internal_request *req, int success) {
  grpc_pollset_set_del_pollset(&req->context->pollset_set, req->pollset);
  req->on_response(req->user_data, success ? &req->parser.r : NULL);
  grpc_httpcli_parser_destroy(&req->parser);
  if (req->addresses != NULL) {
    grpc_resolved_addresses_destroy(req->addresses);
  }
  if (req->ep != NULL) {
    grpc_endpoint_destroy(req->ep);
  }
  gpr_slice_unref(req->request_text);
  gpr_free(req->host);
  grpc_iomgr_unregister_object(&req->iomgr_obj);
  gpr_slice_buffer_destroy(&req->incoming);
  gpr_slice_buffer_destroy(&req->outgoing);
  gpr_free(req);
}

static void on_read(void *user_data, int success);

static void do_read(internal_request *req) {
  switch (grpc_endpoint_read(req->ep, &req->incoming, &req->on_read)) {
    case GRPC_ENDPOINT_DONE:
      on_read(req, 1);
      break;
    case GRPC_ENDPOINT_PENDING:
      break;
    case GRPC_ENDPOINT_ERROR:
      on_read(req, 0);
      break;
  }
}

static void on_read(void *user_data, int success) {
  internal_request *req = user_data;
  size_t i;

  for (i = 0; i < req->incoming.count; i++) {
    if (GPR_SLICE_LENGTH(req->incoming.slices[i])) {
      req->have_read_byte = 1;
      if (!grpc_httpcli_parser_parse(&req->parser, req->incoming.slices[i])) {
        finish(req, 0);
        return;
      }
    }
  }

  if (success) {
    do_read(req);
  } else if (!req->have_read_byte) {
    next_address(req);
  } else {
    finish(req, grpc_httpcli_parser_eof(&req->parser));
  }
}

static void on_written(internal_request *req) { do_read(req); }

static void done_write(void *arg, int success) {
  internal_request *req = arg;
  if (success) {
    on_written(req);
  } else {
    next_address(req);
  }
}

static void start_write(internal_request *req) {
  gpr_slice_ref(req->request_text);
  gpr_slice_buffer_add(&req->outgoing, req->request_text);
  switch (grpc_endpoint_write(req->ep, &req->outgoing, &req->done_write)) {
    case GRPC_ENDPOINT_DONE:
      on_written(req);
      break;
    case GRPC_ENDPOINT_PENDING:
      break;
    case GRPC_ENDPOINT_ERROR:
      finish(req, 0);
      break;
  }
}

static void on_handshake_done(void *arg, grpc_endpoint *ep) {
  internal_request *req = arg;

  if (!ep) {
    next_address(req);
    return;
  }

  req->ep = ep;
  start_write(req);
}

static void on_connected(void *arg, grpc_endpoint *tcp) {
  internal_request *req = arg;

  if (!tcp) {
    next_address(req);
    return;
  }
  req->handshaker->handshake(req, tcp, req->host, on_handshake_done);
}

static void next_address(internal_request *req) {
  grpc_resolved_address *addr;
  if (req->next_address == req->addresses->naddrs) {
    finish(req, 0);
    return;
  }
  addr = &req->addresses->addrs[req->next_address++];
  grpc_tcp_client_connect(on_connected, req, &req->context->pollset_set,
                          (struct sockaddr *)&addr->addr, addr->len,
                          req->deadline);
}

static void on_resolved(void *arg, grpc_resolved_addresses *addresses) {
  internal_request *req = arg;
  if (!addresses) {
    finish(req, 0);
    return;
  }
  req->addresses = addresses;
  req->next_address = 0;
  next_address(req);
}

void grpc_httpcli_get(grpc_httpcli_context *context, grpc_pollset *pollset,
                      const grpc_httpcli_request *request,
                      gpr_timespec deadline,
                      grpc_httpcli_response_cb on_response, void *user_data) {
  internal_request *req;
  char *name;
  if (g_get_override &&
      g_get_override(request, deadline, on_response, user_data)) {
    return;
  }
  req = gpr_malloc(sizeof(internal_request));
  memset(req, 0, sizeof(*req));
  req->request_text = grpc_httpcli_format_get_request(request);
  grpc_httpcli_parser_init(&req->parser);
  req->on_response = on_response;
  req->user_data = user_data;
  req->deadline = deadline;
  req->handshaker =
      request->handshaker ? request->handshaker : &grpc_httpcli_plaintext;
  req->context = context;
  req->pollset = pollset;
  grpc_iomgr_closure_init(&req->on_read, on_read, req);
  grpc_iomgr_closure_init(&req->done_write, done_write, req);
  gpr_slice_buffer_init(&req->incoming);
  gpr_slice_buffer_init(&req->outgoing);
  gpr_asprintf(&name, "HTTP:GET:%s:%s", request->host, request->path);
  grpc_iomgr_register_object(&req->iomgr_obj, name);
  gpr_free(name);
  req->host = gpr_strdup(request->host);

  grpc_pollset_set_add_pollset(&req->context->pollset_set, req->pollset);
  grpc_resolve_address(request->host, req->handshaker->default_port,
                       on_resolved, req);
}

void grpc_httpcli_post(grpc_httpcli_context *context, grpc_pollset *pollset,
                       const grpc_httpcli_request *request,
                       const char *body_bytes, size_t body_size,
                       gpr_timespec deadline,
                       grpc_httpcli_response_cb on_response, void *user_data) {
  internal_request *req;
  char *name;
  if (g_post_override && g_post_override(request, body_bytes, body_size,
                                         deadline, on_response, user_data)) {
    return;
  }
  req = gpr_malloc(sizeof(internal_request));
  memset(req, 0, sizeof(*req));
  req->request_text =
      grpc_httpcli_format_post_request(request, body_bytes, body_size);
  grpc_httpcli_parser_init(&req->parser);
  req->on_response = on_response;
  req->user_data = user_data;
  req->deadline = deadline;
  req->handshaker =
      request->handshaker ? request->handshaker : &grpc_httpcli_plaintext;
  req->context = context;
  req->pollset = pollset;
  grpc_iomgr_closure_init(&req->on_read, on_read, req);
  grpc_iomgr_closure_init(&req->done_write, done_write, req);
  gpr_slice_buffer_init(&req->incoming);
  gpr_slice_buffer_init(&req->outgoing);
  gpr_asprintf(&name, "HTTP:POST:%s:%s", request->host, request->path);
  grpc_iomgr_register_object(&req->iomgr_obj, name);
  gpr_free(name);
  req->host = gpr_strdup(request->host);

  grpc_pollset_set_add_pollset(&req->context->pollset_set, req->pollset);
  grpc_resolve_address(request->host, req->handshaker->default_port,
                       on_resolved, req);
}

void grpc_httpcli_set_override(grpc_httpcli_get_override get,
                               grpc_httpcli_post_override post) {
  g_get_override = get;
  g_post_override = post;
}
