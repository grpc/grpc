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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_EXT_AUTHZ_EXT_AUTHZ_MESSAGES_H
#define GRPC_SRC_CORE_EXT_FILTERS_EXT_AUTHZ_EXT_AUTHZ_MESSAGES_H

#include <grpc/grpc_security.h>
#include <grpc/status.h>

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "src/core/call/evaluate_args.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/util/matchers.h"
#include "src/core/util/time.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// Represents the parsed response from an external authorization service,
// corresponding to envoy.service.auth.v3.CheckResponse in gRFC A92.
struct ExtAuthzResponse {
  // Modifications to the data plane RPC's request headers.
  struct HeaderMutation {
    // Headers to append or set on the data plane RPC's request headers
    // (from OkHttpResponse.headers).
    std::vector<XdsHeaderValueOption> set_headers;
    // Request headers to remove from the data plane RPC
    // (from OkHttpResponse.headers_to_remove).
    std::vector<std::string> remove_headers;
  };

  // Represents an OK response (envoy.service.auth.v3.OkHttpResponse) allowing
  // the data plane RPC.
  struct OkResponse {
    // Modifications to the data plane RPC's request headers
    // (from OkHttpResponse.headers and OkHttpResponse.headers_to_remove).
    HeaderMutation header_mutation;
    // Modifications to the data plane RPC's response headers
    // (from OkHttpResponse.response_headers_to_add).
    std::vector<XdsHeaderValueOption> response_headers_to_add;
  };

  // Represents a denied response (envoy.service.auth.v3.DeniedHttpResponse)
  // rejecting the data plane RPC.
  struct DeniedResponse {
    // The gRPC status code to fail the RPC with, converted from the HTTP status
    // in DeniedHttpResponse.status using standard HTTP-to-gRPC status mapping
    // rules. Defaults to GRPC_STATUS_PERMISSION_DENIED if status is not set
    // (per Envoy's default HTTP 403 Forbidden).
    grpc_status_code status = GRPC_STATUS_PERMISSION_DENIED;
    // In gRPC, failing an RPC involves sending a Trailers-Only response, so
    // this field is used to modify response trailers on the data plane RPC
    // rather than response headers (from DeniedHttpResponse.headers). Note:
    // DeniedHttpResponse.body is ignored as it does not apply to gRPC.
    std::vector<XdsHeaderValueOption> headers;
  };

  // The variant representing the actual response content.
  // It can hold OkResponse, DeniedResponse, or std::monostate if no response
  // is set.
  using ResponseValue =
      std::variant<std::monostate, OkResponse, DeniedResponse>;

  ResponseValue response;

  // The overall status of the CheckResponse (from CheckResponse.status).
  // If this is an OK status, the data plane RPC will be allowed; otherwise, it
  // will be rejected.
  // Note that if the RPC is rejected, this field does not indicate the status
  // with which to fail the RPC (which comes from DeniedResponse.status).
  // However, implementations should include the text of this status in the RPC
  // failure status message.
  grpc_status_code status_code = GRPC_STATUS_OK;

  // Status message from CheckResponse.status. Included in the RPC failure
  // status message when the RPC is rejected.
  std::string status_message;

  // Parses a serialized envoy.service.auth.v3.CheckResponse proto.
  //
  // Parameters:
  //  - serialized_response: The raw string containing the protobuf-serialized
  //    CheckResponse.
  //
  // Returns:
  //  The parsed ExtAuthzResponse structure representing an OK or denied
  //  response, or an absl::Status error if deserialization or validation fails.
  static absl::StatusOr<ExtAuthzResponse> Parse(
      absl::string_view serialized_response);
};

// Parameters used to construct the envoy.service.auth.v3.AttributeContext
// message within envoy.service.auth.v3.CheckRequest according to gRFC A92
// specifications.
struct ExtAuthzRequest {
  // True if the call is made on the gRPC client side.
  // When true, source and destination peers are omitted from AttributeContext.
  bool is_client_call = false;

  // RPC path (AttributeContext.HttpRequest.path). Always set.
  std::string path;

  // Metadata batch from the data plane RPC. Used to populate
  // AttributeContext.HttpRequest.header_map subject to header filtering rules.
  const grpc_metadata_batch* metadata = nullptr;

  // RPC start time (AttributeContext.Request.time).
  // If not recorded, current time when ext_authz sees the request headers is
  // used.
  std::optional<Timestamp> start_time;

  // Connection endpoints & TLS information, used only on the gRPC server side
  // (i.e., when is_client_call is false) to populate:
  // - AttributeContext.source.address: the peer address of the connection that
  //   the request came in on (EvaluateArgs::GetPeerAddress()).
  // - AttributeContext.source.principal: the peer certificate's first URI SAN
  //   if set, otherwise its first DNS SAN if set, otherwise its subject in
  //   RFC 2253 format (EvaluateArgs::GetUriSans(), GetDnsSans(), GetSubject()).
  //   Unset if TLS is not used or the client did not provide a certificate.
  // - AttributeContext.destination.address: the local address of the
  //   connection that the request came in on (EvaluateArgs::GetLocalAddress()).
  // - AttributeContext.destination.principal: the identity asserted by the
  //   certificate that this server presented on this connection: its first
  //   URI SAN if set, otherwise its first DNS SAN if set, otherwise its
  //   subject in RFC 2253 format (EvaluateArgs::GetLocalUriSan(),
  //   GetLocalDnsSan(), GetLocalSubject()).  Unset if TLS is not used or this
  //   server presented no certificate.
  // Addresses that EvaluateArgs cannot resolve (e.g., unix domain sockets) are
  // omitted.
  // If null, source and destination are omitted from the AttributeContext.
  // Note: AttributeContext.{service,labels} are not set.
  const EvaluateArgs* args = nullptr;

  // Header matching rules for populating
  // AttributeContext.HttpRequest.header_map:
  // For each request header on the data plane RPC:
  // - If matched by disallowed_headers, it will not be added.
  // - Otherwise, if allowed_headers is unset or matches, it will be added.
  // - Otherwise, excluded.
  std::vector<StringMatcher> allowed_headers;
  std::vector<StringMatcher> disallowed_headers;

  // Value for AttributeContext.source.certificate, as returned by
  // GetUrlEncodedPemPeerCertificate(). Must be left empty unless the ext_authz
  // config sets include_peer_certificate. Ignored on the client side.
  // This is a non-owning view; the caller must keep the underlying string
  // alive for the duration of the CreateExtAuthzRequest() call. (The value is
  // computed once per connection, so it is passed by reference rather than
  // copied into this struct on every RPC.)
  absl::string_view peer_certificate;
};

// Computes the value of the AttributeContext.source.certificate field (see
// gRFC A92): the URL-encoded PEM-encoded peer certificate, which is obtained
// from \a auth_context. Returns an empty string if there is no peer
// certificate.
//
// Note that this requires copying and encoding the peer certificate, so
// callers must invoke this at most once per connection, and only if the
// ext_authz config sets include_peer_certificate.
std::string GetUrlEncodedPemPeerCertificate(grpc_auth_context* auth_context);

absl::StatusOr<std::string> CreateExtAuthzRequest(
    const ExtAuthzRequest& request);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_EXT_AUTHZ_EXT_AUTHZ_MESSAGES_H
