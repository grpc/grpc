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
  grpc_closure on_read;
  grpc_closure done_write;
  grpc_closure connected;
} internal_request;

static grpc_httpcli_get_override g_get_override = NULL;
static grpc_httpcli_post_override g_post_override = NULL;

static void plaintext_handshake(void *arg, grpc_endpoint *endpoint,
                                const char *host,
                                void (*on_done)(void *arg,
                                                grpc_endpoint *endpoint,
                                                grpc_call_list *call_list),
                                grpc_call_list *call_list) {
  on_done(arg, endpoint, call_list);
}

const grpc_httpcli_handshaker grpc_httpcli_plaintext = {"http",
                                                        plaintext_handshake};

void grpc_httpcli_context_init(grpc_httpcli_context *context) {
  grpc_pollset_set_init(&context->pollset_set);
}

void grpc_httpcli_context_destroy(grpc_httpcli_context *context) {
  grpc_pollset_set_destroy(&context->pollset_set);
}

static void next_address(internal_request *req, grpc_call_list *call_list);

static void finish(internal_request *req, int success,
                   grpc_call_list *call_list) {
  grpc_pollset_set_del_pollset(&req->context->pollset_set, req->pollset,
                               call_list);
  req->on_response(req->user_data, success ? &req->parser.r : NULL, call_list);
  grpc_httpcli_parser_destroy(&req->parser);
  if (req->addresses != NULL) {
    grpc_resolved_addresses_destroy(req->addresses);
  }
  if (req->ep != NULL) {
    grpc_endpoint_destroy(req->ep, call_list);
  }
  gpr_slice_unref(req->request_text);
  gpr_free(req->host);
  grpc_iomgr_unregister_object(&req->iomgr_obj);
  gpr_slice_buffer_destroy(&req->incoming);
  gpr_slice_buffer_destroy(&req->outgoing);
  gpr_free(req);
}

static void on_read(void *user_data, int success, grpc_call_list *call_list);

static void do_read(internal_request *req, grpc_call_list *call_list) {
  grpc_endpoint_read(req->ep, &req->incoming, &req->on_read, call_list);
}

static void on_read(void *user_data, int success, grpc_call_list *call_list) {
  internal_request *req = user_data;
  size_t i;

  for (i = 0; i < req->incoming.count; i++) {
    if (GPR_SLICE_LENGTH(req->incoming.slices[i])) {
      req->have_read_byte = 1;
      if (!grpc_httpcli_parser_parse(&req->parser, req->incoming.slices[i])) {
        finish(req, 0, call_list);
        return;
      }
    }
  }

  if (success) {
    do_read(req, call_list);
  } else if (!req->have_read_byte) {
    next_address(req, call_list);
  } else {
    finish(req, grpc_httpcli_parser_eof(&req->parser), call_list);
  }
}

static void on_written(internal_request *req, grpc_call_list *call_list) {
  do_read(req, call_list);
}

static void done_write(void *arg, int success, grpc_call_list *call_list) {
  internal_request *req = arg;
  if (success) {
    on_written(req, call_list);
  } else {
    next_address(req, call_list);
  }
}

static void start_write(internal_request *req, grpc_call_list *call_list) {
  gpr_slice_ref(req->request_text);
  gpr_slice_buffer_add(&req->outgoing, req->request_text);
  grpc_endpoint_write(req->ep, &req->outgoing, &req->done_write, call_list);
}

static void on_handshake_done(void *arg, grpc_endpoint *ep,
                              grpc_call_list *call_list) {
  internal_request *req = arg;

  if (!ep) {
    next_address(req, call_list);
    return;
  }

  req->ep = ep;
  start_write(req, call_list);
}

static void on_connected(void *arg, int success, grpc_call_list *call_list) {
  internal_request *req = arg;

  if (!req->ep) {
    next_address(req, call_list);
    return;
  }
  req->handshaker->handshake(req, req->ep, req->host, on_handshake_done,
                             call_list);
}

static void next_address(internal_request *req, grpc_call_list *call_list) {
  grpc_resolved_address *addr;
  if (req->next_address == req->addresses->naddrs) {
    finish(req, 0, call_list);
    return;
  }
  addr = &req->addresses->addrs[req->next_address++];
  grpc_closure_init(&req->connected, on_connected, req);
  grpc_tcp_client_connect(&req->connected, &req->ep, &req->context->pollset_set,
                          (struct sockaddr *)&addr->addr, addr->len,
                          req->deadline, call_list);
}

static void on_resolved(void *arg, grpc_resolved_addresses *addresses,
                        grpc_call_list *call_list) {
  internal_request *req = arg;
  if (!addresses) {
    finish(req, 0, call_list);
    return;
  }
  req->addresses = addresses;
  req->next_address = 0;
  next_address(req, call_list);
}

static void internal_request_begin(
    grpc_httpcli_context *context, grpc_pollset *pollset,
    const grpc_httpcli_request *request, gpr_timespec deadline,
    grpc_httpcli_response_cb on_response, void *user_data, const char *name,
    gpr_slice request_text, grpc_call_list *call_list) {
  internal_request *req = gpr_malloc(sizeof(internal_request));
  memset(req, 0, sizeof(*req));
  req->request_text = request_text;
  grpc_httpcli_parser_init(&req->parser);
  req->on_response = on_response;
  req->user_data = user_data;
  req->deadline = deadline;
  req->handshaker =
      request->handshaker ? request->handshaker : &grpc_httpcli_plaintext;
  req->context = context;
  req->pollset = pollset;
  grpc_closure_init(&req->on_read, on_read, req);
  grpc_closure_init(&req->done_write, done_write, req);
  gpr_slice_buffer_init(&req->incoming);
  gpr_slice_buffer_init(&req->outgoing);
  grpc_iomgr_register_object(&req->iomgr_obj, name);
  req->host = gpr_strdup(request->host);

  grpc_pollset_set_add_pollset(&req->context->pollset_set, req->pollset,
                               call_list);
  grpc_resolve_address(request->host, req->handshaker->default_port,
                       on_resolved, req);
}

void grpc_httpcli_get(grpc_httpcli_context *context, grpc_pollset *pollset,
                      const grpc_httpcli_request *request,
                      gpr_timespec deadline,
                      grpc_httpcli_response_cb on_response, void *user_data,
                      grpc_call_list *call_list) {
  char *name;
  if (g_get_override &&
      g_get_override(request, deadline, on_response, user_data, call_list)) {
    return;
  }
  gpr_asprintf(&name, "HTTP:GET:%s:%s", request->host, request->path);
  internal_request_begin(context, pollset, request, deadline, on_response,
                         user_data, name,
                         grpc_httpcli_format_get_request(request), call_list);
  gpr_free(name);
}

void grpc_httpcli_post(grpc_httpcli_context *context, grpc_pollset *pollset,
                       const grpc_httpcli_request *request,
                       const char *body_bytes, size_t body_size,
                       gpr_timespec deadline,
                       grpc_httpcli_response_cb on_response, void *user_data,
                       grpc_call_list *call_list) {
  char *name;
  if (g_post_override &&
      g_post_override(request, body_bytes, body_size, deadline, on_response,
                      user_data, call_list)) {
    return;
  }
  gpr_asprintf(&name, "HTTP:POST:%s:%s", request->host, request->path);
  internal_request_begin(
      context, pollset, request, deadline, on_response, user_data, name,
      grpc_httpcli_format_post_request(request, body_bytes, body_size),
      call_list);
  gpr_free(name);
}

void grpc_httpcli_set_override(grpc_httpcli_get_override get,
                               grpc_httpcli_post_override post) {
  g_get_override = get;
  g_post_override = post;
}
