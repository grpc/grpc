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

#include <limits.h>
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
#include "src/core/lib/transport/tcp_connect_handshaker.h"

namespace grpc_core {

namespace {

grpc_httpcli_get_override g_get_override;
grpc_httpcli_post_override g_post_override;
grpc_httpcli_put_override g_put_override;
void (*g_test_only_on_handshake_done_intercept)(HttpRequest* req);

}  // namespace

OrphanablePtr<HttpRequest> HttpRequest::Get(
    URI uri, const grpc_channel_args* channel_args,
    grpc_polling_entity* pollent, const grpc_http_request* request,
    Timestamp deadline, grpc_closure* on_done, grpc_http_response* response,
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
    Timestamp deadline, grpc_closure* on_done, grpc_http_response* response,
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

OrphanablePtr<HttpRequest> HttpRequest::Put(
    URI uri, const grpc_channel_args* channel_args,
    grpc_polling_entity* pollent, const grpc_http_request* request,
    Timestamp deadline, grpc_closure* on_done, grpc_http_response* response,
    RefCountedPtr<grpc_channel_credentials> channel_creds) {
  absl::optional<std::function<void()>> test_only_generate_response;
  if (g_put_override != nullptr) {
    test_only_generate_response = [request, uri, deadline, on_done,
                                   response]() {
      g_put_override(request, uri.authority().c_str(), uri.path().c_str(),
                     request->body, request->body_length, deadline, on_done,
                     response);
    };
  }
  std::string name =
      absl::StrFormat("HTTP:PUT:%s:%s", uri.authority(), uri.path());
  const grpc_slice request_text = grpc_httpcli_format_put_request(
      request, uri.authority().c_str(), uri.path().c_str());
  return MakeOrphanable<HttpRequest>(
      std::move(uri), request_text, response, deadline, channel_args, on_done,
      pollent, name.c_str(), std::move(test_only_generate_response),
      std::move(channel_creds));
}

void HttpRequest::SetOverride(grpc_httpcli_get_override get,
                              grpc_httpcli_post_override post,
                              grpc_httpcli_put_override put) {
  g_get_override = get;
  g_post_override = post;
  g_put_override = put;
}

void HttpRequest::TestOnlySetOnHandshakeDoneIntercept(
    void (*intercept)(HttpRequest* req)) {
  g_test_only_on_handshake_done_intercept = intercept;
}

HttpRequest::HttpRequest(
    URI uri, const grpc_slice& request_text, grpc_http_response* response,
    Timestamp deadline, const grpc_channel_args* channel_args,
    grpc_closure* on_done, grpc_polling_entity* pollent, const char* name,
    absl::optional<std::function<void()>> test_only_generate_response,
    RefCountedPtr<grpc_channel_credentials> channel_creds)
    : uri_(std::move(uri)),
      request_text_(request_text),
      deadline_(deadline),
      channel_args_(CoreConfiguration::Get()
                        .channel_args_preconditioning()
                        .PreconditionChannelArgs(channel_args)
                        .ToC()),
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
  dns_request_handle_ = GetDNSResolver()->ResolveName(
      uri_.authority(), uri_.scheme(), pollset_set_,
      absl::bind_front(&HttpRequest::OnResolved, this));
}

void HttpRequest::Orphan() {
  {
    MutexLock lock(&mu_);
    GPR_ASSERT(!cancelled_);
    cancelled_ = true;
    // cancel potentially pending DNS resolution.
    if (dns_request_handle_.has_value() &&
        GetDNSResolver()->Cancel(dns_request_handle_.value())) {
      Finish(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "cancelled during DNS resolution"));
      Unref();
    }
    if (handshake_mgr_ != nullptr) {
      // Shutdown will cancel any ongoing tcp connect.
      handshake_mgr_->Shutdown(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "HTTP request cancelled during handshake"));
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
  auto addr_text = grpc_sockaddr_to_uri(addr);
  overall_error_ = grpc_error_add_child(
      overall_error_,
      grpc_error_set_str(
          error, GRPC_ERROR_STR_TARGET_ADDRESS,
          addr_text.ok() ? addr_text.value() : addr_text.status().ToString()));
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
  grpc_endpoint_write(ep_, &outgoing_, &done_write_, nullptr,
                      /*max_frame_size=*/INT_MAX);
}

void HttpRequest::OnHandshakeDone(void* arg, grpc_error_handle error) {
  auto* args = static_cast<HandshakerArgs*>(arg);
  RefCountedPtr<HttpRequest> req(static_cast<HttpRequest*>(args->user_data));
  if (g_test_only_on_handshake_done_intercept != nullptr) {
    // Run this testing intercept before the lock so that it has a chance to
    // do things like calling Orphan on the request
    g_test_only_on_handshake_done_intercept(req.get());
  }
  MutexLock lock(&req->mu_);
  req->own_endpoint_ = true;
  if (error != GRPC_ERROR_NONE) {
    req->handshake_mgr_.reset();
    req->NextAddress(GRPC_ERROR_REF(error));
    return;
  }
  // Handshake completed, so we own fields in args
  grpc_channel_args_destroy(args->args);
  grpc_slice_buffer_destroy_internal(args->read_buffer);
  gpr_free(args->read_buffer);
  req->ep_ = args->endpoint;
  req->handshake_mgr_.reset();
  if (req->cancelled_) {
    req->NextAddress(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "HTTP request cancelled during handshake"));
    return;
  }
  req->StartWrite();
}

void HttpRequest::DoHandshake(const grpc_resolved_address* addr) {
  // Create the security connector using the credentials and target name.
  grpc_channel_args* new_args_from_connector = nullptr;
  RefCountedPtr<grpc_channel_security_connector> sc =
      channel_creds_->create_security_connector(
          nullptr /*call_creds*/, uri_.authority().c_str(), channel_args_,
          &new_args_from_connector);
  if (sc == nullptr) {
    Finish(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
        "failed to create security connector", &overall_error_, 1));
    return;
  }
  absl::StatusOr<std::string> address = grpc_sockaddr_to_uri(addr);
  if (!address.ok()) {
    Finish(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
        "Failed to extract URI from address", &overall_error_, 1));
    return;
  }
  absl::InlinedVector<grpc_arg, 2> args_to_add = {
      grpc_security_connector_to_arg(sc.get()),
      grpc_channel_arg_string_create(
          const_cast<char*>(GRPC_ARG_TCP_HANDSHAKER_RESOLVED_ADDRESS),
          const_cast<char*>(address.value().c_str())),
  };
  const grpc_channel_args* new_args = grpc_channel_args_copy_and_add(
      new_args_from_connector != nullptr ? new_args_from_connector
                                         : channel_args_,
      args_to_add.data(), args_to_add.size());
  grpc_channel_args_destroy(new_args_from_connector);
  // Start the handshake
  handshake_mgr_ = MakeRefCounted<HandshakeManager>();
  CoreConfiguration::Get().handshaker_registry().AddHandshakers(
      HANDSHAKER_CLIENT, new_args, pollset_set_, handshake_mgr_.get());
  Ref().release();  // ref held by pending handshake
  grpc_endpoint* ep = ep_;
  ep_ = nullptr;
  own_endpoint_ = false;
  handshake_mgr_->DoHandshake(ep, new_args, deadline_,
                              /*acceptor=*/nullptr, OnHandshakeDone,
                              /*user_data=*/this);
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
  DoHandshake(addr);
}

void HttpRequest::OnResolved(
    absl::StatusOr<std::vector<grpc_resolved_address>> addresses_or) {
  RefCountedPtr<HttpRequest> unreffer(this);
  MutexLock lock(&mu_);
  dns_request_handle_.reset();
  if (cancelled_) {
    Finish(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "cancelled during DNS resolution"));
    return;
  }
  if (!addresses_or.ok()) {
    Finish(absl_status_to_grpc_error(addresses_or.status()));
    return;
  }
  addresses_ = std::move(*addresses_or);
  next_address_ = 0;
  NextAddress(GRPC_ERROR_NONE);
}

}  // namespace grpc_core
