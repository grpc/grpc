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

#include "src/core/lib/http/httpcli.h"

#include <string.h>

#include <string>

#include "absl/strings/str_format.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/http/format_request.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/slice/slice_internal.h"

namespace {

struct InternalRequest {
  InternalRequest(const grpc_slice& request_text,
                  grpc_httpcli_response* response,
                  grpc_resource_quota* resource_quota, const char* host,
                  const char* ssl_host_override, grpc_millis deadline,
                  const grpc_httpcli_handshaker* handshaker,
                  grpc_closure* on_done, grpc_httpcli_context* context,
                  grpc_polling_entity* pollent, const char* name)
      : request_text(request_text),
        resource_quota(resource_quota),
        host(gpr_strdup(host)),
        ssl_host_override(gpr_strdup(ssl_host_override)),
        deadline(deadline),
        handshaker(handshaker),
        on_done(on_done),
        context(context),
        pollent(pollent) {
    grpc_http_parser_init(&parser, GRPC_HTTP_RESPONSE, response);
    grpc_slice_buffer_init(&incoming);
    grpc_slice_buffer_init(&outgoing);
    grpc_iomgr_register_object(&iomgr_obj, name);
  }

  ~InternalRequest() {
    grpc_http_parser_destroy(&parser);
    if (addresses != nullptr) {
      grpc_resolved_addresses_destroy(addresses);
    }
    if (ep != nullptr) {
      grpc_endpoint_destroy(ep);
    }
    grpc_slice_unref_internal(request_text);
    gpr_free(host);
    gpr_free(ssl_host_override);
    grpc_iomgr_unregister_object(&iomgr_obj);
    grpc_slice_buffer_destroy_internal(&incoming);
    grpc_slice_buffer_destroy_internal(&outgoing);
    GRPC_ERROR_UNREF(overall_error);
    grpc_resource_quota_unref_internal(resource_quota);
  }

  grpc_slice request_text;
  grpc_http_parser parser;
  grpc_resolved_addresses* addresses = nullptr;
  size_t next_address = 0;
  grpc_endpoint* ep = nullptr;
  grpc_resource_quota* resource_quota;
  char* host;
  char* ssl_host_override;
  grpc_millis deadline;
  int have_read_byte = 0;
  const grpc_httpcli_handshaker* handshaker;
  grpc_closure* on_done;
  grpc_httpcli_context* context;
  grpc_polling_entity* pollent;
  grpc_iomgr_object iomgr_obj;
  grpc_slice_buffer incoming;
  grpc_slice_buffer outgoing;
  grpc_closure on_read;
  grpc_closure done_write;
  grpc_closure connected;
  grpc_error_handle overall_error = GRPC_ERROR_NONE;
};

}  // namespace

static grpc_httpcli_get_override g_get_override = nullptr;
static grpc_httpcli_post_override g_post_override = nullptr;

static void plaintext_handshake(void* arg, grpc_endpoint* endpoint,
                                const char* /*host*/, grpc_millis /*deadline*/,
                                void (*on_done)(void* arg,
                                                grpc_endpoint* endpoint)) {
  on_done(arg, endpoint);
}

const grpc_httpcli_handshaker grpc_httpcli_plaintext = {"http",
                                                        plaintext_handshake};

void grpc_httpcli_context_init(grpc_httpcli_context* context) {
  context->pollset_set = grpc_pollset_set_create();
}

void grpc_httpcli_context_destroy(grpc_httpcli_context* context) {
  grpc_pollset_set_destroy(context->pollset_set);
}

static void next_address(InternalRequest* req, grpc_error_handle due_to_error);

static void finish(InternalRequest* req, grpc_error_handle error) {
  grpc_polling_entity_del_from_pollset_set(req->pollent,
                                           req->context->pollset_set);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, req->on_done, error);
  delete req;
}

static void append_error(InternalRequest* req, grpc_error_handle error) {
  if (req->overall_error == GRPC_ERROR_NONE) {
    req->overall_error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Failed HTTP/1 client request");
  }
  grpc_resolved_address* addr = &req->addresses->addrs[req->next_address - 1];
  std::string addr_text = grpc_sockaddr_to_uri(addr);
  req->overall_error = grpc_error_add_child(
      req->overall_error,
      grpc_error_set_str(error, GRPC_ERROR_STR_TARGET_ADDRESS, addr_text));
}

static void do_read(InternalRequest* req) {
  grpc_endpoint_read(req->ep, &req->incoming, &req->on_read, /*urgent=*/true);
}

static void on_read(void* user_data, grpc_error_handle error) {
  InternalRequest* req = static_cast<InternalRequest*>(user_data);
  size_t i;

  for (i = 0; i < req->incoming.count; i++) {
    if (GRPC_SLICE_LENGTH(req->incoming.slices[i])) {
      req->have_read_byte = 1;
      grpc_error_handle err = grpc_http_parser_parse(
          &req->parser, req->incoming.slices[i], nullptr);
      if (err != GRPC_ERROR_NONE) {
        finish(req, err);
        return;
      }
    }
  }

  if (error == GRPC_ERROR_NONE) {
    do_read(req);
  } else if (!req->have_read_byte) {
    next_address(req, GRPC_ERROR_REF(error));
  } else {
    finish(req, grpc_http_parser_eof(&req->parser));
  }
}

static void on_written(InternalRequest* req) { do_read(req); }

static void done_write(void* arg, grpc_error_handle error) {
  InternalRequest* req = static_cast<InternalRequest*>(arg);
  if (error == GRPC_ERROR_NONE) {
    on_written(req);
  } else {
    next_address(req, GRPC_ERROR_REF(error));
  }
}

static void start_write(InternalRequest* req) {
  grpc_slice_ref_internal(req->request_text);
  grpc_slice_buffer_add(&req->outgoing, req->request_text);
  grpc_endpoint_write(req->ep, &req->outgoing, &req->done_write, nullptr);
}

static void on_handshake_done(void* arg, grpc_endpoint* ep) {
  InternalRequest* req = static_cast<InternalRequest*>(arg);

  if (!ep) {
    next_address(req, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                          "Unexplained handshake failure"));
    return;
  }

  req->ep = ep;
  start_write(req);
}

static void on_connected(void* arg, grpc_error_handle error) {
  InternalRequest* req = static_cast<InternalRequest*>(arg);

  if (!req->ep) {
    next_address(req, GRPC_ERROR_REF(error));
    return;
  }
  req->handshaker->handshake(
      req, req->ep, req->ssl_host_override ? req->ssl_host_override : req->host,
      req->deadline, on_handshake_done);
}

static void next_address(InternalRequest* req, grpc_error_handle error) {
  grpc_resolved_address* addr;
  if (error != GRPC_ERROR_NONE) {
    append_error(req, error);
  }
  if (req->next_address == req->addresses->naddrs) {
    finish(req,
           GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
               "Failed HTTP requests to all targets", &req->overall_error, 1));
    return;
  }
  addr = &req->addresses->addrs[req->next_address++];
  GRPC_CLOSURE_INIT(&req->connected, on_connected, req,
                    grpc_schedule_on_exec_ctx);
  grpc_tcp_client_connect(&req->connected, &req->ep,
                          grpc_slice_allocator_create(
                              req->resource_quota, grpc_sockaddr_to_uri(addr)),
                          req->context->pollset_set, nullptr, addr,
                          req->deadline);
}

static void on_resolved(void* arg, grpc_error_handle error) {
  InternalRequest* req = static_cast<InternalRequest*>(arg);
  if (error != GRPC_ERROR_NONE) {
    finish(req, GRPC_ERROR_REF(error));
    return;
  }
  req->next_address = 0;
  next_address(req, GRPC_ERROR_NONE);
}

static void internal_request_begin(grpc_httpcli_context* context,
                                   grpc_polling_entity* pollent,
                                   grpc_resource_quota* resource_quota,
                                   const grpc_httpcli_request* request,
                                   grpc_millis deadline, grpc_closure* on_done,
                                   grpc_httpcli_response* response,
                                   const char* name,
                                   const grpc_slice& request_text) {
  InternalRequest* req = new InternalRequest(
      request_text, response, resource_quota,

      request->host, request->ssl_host_override, deadline,
      request->handshaker ? request->handshaker : &grpc_httpcli_plaintext,
      on_done, context, pollent, name);
  GRPC_CLOSURE_INIT(&req->on_read, on_read, req, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&req->done_write, done_write, req,
                    grpc_schedule_on_exec_ctx);
  GPR_ASSERT(pollent);
  grpc_polling_entity_add_to_pollset_set(req->pollent,
                                         req->context->pollset_set);
  grpc_resolve_address(
      request->host, req->handshaker->default_port, req->context->pollset_set,
      GRPC_CLOSURE_CREATE(on_resolved, req, grpc_schedule_on_exec_ctx),
      &req->addresses);
}

void grpc_httpcli_get(grpc_httpcli_context* context,
                      grpc_polling_entity* pollent,
                      grpc_resource_quota* resource_quota,
                      const grpc_httpcli_request* request, grpc_millis deadline,
                      grpc_closure* on_done, grpc_httpcli_response* response) {
  if (g_get_override && g_get_override(request, deadline, on_done, response)) {
    grpc_resource_quota_unref_internal(resource_quota);
    return;
  }
  std::string name =
      absl::StrFormat("HTTP:GET:%s:%s", request->host, request->http.path);
  internal_request_begin(context, pollent, resource_quota, request, deadline,
                         on_done, response, name.c_str(),
                         grpc_httpcli_format_get_request(request));
}

void grpc_httpcli_post(grpc_httpcli_context* context,
                       grpc_polling_entity* pollent,
                       grpc_resource_quota* resource_quota,
                       const grpc_httpcli_request* request,
                       const char* body_bytes, size_t body_size,
                       grpc_millis deadline, grpc_closure* on_done,
                       grpc_httpcli_response* response) {
  if (g_post_override && g_post_override(request, body_bytes, body_size,
                                         deadline, on_done, response)) {
    grpc_resource_quota_unref_internal(resource_quota);
    return;
  }
  std::string name =
      absl::StrFormat("HTTP:POST:%s:%s", request->host, request->http.path);
  internal_request_begin(
      context, pollent, resource_quota, request, deadline, on_done, response,
      name.c_str(),
      grpc_httpcli_format_post_request(request, body_bytes, body_size));
}

void grpc_httpcli_set_override(grpc_httpcli_get_override get,
                               grpc_httpcli_post_override post) {
  g_get_override = get;
  g_post_override = post;
}
