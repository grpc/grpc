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

#include "src/core/lib/http/httpcli.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/lib/http/format_request.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/support/string.h"

typedef struct {
  grpc_slice request_text;
  grpc_http_parser parser;
  grpc_resolved_addresses *addresses;
  size_t next_address;
  grpc_endpoint *ep;
  char *host;
  char *ssl_host_override;
  gpr_timespec deadline;
  int have_read_byte;
  const grpc_httpcli_handshaker *handshaker;
  grpc_closure *on_done;
  grpc_httpcli_context *context;
  grpc_polling_entity *pollent;
  grpc_iomgr_object iomgr_obj;
  grpc_slice_buffer incoming;
  grpc_slice_buffer outgoing;
  grpc_closure on_read;
  grpc_closure done_write;
  grpc_closure connected;
  grpc_error *overall_error;
  grpc_resource_quota *resource_quota;
} internal_request;

static grpc_httpcli_get_override g_get_override = NULL;
static grpc_httpcli_post_override g_post_override = NULL;

static void plaintext_handshake(grpc_exec_ctx *exec_ctx, void *arg,
                                grpc_endpoint *endpoint, const char *host,
                                gpr_timespec deadline,
                                void (*on_done)(grpc_exec_ctx *exec_ctx,
                                                void *arg,
                                                grpc_endpoint *endpoint)) {
  on_done(exec_ctx, arg, endpoint);
}

const grpc_httpcli_handshaker grpc_httpcli_plaintext = {"http",
                                                        plaintext_handshake};

void grpc_httpcli_context_init(grpc_httpcli_context *context) {
  context->pollset_set = grpc_pollset_set_create();
}

void grpc_httpcli_context_destroy(grpc_exec_ctx *exec_ctx,
                                  grpc_httpcli_context *context) {
  grpc_pollset_set_destroy(exec_ctx, context->pollset_set);
}

static void next_address(grpc_exec_ctx *exec_ctx, internal_request *req,
                         grpc_error *due_to_error);

static void finish(grpc_exec_ctx *exec_ctx, internal_request *req,
                   grpc_error *error) {
  grpc_polling_entity_del_from_pollset_set(exec_ctx, req->pollent,
                                           req->context->pollset_set);
  grpc_closure_sched(exec_ctx, req->on_done, error);
  grpc_http_parser_destroy(&req->parser);
  if (req->addresses != NULL) {
    grpc_resolved_addresses_destroy(req->addresses);
  }
  if (req->ep != NULL) {
    grpc_endpoint_destroy(exec_ctx, req->ep);
  }
  grpc_slice_unref_internal(exec_ctx, req->request_text);
  gpr_free(req->host);
  gpr_free(req->ssl_host_override);
  grpc_iomgr_unregister_object(&req->iomgr_obj);
  grpc_slice_buffer_destroy_internal(exec_ctx, &req->incoming);
  grpc_slice_buffer_destroy_internal(exec_ctx, &req->outgoing);
  GRPC_ERROR_UNREF(req->overall_error);
  grpc_resource_quota_unref_internal(exec_ctx, req->resource_quota);
  gpr_free(req);
}

static void append_error(internal_request *req, grpc_error *error) {
  if (req->overall_error == GRPC_ERROR_NONE) {
    req->overall_error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Failed HTTP/1 client request");
  }
  grpc_resolved_address *addr = &req->addresses->addrs[req->next_address - 1];
  char *addr_text = grpc_sockaddr_to_uri(addr);
  req->overall_error = grpc_error_add_child(
      req->overall_error,
      grpc_error_set_str(error, GRPC_ERROR_STR_TARGET_ADDRESS,
                         grpc_slice_from_copied_string(addr_text)));
  gpr_free(addr_text);
}

static void do_read(grpc_exec_ctx *exec_ctx, internal_request *req) {
  grpc_endpoint_read(exec_ctx, req->ep, &req->incoming, &req->on_read);
}

static void on_read(grpc_exec_ctx *exec_ctx, void *user_data,
                    grpc_error *error) {
  internal_request *req = user_data;
  size_t i;

  for (i = 0; i < req->incoming.count; i++) {
    if (GRPC_SLICE_LENGTH(req->incoming.slices[i])) {
      req->have_read_byte = 1;
      grpc_error *err =
          grpc_http_parser_parse(&req->parser, req->incoming.slices[i], NULL);
      if (err != GRPC_ERROR_NONE) {
        finish(exec_ctx, req, err);
        return;
      }
    }
  }

  if (error == GRPC_ERROR_NONE) {
    do_read(exec_ctx, req);
  } else if (!req->have_read_byte) {
    next_address(exec_ctx, req, GRPC_ERROR_REF(error));
  } else {
    finish(exec_ctx, req, grpc_http_parser_eof(&req->parser));
  }
}

static void on_written(grpc_exec_ctx *exec_ctx, internal_request *req) {
  do_read(exec_ctx, req);
}

static void done_write(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  internal_request *req = arg;
  if (error == GRPC_ERROR_NONE) {
    on_written(exec_ctx, req);
  } else {
    next_address(exec_ctx, req, GRPC_ERROR_REF(error));
  }
}

static void start_write(grpc_exec_ctx *exec_ctx, internal_request *req) {
  grpc_slice_ref_internal(req->request_text);
  grpc_slice_buffer_add(&req->outgoing, req->request_text);
  grpc_endpoint_write(exec_ctx, req->ep, &req->outgoing, &req->done_write);
}

static void on_handshake_done(grpc_exec_ctx *exec_ctx, void *arg,
                              grpc_endpoint *ep) {
  internal_request *req = arg;

  if (!ep) {
    next_address(exec_ctx, req, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                    "Unexplained handshake failure"));
    return;
  }

  req->ep = ep;
  start_write(exec_ctx, req);
}

static void on_connected(grpc_exec_ctx *exec_ctx, void *arg,
                         grpc_error *error) {
  internal_request *req = arg;

  if (!req->ep) {
    next_address(exec_ctx, req, GRPC_ERROR_REF(error));
    return;
  }
  req->handshaker->handshake(
      exec_ctx, req, req->ep,
      req->ssl_host_override ? req->ssl_host_override : req->host,
      req->deadline, on_handshake_done);
}

static void next_address(grpc_exec_ctx *exec_ctx, internal_request *req,
                         grpc_error *error) {
  grpc_resolved_address *addr;
  if (error != GRPC_ERROR_NONE) {
    append_error(req, error);
  }
  if (req->next_address == req->addresses->naddrs) {
    finish(exec_ctx, req,
           GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
               "Failed HTTP requests to all targets", &req->overall_error, 1));
    return;
  }
  addr = &req->addresses->addrs[req->next_address++];
  grpc_closure_init(&req->connected, on_connected, req,
                    grpc_schedule_on_exec_ctx);
  grpc_arg arg;
  arg.key = GRPC_ARG_RESOURCE_QUOTA;
  arg.type = GRPC_ARG_POINTER;
  arg.value.pointer.p = req->resource_quota;
  arg.value.pointer.vtable = grpc_resource_quota_arg_vtable();
  grpc_channel_args args = {1, &arg};
  grpc_tcp_client_connect(exec_ctx, &req->connected, &req->ep,
                          req->context->pollset_set, &args, addr,
                          req->deadline);
}

static void on_resolved(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  internal_request *req = arg;
  if (error != GRPC_ERROR_NONE) {
    finish(exec_ctx, req, error);
    return;
  }
  req->next_address = 0;
  next_address(exec_ctx, req, GRPC_ERROR_NONE);
}

static void internal_request_begin(grpc_exec_ctx *exec_ctx,
                                   grpc_httpcli_context *context,
                                   grpc_polling_entity *pollent,
                                   grpc_resource_quota *resource_quota,
                                   const grpc_httpcli_request *request,
                                   gpr_timespec deadline, grpc_closure *on_done,
                                   grpc_httpcli_response *response,
                                   const char *name, grpc_slice request_text) {
  internal_request *req = gpr_malloc(sizeof(internal_request));
  memset(req, 0, sizeof(*req));
  req->request_text = request_text;
  grpc_http_parser_init(&req->parser, GRPC_HTTP_RESPONSE, response);
  req->on_done = on_done;
  req->deadline = deadline;
  req->handshaker =
      request->handshaker ? request->handshaker : &grpc_httpcli_plaintext;
  req->context = context;
  req->pollent = pollent;
  req->overall_error = GRPC_ERROR_NONE;
  req->resource_quota = grpc_resource_quota_ref_internal(resource_quota);
  grpc_closure_init(&req->on_read, on_read, req, grpc_schedule_on_exec_ctx);
  grpc_closure_init(&req->done_write, done_write, req,
                    grpc_schedule_on_exec_ctx);
  grpc_slice_buffer_init(&req->incoming);
  grpc_slice_buffer_init(&req->outgoing);
  grpc_iomgr_register_object(&req->iomgr_obj, name);
  req->host = gpr_strdup(request->host);
  req->ssl_host_override = gpr_strdup(request->ssl_host_override);

  GPR_ASSERT(pollent);
  grpc_polling_entity_add_to_pollset_set(exec_ctx, req->pollent,
                                         req->context->pollset_set);
  grpc_resolve_address(
      exec_ctx, request->host, req->handshaker->default_port,
      req->context->pollset_set,
      grpc_closure_create(on_resolved, req, grpc_schedule_on_exec_ctx),
      &req->addresses);
}

void grpc_httpcli_get(grpc_exec_ctx *exec_ctx, grpc_httpcli_context *context,
                      grpc_polling_entity *pollent,
                      grpc_resource_quota *resource_quota,
                      const grpc_httpcli_request *request,
                      gpr_timespec deadline, grpc_closure *on_done,
                      grpc_httpcli_response *response) {
  char *name;
  if (g_get_override &&
      g_get_override(exec_ctx, request, deadline, on_done, response)) {
    return;
  }
  gpr_asprintf(&name, "HTTP:GET:%s:%s", request->host, request->http.path);
  internal_request_begin(exec_ctx, context, pollent, resource_quota, request,
                         deadline, on_done, response, name,
                         grpc_httpcli_format_get_request(request));
  gpr_free(name);
}

void grpc_httpcli_post(grpc_exec_ctx *exec_ctx, grpc_httpcli_context *context,
                       grpc_polling_entity *pollent,
                       grpc_resource_quota *resource_quota,
                       const grpc_httpcli_request *request,
                       const char *body_bytes, size_t body_size,
                       gpr_timespec deadline, grpc_closure *on_done,
                       grpc_httpcli_response *response) {
  char *name;
  if (g_post_override &&
      g_post_override(exec_ctx, request, body_bytes, body_size, deadline,
                      on_done, response)) {
    return;
  }
  gpr_asprintf(&name, "HTTP:POST:%s:%s", request->host, request->http.path);
  internal_request_begin(
      exec_ctx, context, pollent, resource_quota, request, deadline, on_done,
      response, name,
      grpc_httpcli_format_post_request(request, body_bytes, body_size));
  gpr_free(name);
}

void grpc_httpcli_set_override(grpc_httpcli_get_override get,
                               grpc_httpcli_post_override post) {
  g_get_override = get;
  g_post_override = post;
}
