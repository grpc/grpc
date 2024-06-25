//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include "src/core/util/http_client/httpcli.h"

#include <limits.h>

#include <string>
#include <utility>

#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

#include <grpc/grpc.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/handshaker/handshaker.h"
#include "src/core/handshaker/handshaker_registry.h"
#include "src/core/handshaker/tcp_connect/tcp_connect_handshaker.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/util/http_client/format_request.h"
#include "src/core/util/http_client/parser.h"

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
                        .ToC()
                        .release()),
      channel_creds_(std::move(channel_creds)),
      on_done_(on_done),
      resource_quota_(ResourceQuotaFromChannelArgs(channel_args_)),
      pollent_(pollent),
      pollset_set_(grpc_pollset_set_create()),
      test_only_generate_response_(std::move(test_only_generate_response)),
      resolver_(GetDNSResolver()) {
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
  CHECK(pollent);
  grpc_polling_entity_add_to_pollset_set(pollent, pollset_set_);
}

HttpRequest::~HttpRequest() {
  grpc_channel_args_destroy(channel_args_);
  grpc_http_parser_destroy(&parser_);
  ep_.reset();
  CSliceUnref(request_text_);
  grpc_iomgr_unregister_object(&iomgr_obj_);
  grpc_slice_buffer_destroy(&incoming_);
  grpc_slice_buffer_destroy(&outgoing_);
  grpc_pollset_set_destroy(pollset_set_);
}

void HttpRequest::Start() {
  MutexLock lock(&mu_);
  if (test_only_generate_response_.has_value()) {
    test_only_generate_response_.value()();
    return;
  }
  Ref().release();  // ref held by pending DNS resolution
  dns_request_handle_ = resolver_->LookupHostname(
      absl::bind_front(&HttpRequest::OnResolved, this), uri_.authority(),
      uri_.scheme(), kDefaultDNSRequestTimeout, pollset_set_,
      /*name_server=*/"");
}

void HttpRequest::Orphan() {
  {
    MutexLock lock(&mu_);
    CHECK(!cancelled_);
    cancelled_ = true;
    // cancel potentially pending DNS resolution.
    if (dns_request_handle_.has_value() &&
        resolver_->Cancel(dns_request_handle_.value())) {
      Finish(GRPC_ERROR_CREATE("cancelled during DNS resolution"));
      Unref();
    }
    if (handshake_mgr_ != nullptr) {
      // Shutdown will cancel any ongoing tcp connect.
      handshake_mgr_->Shutdown(
          GRPC_ERROR_CREATE("HTTP request cancelled during handshake"));
    }
    ep_.reset();
  }
  Unref();
}

void HttpRequest::AppendError(grpc_error_handle error) {
  if (overall_error_.ok()) {
    overall_error_ = GRPC_ERROR_CREATE("Failed HTTP/1 client request");
  }
  const grpc_resolved_address* addr = &addresses_[next_address_ - 1];
  auto addr_text = grpc_sockaddr_to_uri(addr);
  if (addr_text.ok()) error = AddMessagePrefix(*addr_text, std::move(error));
  overall_error_ = grpc_error_add_child(overall_error_, std::move(error));
}

void HttpRequest::OnReadInternal(grpc_error_handle error) {
  for (size_t i = 0; i < incoming_.count; i++) {
    if (GRPC_SLICE_LENGTH(incoming_.slices[i])) {
      have_read_byte_ = 1;
      grpc_error_handle err =
          grpc_http_parser_parse(&parser_, incoming_.slices[i], nullptr);
      if (!err.ok()) {
        Finish(err);
        return;
      }
    }
  }
  if (cancelled_) {
    Finish(GRPC_ERROR_CREATE_REFERENCING("HTTP1 request cancelled during read",
                                         &overall_error_, 1));
  } else if (error.ok()) {
    DoRead();
  } else if (!have_read_byte_) {
    NextAddress(error);
  } else {
    Finish(grpc_http_parser_eof(&parser_));
  }
}

void HttpRequest::ContinueDoneWriteAfterScheduleOnExecCtx(
    void* arg, grpc_error_handle error) {
  RefCountedPtr<HttpRequest> req(static_cast<HttpRequest*>(arg));
  MutexLock lock(&req->mu_);
  if (error.ok() && !req->cancelled_) {
    req->OnWritten();
  } else {
    req->NextAddress(error);
  }
}

void HttpRequest::StartWrite() {
  CSliceRef(request_text_);
  grpc_slice_buffer_add(&outgoing_, request_text_);
  Ref().release();  // ref held by pending write
  grpc_endpoint_write(ep_.get(), &outgoing_, &done_write_, nullptr,
                      /*max_frame_size=*/INT_MAX);
}

void HttpRequest::OnHandshakeDone(absl::StatusOr<HandshakerArgs*> result) {
  if (g_test_only_on_handshake_done_intercept != nullptr) {
    // Run this testing intercept before the lock so that it has a chance to
    // do things like calling Orphan on the request
    g_test_only_on_handshake_done_intercept(this);
  }
  MutexLock lock(&mu_);
  if (!result.ok()) {
    handshake_mgr_.reset();
    NextAddress(result.status());
    return;
  }
  // Handshake completed, so get the endpoint.
  ep_ = std::move((*result)->endpoint);
  handshake_mgr_.reset();
  if (cancelled_) {
    NextAddress(GRPC_ERROR_CREATE("HTTP request cancelled during handshake"));
    return;
  }
  StartWrite();
}

void HttpRequest::DoHandshake(const grpc_resolved_address* addr) {
  // Create the security connector using the credentials and target name.
  ChannelArgs args = ChannelArgs::FromC(channel_args_);
  RefCountedPtr<grpc_channel_security_connector> sc =
      channel_creds_->create_security_connector(
          nullptr /*call_creds*/, uri_.authority().c_str(), &args);
  if (sc == nullptr) {
    Finish(GRPC_ERROR_CREATE_REFERENCING("failed to create security connector",
                                         &overall_error_, 1));
    return;
  }
  absl::StatusOr<std::string> address = grpc_sockaddr_to_uri(addr);
  if (!address.ok()) {
    Finish(GRPC_ERROR_CREATE_REFERENCING("Failed to extract URI from address",
                                         &overall_error_, 1));
    return;
  }
  args = args.SetObject(std::move(sc))
             .Set(GRPC_ARG_TCP_HANDSHAKER_RESOLVED_ADDRESS, address.value());
  // Start the handshake
  handshake_mgr_ = MakeRefCounted<HandshakeManager>();
  CoreConfiguration::Get().handshaker_registry().AddHandshakers(
      HANDSHAKER_CLIENT, args, pollset_set_, handshake_mgr_.get());
  handshake_mgr_->DoHandshake(
      nullptr, args, deadline_, /*acceptor=*/nullptr,
      [self = Ref()](absl::StatusOr<HandshakerArgs*> result) {
        self->OnHandshakeDone(std::move(result));
      });
}

void HttpRequest::NextAddress(grpc_error_handle error) {
  if (!error.ok()) {
    AppendError(error);
  }
  if (cancelled_) {
    Finish(GRPC_ERROR_CREATE_REFERENCING("HTTP request was cancelled",
                                         &overall_error_, 1));
    return;
  }
  if (next_address_ == addresses_.size()) {
    Finish(GRPC_ERROR_CREATE_REFERENCING("Failed HTTP requests to all targets",
                                         &overall_error_, 1));
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
    Finish(GRPC_ERROR_CREATE("cancelled during DNS resolution"));
    return;
  }
  if (!addresses_or.ok()) {
    Finish(absl_status_to_grpc_error(addresses_or.status()));
    return;
  }
  addresses_ = std::move(*addresses_or);
  next_address_ = 0;
  NextAddress(absl::OkStatus());
}

}  // namespace grpc_core
