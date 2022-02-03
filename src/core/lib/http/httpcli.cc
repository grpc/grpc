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

grpc_httpcli_get_override g_get_override;
grpc_httpcli_post_override g_post_override;

}  // namespace

OrphanablePtr<HttpRequest> HttpRequest::Get(
    URI uri, const grpc_channel_args* channel_args,
    grpc_polling_entity* pollent, const grpc_http_request* request,
    grpc_millis deadline, grpc_closure* on_done, grpc_http_response* response,
    RefCountedPtr<grpc_channel_credentials> channel_creds) {
  absl::optional<std::function<void()>> test_only_generate_response;
  if (g_get_override != nullptr) {
    test_only_generate_response = [request, uri, deadline, on_done,
                                   response]() {
      // Note that capturing request here assumes it will remain alive
      // until after Start is called. This avoids making a copy as this
      // code path is only used for test mocks.
      g_get_override(request, uri.authority().c_str(), uri.path().c_str(),
                     deadline, on_done, response);
    };
  }
  std::string name =
      absl::StrFormat("HTTP:GET:%s:%s", uri.authority(), uri.path());
  const grpc_slice request_text = grpc_httpcli_format_get_request(
      request, uri.authority().c_str(), uri.path().c_str());
  return MakeOrphanable<HttpRequest>(
      std::move(uri), request_text, response, deadline, channel_args, on_done,
      pollent, name.c_str(), std::move(test_only_generate_response),
      std::move(channel_creds));
}

OrphanablePtr<HttpRequest> HttpRequest::Post(
    URI uri, const grpc_channel_args* channel_args,
    grpc_polling_entity* pollent, const grpc_http_request* request,
    grpc_millis deadline, grpc_closure* on_done, grpc_http_response* response,
    RefCountedPtr<grpc_channel_credentials> channel_creds) {
  absl::optional<std::function<void()>> test_only_generate_response;
  if (g_post_override != nullptr) {
    test_only_generate_response = [request, uri, deadline, on_done,
                                   response]() {
      g_post_override(request, uri.authority().c_str(), uri.path().c_str(),
                      request->body, request->body_length, deadline, on_done,
                      response);
    };
  }
  std::string name =
      absl::StrFormat("HTTP:POST:%s:%s", uri.authority(), uri.path());
  const grpc_slice request_text = grpc_httpcli_format_post_request(
      request, uri.authority().c_str(), uri.path().c_str());
  return MakeOrphanable<HttpRequest>(
      std::move(uri), request_text, response, deadline, channel_args, on_done,
      pollent, name.c_str(), std::move(test_only_generate_response),
      std::move(channel_creds));
}

void HttpRequest::SetOverride(grpc_httpcli_get_override get,
                              grpc_httpcli_post_override post) {
  g_get_override = get;
  g_post_override = post;
}

HttpRequest::HttpRequest(
    URI uri, const grpc_slice& request_text, grpc_http_response* response,
    grpc_millis deadline, const grpc_channel_args* channel_args,
    grpc_closure* on_done, grpc_polling_entity* pollent, const char* name,
    absl::optional<std::function<void()>> test_only_generate_response,
    RefCountedPtr<grpc_channel_credentials> channel_creds)
    : uri_(std::move(uri)),
      request_text_(request_text),
      deadline_(deadline),
      channel_args_(CoreConfiguration::Get()
                        .channel_args_preconditioning()
                        .PreconditionChannelArgs(channel_args)),
      channel_creds_(std::move(channel_creds)),
      on_done_(on_done),
      resource_quota_(ResourceQuotaFromChannelArgs(channel_args_)),
      pollent_(pollent),
      pollset_set_(grpc_pollset_set_create()),
      test_only_generate_response_(std::move(test_only_generate_response)) {
  grpc_http_parser_init(&parser_, GRPC_HTTP_RESPONSE, response);
  grpc_slice_buffer_init(&incoming_);
  grpc_slice_buffer_init(&outgoing_);
  grpc_iomgr_register_object(&iomgr_obj_, name);
  GRPC_CLOSURE_INIT(&on_read_, OnRead, this, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&continue_on_read_after_schedule_on_exec_ctx_,
                    ContinueOnReadAfterScheduleOnExecCtx, this,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&done_write_, DoneWrite, this, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&continue_done_write_after_schedule_on_exec_ctx_,
                    ContinueDoneWriteAfterScheduleOnExecCtx, this,
                    grpc_schedule_on_exec_ctx);
  GPR_ASSERT(pollent);
  grpc_polling_entity_add_to_pollset_set(pollent, pollset_set_);
  // Create the DNS resolver. We'll start resolving when Start is called.
  dns_request_ = GetDNSResolver()->ResolveName(
      uri_.authority(), uri_.scheme(), pollset_set_,
      absl::bind_front(&HttpRequest::OnResolved, this));
}

HttpRequest::~HttpRequest() {
  grpc_channel_args_destroy(channel_args_);
  grpc_http_parser_destroy(&parser_);
  if (own_endpoint_ && ep_ != nullptr) {
    grpc_endpoint_destroy(ep_);
  }
  grpc_slice_unref_internal(request_text_);
  grpc_iomgr_unregister_object(&iomgr_obj_);
  grpc_slice_buffer_destroy_internal(&incoming_);
  grpc_slice_buffer_destroy_internal(&outgoing_);
  GRPC_ERROR_UNREF(overall_error_);
  grpc_pollset_set_destroy(pollset_set_);
}

void HttpRequest::Start() {
  MutexLock lock(&mu_);
  if (test_only_generate_response_.has_value()) {
    test_only_generate_response_.value()();
    return;
  }
  Ref().release();  // ref held by pending DNS resolution
  dns_request_->Start();
}

void HttpRequest::Orphan() {
  {
    MutexLock lock(&mu_);
    GPR_ASSERT(!cancelled_);
    cancelled_ = true;
    dns_request_.reset();  // cancel potentially pending DNS resolution
    if (connecting_) {
      // gRPC's TCP connection establishment API doesn't currently have
      // a mechanism for cancellation. So invoke the user callback now. The TCP
      // connection will eventually complete (at least within its deadline), and
      // we'll simply unref ourselves at that point.
      // TODO(apolcyn): fix this to cancel the TCP connection attempt when
      // an API to do so exists.
      Finish(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
          "HTTP request cancelled during TCP connection establishment",
          &overall_error_, 1));
    }
    if (handshake_mgr_ != nullptr) {
      handshake_mgr_->Shutdown(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "HTTP request cancelled during security handshake"));
    }
    if (own_endpoint_ && ep_ != nullptr) {
      grpc_endpoint_shutdown(
          ep_, GRPC_ERROR_CREATE_FROM_STATIC_STRING("HTTP request cancelled"));
    }
  }
  Unref();
}

void HttpRequest::AppendError(grpc_error_handle error) {
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

void HttpRequest::OnReadInternal(grpc_error_handle error) {
  for (size_t i = 0; i < incoming_.count; i++) {
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
  if (cancelled_) {
    Finish(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
        "HTTP1 request cancelled during read", &overall_error_, 1));
  } else if (error == GRPC_ERROR_NONE) {
    DoRead();
  } else if (!have_read_byte_) {
    NextAddress(GRPC_ERROR_REF(error));
  } else {
    Finish(grpc_http_parser_eof(&parser_));
  }
}

void HttpRequest::ContinueDoneWriteAfterScheduleOnExecCtx(
    void* arg, grpc_error_handle error) {
  RefCountedPtr<HttpRequest> req(static_cast<HttpRequest*>(arg));
  MutexLock lock(&req->mu_);
  if (error == GRPC_ERROR_NONE && !req->cancelled_) {
    req->OnWritten();
  } else {
    req->NextAddress(GRPC_ERROR_REF(error));
  }
}

void HttpRequest::StartWrite() {
  grpc_slice_ref_internal(request_text_);
  grpc_slice_buffer_add(&outgoing_, request_text_);
  Ref().release();  // ref held by pending write
  grpc_endpoint_write(ep_, &outgoing_, &done_write_, nullptr);
}

void HttpRequest::OnHandshakeDone(void* arg, grpc_error_handle error) {
  auto* args = static_cast<HandshakerArgs*>(arg);
  RefCountedPtr<HttpRequest> req(static_cast<HttpRequest*>(args->user_data));
  MutexLock lock(&req->mu_);
  req->own_endpoint_ = true;
  if (error != GRPC_ERROR_NONE || req->cancelled_) {
    gpr_log(GPR_ERROR, "Secure transport setup failed: %s",
            grpc_error_std_string(error).c_str());
    req->NextAddress(GRPC_ERROR_REF(error));
    return;
  }
  grpc_channel_args_destroy(args->args);
  grpc_slice_buffer_destroy_internal(args->read_buffer);
  gpr_free(args->read_buffer);
  req->ep_ = args->endpoint;
  req->StartWrite();
}

void HttpRequest::OnConnected(void* arg, grpc_error_handle error) {
  RefCountedPtr<HttpRequest> req(static_cast<HttpRequest*>(arg));
  MutexLock lock(&req->mu_);
  req->connecting_ = false;
  req->own_endpoint_ = true;
  if (req->cancelled_) {
    // since we were cancelled while connecting, Finish has already
    // been called.
    return;
  }
  if (!req->ep_) {
    req->NextAddress(GRPC_ERROR_REF(error));
    return;
  }
  // TODO(yihuaz): treating nullptr channel_creds_ as insecure is
  // a hack used to support the port server client (a test utility) in
  // unsecure builds (when no definition of grpc_insecure_credentials_create
  // exists). We can remove this hack and unconditionally assume a valid
  // channel_creds_ object after unsecure builds are deleted, in
  // https://github.com/grpc/grpc/pull/25586.
  if (req->channel_creds_ == nullptr) {
    gpr_log(GPR_DEBUG,
            "HTTP request skipping handshake because creds are null");
    req->StartWrite();
    return;
  }
  // Create the security connector using the credentials and target name.
  grpc_channel_args* new_args_from_connector = nullptr;
  RefCountedPtr<grpc_channel_security_connector> sc =
      req->channel_creds_->create_security_connector(
          nullptr /*call_creds*/, req->uri_.authority().c_str(),
          req->channel_args_, &new_args_from_connector);
  if (sc == nullptr) {
    req->Finish(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
        "failed to create security connector", &req->overall_error_, 1));
    return;
  }
  grpc_arg security_connector_arg = grpc_security_connector_to_arg(sc.get());
  grpc_channel_args* new_args = grpc_channel_args_copy_and_add(
      new_args_from_connector != nullptr ? new_args_from_connector
                                         : req->channel_args_,
      &security_connector_arg, 1);
  grpc_channel_args_destroy(new_args_from_connector);
  // Start the handshake
  req->handshake_mgr_ = MakeRefCounted<HandshakeManager>();
  CoreConfiguration::Get().handshaker_registry().AddHandshakers(
      HANDSHAKER_CLIENT, new_args, req->pollset_set_,
      req->handshake_mgr_.get());
  req->Ref().release();  // ref held by pending handshake
  grpc_endpoint* ep = req->ep_;
  req->ep_ = nullptr;
  req->own_endpoint_ = false;
  req->handshake_mgr_->DoHandshake(ep, new_args, req->deadline_,
                                   /*acceptor=*/nullptr, OnHandshakeDone,
                                   /*user_data=*/req.get());
  sc.reset(DEBUG_LOCATION, "httpcli");
  grpc_channel_args_destroy(new_args);
}

void HttpRequest::NextAddress(grpc_error_handle error) {
  if (error != GRPC_ERROR_NONE) {
    AppendError(error);
  }
  if (cancelled_) {
    Finish(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
        "HTTP request was cancelled", &overall_error_, 1));
    return;
  }
  if (next_address_ == addresses_.size()) {
    Finish(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
        "Failed HTTP requests to all targets", &overall_error_, 1));
    return;
  }
  const grpc_resolved_address* addr = &addresses_[next_address_++];
  GRPC_CLOSURE_INIT(&connected_, OnConnected, this, grpc_schedule_on_exec_ctx);
  connecting_ = true;
  own_endpoint_ = false;
  Ref().release();  // ref held by pending connect
  grpc_tcp_client_connect(&connected_, &ep_, pollset_set_, channel_args_, addr,
                          deadline_);
}

void HttpRequest::OnResolved(
    absl::StatusOr<std::vector<grpc_resolved_address>> addresses_or) {
  RefCountedPtr<HttpRequest> unreffer(this);
  MutexLock lock(&mu_);
  dns_request_.reset();
  if (!addresses_or.ok()) {
    Finish(absl_status_to_grpc_error(addresses_or.status()));
    return;
  }
  if (cancelled_) {
    Finish(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "cancelled during DNS resolution"));
    return;
  }
  addresses_ = std::move(*addresses_or);
  next_address_ = 0;
  NextAddress(GRPC_ERROR_NONE);
}

}  // namespace grpc_core
