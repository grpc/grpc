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

#include <memory>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/core/ext/filters/ext_authz/ext_authz_messages.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_transport.h"

namespace grpc_core {

//
// ExtAuthzClient
//

ExtAuthzClient::ExtAuthzClient(
    std::unique_ptr<const XdsBootstrap::XdsServerTarget> server,
    RefCountedPtr<XdsTransportFactory::XdsTransport> transport)
    : DualRefCounted<ExtAuthzClient>(
          GRPC_TRACE_FLAG_ENABLED(xds_client_refcount) ? "ExtAuthzClient"
                                                       : nullptr),
      server_(std::move(server)),
      transport_(std::move(transport)) {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[ext_authz_client " << this << "] creating ext_authz client "
      << "for server " << server_->server_uri();
  GRPC_CHECK(transport_ != nullptr);
}

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
  std::string payload = CreateExtAuthzRequest(params);
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
  return ParseExtAuthzResponse(*status);
}

std::string ExtAuthzClient::CreateExtAuthzRequest(
    const ExtAuthzRequestParams& params) {
  return grpc_core::CreateExtAuthzRequest(params);
}

absl::StatusOr<ExtAuthzClient::ExtAuthzResponse>
ExtAuthzClient::ParseExtAuthzResponse(absl::string_view encoded_response) {
  return ExtAuthzResponse::Parse(encoded_response);
}

}  // namespace grpc_core