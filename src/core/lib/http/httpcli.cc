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

#include "absl/functional/bind_front.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/http/format_request.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {
namespace {

class InternalRequest {
 public:
  InternalRequest(const grpc_slice& request_text,
                  grpc_httpcli_response* response,
                  ResourceQuotaRefPtr resource_quota, absl::string_view host,
                  absl::string_view ssl_host_override, grpc_millis deadline,
                  const grpc_httpcli_handshaker* handshaker,
                  grpc_closure* on_done, grpc_polling_entity* pollent,
                  const char* name)
      : request_text_(request_text),
        resource_quota_(std::move(resource_quota)),
        host_(host),
        ssl_host_override_(ssl_host_override),
        deadline_(deadline),
        handshaker_(handshaker),
        on_done_(on_done),
        pollent_(pollent),
        pollset_set_(grpc_pollset_set_create()) {
    grpc_http_parser_init(&parser_, GRPC_HTTP_RESPONSE, response);
    grpc_slice_buffer_init(&incoming_);
    grpc_slice_buffer_init(&outgoing_);
    grpc_iomgr_register_object(&iomgr_obj_, name);

    GRPC_CLOSURE_INIT(&on_read_, OnRead, this, grpc_schedule_on_exec_ctx);
    GRPC_CLOSURE_INIT(&done_write_, DoneWrite, this, grpc_schedule_on_exec_ctx);
    GPR_ASSERT(pollent);
    grpc_polling_entity_add_to_pollset_set(pollent, pollset_set_);
    dns_request_ = GetDNSResolver()->ResolveName(
        host_.c_str(), handshaker_->default_port, pollset_set_,
        absl::bind_front(&InternalRequest::OnResolved, this));
    dns_request_->Start();
  }

  ~InternalRequest() {
    grpc_http_parser_destroy(&parser_);
    if (ep_ != nullptr) {
      grpc_endpoint_destroy(ep_);
    }
    grpc_slice_unref_internal(request_text_);
    grpc_iomgr_unregister_object(&iomgr_obj_);
    grpc_slice_buffer_destroy_internal(&incoming_);
    grpc_slice_buffer_destroy_internal(&outgoing_);
    GRPC_ERROR_UNREF(overall_error_);
    grpc_pollset_set_destroy(pollset_set_);
  }

 private:
  void Finish(grpc_error_handle error) {
    grpc_polling_entity_del_from_pollset_set(pollent_, pollset_set_);
    ExecCtx::Run(DEBUG_LOCATION, on_done_, error);
    delete this;
  }

  void AppendError(grpc_error_handle error) {
    if (overall_error_ == GRPC_ERROR_NONE) {
      overall_error_ =
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Failed HTTP/1 client request");
    }
    const grpc_resolved_address* addr = &addresses_[next_address_ - 1];
    std::string addr_text = grpc_sockaddr_to_uri(addr);
    overall_error_ = grpc_error_add_child(
        overall_error_,
        grpc_error_set_str(error, GRPC_ERROR_STR_TARGET_ADDRESS, addr_text));
  }

  void DoRead() {
    grpc_endpoint_read(ep_, &incoming_, &on_read_, /*urgent=*/true);
  }

  static void OnRead(void* user_data, grpc_error_handle error) {
    InternalRequest* req = static_cast<InternalRequest*>(user_data);
    req->OnReadInternal(error);
  }

  void OnReadInternal(grpc_error_handle error) {
    size_t i;

    for (i = 0; i < incoming_.count; i++) {
      if (GRPC_SLICE_LENGTH(incoming_.slices[i])) {
        have_read_byte_ = 1;
        grpc_error_handle err =
            grpc_http_parser_parse(&parser_, incoming_.slices[i], nullptr);
        if (err != GRPC_ERROR_NONE) {
          Finish(err);
          return;
        }
      }
    }

    if (error == GRPC_ERROR_NONE) {
      DoRead();
    } else if (!have_read_byte_) {
      NextAddress(GRPC_ERROR_REF(error));
    } else {
      Finish(grpc_http_parser_eof(&parser_));
    }
  }

  void OnWritten() { DoRead(); }

  static void DoneWrite(void* arg, grpc_error_handle error) {
    InternalRequest* req = static_cast<InternalRequest*>(arg);
    if (error == GRPC_ERROR_NONE) {
      req->OnWritten();
    } else {
      req->NextAddress(GRPC_ERROR_REF(error));
    }
  }

  void StartWrite() {
    grpc_slice_ref_internal(request_text_);
    grpc_slice_buffer_add(&outgoing_, request_text_);
    grpc_endpoint_write(ep_, &outgoing_, &done_write_, nullptr);
  }

  static void OnHandshakeDone(void* arg, grpc_endpoint* ep) {
    InternalRequest* req = static_cast<InternalRequest*>(arg);

    if (!ep) {
      req->NextAddress(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Unexplained handshake failure"));
      return;
    }

    req->ep_ = ep;
    req->StartWrite();
  }

  static void OnConnected(void* arg, grpc_error_handle error) {
    InternalRequest* req = static_cast<InternalRequest*>(arg);

    if (!req->ep_) {
      req->NextAddress(GRPC_ERROR_REF(error));
      return;
    }
    req->handshaker_->handshake(req, req->ep_,
                                req->ssl_host_override_.empty()
                                    ? req->host_.c_str()
                                    : req->ssl_host_override_.c_str(),
                                req->deadline_, OnHandshakeDone);
  }

  void NextAddress(grpc_error_handle error) {
    if (error != GRPC_ERROR_NONE) {
      AppendError(error);
    }
    if (next_address_ == addresses_.size()) {
      Finish(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
          "Failed HTTP requests to all targets", &overall_error_, 1));
      return;
    }
    const grpc_resolved_address* addr = &addresses_[next_address_++];
    GRPC_CLOSURE_INIT(&connected_, OnConnected, this,
                      grpc_schedule_on_exec_ctx);
    grpc_arg rq_arg = grpc_channel_arg_pointer_create(
        const_cast<char*>(GRPC_ARG_RESOURCE_QUOTA), resource_quota_->c_ptr(),
        grpc_resource_quota_arg_vtable());
    grpc_channel_args channel_args{1, &rq_arg};
    auto* args = CoreConfiguration::Get()
                     .channel_args_preconditioning()
                     .PreconditionChannelArgs(&channel_args);
    grpc_tcp_client_connect(&connected_, &ep_, pollset_set_, args, addr,
                            deadline_);
    grpc_channel_args_destroy(args);
  }

  void OnResolved(
      absl::StatusOr<std::vector<grpc_resolved_address>> addresses_or) {
    dns_request_.reset();
    if (!addresses_or.ok()) {
      Finish(absl_status_to_grpc_error(addresses_or.status()));
      return;
    }
    addresses_ = std::move(*addresses_or);
    next_address_ = 0;
    NextAddress(GRPC_ERROR_NONE);
  }

  grpc_slice request_text_;
  grpc_http_parser parser_;
  std::vector<grpc_resolved_address> addresses_;
  size_t next_address_ = 0;
  grpc_endpoint* ep_ = nullptr;
  ResourceQuotaRefPtr resource_quota_;
  std::string host_;
  std::string ssl_host_override_;
  grpc_millis deadline_;
  int have_read_byte_ = 0;
  const grpc_httpcli_handshaker* handshaker_;
  grpc_closure* on_done_;
  grpc_polling_entity* pollent_;
  grpc_pollset_set* pollset_set_;
  grpc_iomgr_object iomgr_obj_;
  grpc_slice_buffer incoming_;
  grpc_slice_buffer outgoing_;
  grpc_closure on_read_;
  grpc_closure done_write_;
  grpc_closure connected_;
  grpc_error_handle overall_error_ = GRPC_ERROR_NONE;
  OrphanablePtr<DNSResolver::Request> dns_request_;
};

}  // namespace
}  // namespace grpc_core

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

static void internal_request_begin(
    grpc_polling_entity* pollent, grpc_core::ResourceQuotaRefPtr resource_quota,
    const grpc_httpcli_request* request, grpc_millis deadline,
    grpc_closure* on_done, grpc_httpcli_response* response, const char* name,
    const grpc_slice& request_text) {
  new grpc_core::InternalRequest(
      request_text, response, std::move(resource_quota), request->host,
      request->ssl_host_override, deadline,
      request->handshaker ? request->handshaker : &grpc_httpcli_plaintext,
      on_done, pollent, name);
}

void grpc_httpcli_get(grpc_polling_entity* pollent,
                      grpc_core::ResourceQuotaRefPtr resource_quota,
                      const grpc_httpcli_request* request, grpc_millis deadline,
                      grpc_closure* on_done, grpc_httpcli_response* response) {
  if (g_get_override && g_get_override(request, deadline, on_done, response)) {
    return;
  }
  std::string name =
      absl::StrFormat("HTTP:GET:%s:%s", request->host, request->http.path);
  internal_request_begin(pollent, std::move(resource_quota), request, deadline,
                         on_done, response, name.c_str(),
                         grpc_httpcli_format_get_request(request));
}

void grpc_httpcli_post(grpc_polling_entity* pollent,
                       grpc_core::ResourceQuotaRefPtr resource_quota,
                       const grpc_httpcli_request* request,
                       const char* body_bytes, size_t body_size,
                       grpc_millis deadline, grpc_closure* on_done,
                       grpc_httpcli_response* response) {
  if (g_post_override && g_post_override(request, body_bytes, body_size,
                                         deadline, on_done, response)) {
    return;
  }
  std::string name =
      absl::StrFormat("HTTP:POST:%s:%s", request->host, request->http.path);
  internal_request_begin(
      pollent, std::move(resource_quota), request, deadline, on_done, response,
      name.c_str(),
      grpc_httpcli_format_post_request(request, body_bytes, body_size));
}

void grpc_httpcli_set_override(grpc_httpcli_get_override get,
                               grpc_httpcli_post_override post) {
  g_get_override = get;
  g_post_override = post;
}
