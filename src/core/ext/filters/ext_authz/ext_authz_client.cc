//
// Copyright 2026 gRPC authors.
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

#include "src/core/ext/filters/ext_authz/ext_authz_client.h"

#include <grpc/event_engine/event_engine.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include "envoy/config/core/v3/base.upb.h"
#include "envoy/service/auth/v3/attribute_context.upb.h"
#include "envoy/service/auth/v3/external_auth.upb.h"
#include "envoy/type/v3/http_status.upb.h"
#include "google/protobuf/timestamp.upb.h"
#include "google/rpc/status.upb.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/transport/status_conversion.h"
#include "src/core/util/backoff.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/upb_utils.h"
#include "src/core/util/xds_utils.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "upb/base/string_view.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

//
// ExtAuthzClient
//

ExtAuthzClient::ExtAuthzClient(
    RefCountedPtr<XdsTransportFactory> transport_factory,
    std::unique_ptr<const XdsBootstrap::XdsServerTarget> server)
    : DualRefCounted<ExtAuthzClient>(
          GRPC_TRACE_FLAG_ENABLED(xds_client_refcount) ? "ExtAuthzClient"
                                                       : nullptr),
      transport_factory_(std::move(transport_factory)),
      server_(std::move(server)) {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[ext_authz_client " << this << "] creating ext_authz client "
      << "for server " << server_->server_uri();
  absl::Status status;
  transport_ = transport_factory_->GetTransport(*server_, &status);
  GRPC_CHECK(transport_ != nullptr);
  if (!status.ok()) {
    LOG(ERROR) << "Error creating ExtAuthz client to " << server_->server_uri()
               << ": " << status;
  }
}

ExtAuthzClient::~ExtAuthzClient() {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[ext_authz_client " << this << "] destroying ext_authz client "
      << "for server " << server_->server_uri();
}

void ExtAuthzClient::Orphaned() {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[ext_authz_client " << this << "] orphaning ext_authz client "
      << "for server " << server_->server_uri();
  transport_.reset();
}

void ExtAuthzClient::ResetBackoff() {
  if (transport_ != nullptr) {
    transport_->ResetBackoff();
  }
}

std::string ExtAuthzClient::server_uri() const {
  return server_->server_uri();
}

absl::StatusOr<ExtAuthzClient::ExtAuthzResponse> ExtAuthzClient::Check(
    const ExtAuthzRequestParams& params) {
  std::string payload;
  {
    MutexLock lock(&mu_);
    payload = CreateExtAuthzRequest(params);
  }
  const char* method = "/envoy.service.auth.v3.Authorization/Check";
  auto call = transport_->CreateUnaryCall(method);
  if (call == nullptr) {
    return absl::UnavailableError("Failed to create unary call");
  }
  // Start the call.
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[ext_authz_client " << this << "] ext_authz server "
      << server_->server_uri() << ": starting ext_authz call";
  auto status = call->SendMessage(std::move(payload));
  if (!status.ok()) {
    return status.status();
  }
  MutexLock lock(&mu_);
  return ParseExtAuthzResponse(*status);
}

//
// ExtAuthzRequest
//

namespace {

struct ExtAuthzApiContext {
  ExtAuthzClient* client;
  upb_DefPool* def_pool;
  upb_Arena* arena;
};

std::string SerializeExtAuthzRequest(
    const ExtAuthzApiContext& context,
    const envoy_service_auth_v3_AttributeContext* request) {
  size_t output_length;
  char* output = envoy_service_auth_v3_AttributeContext_serialize(
      request, context.arena, &output_length);
  return std::string(output, output_length);
}

}  // namespace

envoy_service_auth_v3_AttributeContext_Request* CreateRequest(
    const ExtAuthzApiContext& context,
    const ExtAuthzClient::ExtAuthzRequestParams& params) {
  envoy_service_auth_v3_AttributeContext_Request* request =
      envoy_service_auth_v3_AttributeContext_Request_new(context.arena);
  envoy_service_auth_v3_AttributeContext_HttpRequest* http_request =
      envoy_service_auth_v3_AttributeContext_HttpRequest_new(context.arena);
  // set method
  envoy_service_auth_v3_AttributeContext_HttpRequest_set_method(
      http_request, upb_StringView_FromString("POST"));
  // set path
  envoy_service_auth_v3_AttributeContext_HttpRequest_set_path(
      http_request, upb_StringView_FromString(params.path.data()));
  // set size
  envoy_service_auth_v3_AttributeContext_HttpRequest_set_size(http_request, -1);
  // set protocol
  envoy_service_auth_v3_AttributeContext_HttpRequest_set_protocol(
      http_request, upb_StringView_FromString("HTTP/2"));
  // set http_request to request
  envoy_service_auth_v3_AttributeContext_Request_set_http(request,
                                                          http_request);
  // set time
  const Timestamp now = Timestamp::Now();
  google_protobuf_Timestamp* timestamp =
      google_protobuf_Timestamp_new(context.arena);
  google_protobuf_Timestamp_set_nanos(
      timestamp, now.milliseconds_after_process_epoch() * 1000000);
  // set headers
  auto header_map = envoy_config_core_v3_HeaderMap_new(context.arena);
  for (auto& [key, value] : params.headers) {
    auto header = ParseEnvoyHeader(key, value, context.arena);
    if (header != nullptr) {
      auto* header_to_assign =
          envoy_config_core_v3_HeaderMap_add_headers(header_map, context.arena);
      *header_to_assign = *header;
    }
  }
  envoy_service_auth_v3_AttributeContext_Request_set_time(request, timestamp);
  return request;
}

envoy_service_auth_v3_AttributeContext_Peer* CreateSource(
    const ExtAuthzApiContext& context) {
  // TODO(rishesh): add logic for create source
  return nullptr;
}

envoy_service_auth_v3_AttributeContext_Peer* CreateDestination(
    const ExtAuthzApiContext& context) {
  // TODO(rishesh): add logic for create destination
  return nullptr;
}

std::string ExtAuthzClient::CreateExtAuthzRequest(
    const ExtAuthzRequestParams& params) {
  upb::Arena arena;
  const ExtAuthzApiContext context = {this, def_pool_.ptr(), arena.ptr()};
  envoy_service_auth_v3_AttributeContext* attribute_context =
      envoy_service_auth_v3_AttributeContext_new(arena.ptr());

  if (!params.is_client_call) {
    envoy_service_auth_v3_AttributeContext_set_source(attribute_context,
                                                      CreateSource(context));
    envoy_service_auth_v3_AttributeContext_set_destination(
        attribute_context, CreateDestination(context));
  }
  envoy_service_auth_v3_AttributeContext_set_request(
      attribute_context, CreateRequest(context, params));

  return SerializeExtAuthzRequest(context, attribute_context);
}

//
// ExtAuthzClient::ExtAuthzResponse
//

absl::StatusOr<ExtAuthzClient::ExtAuthzResponse>
ExtAuthzClient::ParseExtAuthzResponse(absl::string_view encoded_response) {
  upb::Arena arena;
  const envoy_service_auth_v3_CheckResponse* decoded_response =
      envoy_service_auth_v3_CheckResponse_parse(
          encoded_response.data(), encoded_response.size(), arena.ptr());
  if (decoded_response == nullptr) {
    return absl::UnavailableError("Can't decode response.");
  }
  // const ExtAuthzApiContext context = {this, def_pool_.ptr(), arena.ptr()};
  const auto* status =
      envoy_service_auth_v3_CheckResponse_status(decoded_response);
  const int32_t status_code = google_rpc_Status_code(status);
  ExtAuthzResponse result;
  result.status_code = grpc_http2_status_to_grpc_status(status_code);
  ValidationErrors errors;
  if (status_code == 200) {
    const auto* ok_resp =
        envoy_service_auth_v3_CheckResponse_ok_response(decoded_response);
    ExtAuthzResponse::OkResponse ok_struct;

    // Headers
    size_t size = 0;
    const auto* const* headers =
        envoy_service_auth_v3_OkHttpResponse_headers(ok_resp, &size);
    for (size_t i = 0; i < size; ++i) {
      ok_struct.headers.push_back(ParseHeaderValueOption(headers[i], &errors));
    }

    // Headers to remove
    auto headers_remove =
        envoy_service_auth_v3_OkHttpResponse_headers_to_remove(ok_resp, &size);
    for (size_t i = 0; i < size; ++i) {
      ok_struct.headers_to_remove.push_back(
          UpbStringToStdString(headers_remove[i]));
    }

    // Response headers to add
    const auto* const* resp_headers =
        envoy_service_auth_v3_OkHttpResponse_response_headers_to_add(ok_resp,
                                                                     &size);
    for (size_t i = 0; i < size; ++i) {
      ok_struct.response_headers_to_add.push_back(
          ParseHeaderValueOption(resp_headers[i], &errors));
    }

    result.ok_response = std::move(ok_struct);
  } else {
    if (envoy_service_auth_v3_CheckResponse_has_denied_response(
            decoded_response)) {
      const auto* denied =
          envoy_service_auth_v3_CheckResponse_denied_response(decoded_response);
      ExtAuthzResponse::DeniedResponse denied_struct;

      const auto* http_status =
          envoy_service_auth_v3_DeniedHttpResponse_status(denied);
      envoy_type_v3_HttpStatus_code(http_status);
      denied_struct.status = grpc_http2_status_to_grpc_status(
          envoy_type_v3_HttpStatus_code(http_status));

      size_t size = 0;
      const auto* const* headers =
          envoy_service_auth_v3_DeniedHttpResponse_headers(denied, &size);
      for (size_t i = 0; i < size; ++i) {
        denied_struct.headers.push_back(
            ParseHeaderValueOption(headers[i], &errors));
      }
      result.denied_response = std::move(denied_struct);
    }
  }
  return result;
}

}  // namespace grpc_core