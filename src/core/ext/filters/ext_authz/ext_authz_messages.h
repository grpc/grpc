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

#include <grpc/status.h>

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "src/core/lib/iomgr/resolved_address.h"
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
    // rules.
    grpc_status_code status = GRPC_STATUS_UNKNOWN;
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
  // Represents peer endpoint and credential attributes
  // (envoy.service.auth.v3.AttributeContext.Peer).
  struct Peer {
    // Address of the connection endpoint (AttributeContext.Peer.address).
    // For source, peer address of the connection that the request came in on.
    // For destination, local address of the connection that the request came in
    // on.
    std::optional<grpc_resolved_address> address;

    // TLS SANs and subject for principal resolution
    // (AttributeContext.Peer.principal).
    // If TLS is used (and for source, the client provided a valid certificate),
    // this will be set to the certificate's first URI SAN if set, otherwise
    // the certificate's first DNS SAN if set, otherwise the subject field of
    // the certificate in RFC 2253 format. If TLS is not used (or for source, no
    // cert was provided), principal will be unset.
    std::vector<std::string> uri_sans;
    std::vector<std::string> dns_sans;
    std::string subject;

    // Peer certificate (AttributeContext.Peer.certificate).
    // Populated for source if the include_peer_certificate config field is set
    // to true. Unset for destination.
    std::string certificate;
  };

  // True if the call is made on the gRPC client side.
  // When true, source and destination peers are omitted from AttributeContext.
  bool is_client_call = false;

  // RPC path (AttributeContext.HttpRequest.path). Always set.
  std::string path;

  // Request headers from the data plane RPC. Used to populate
  // AttributeContext.HttpRequest.header_map subject to header filtering rules.
  std::vector<std::pair<std::string, std::string>> headers;

  // RPC start time (AttributeContext.Request.time).
  // If not recorded, current time when ext_authz sees the request headers is
  // used.
  std::optional<Timestamp> start_time;

  // Connection endpoints & TLS information. Set only on the gRPC server side:
  // - source (AttributeContext.source): peer address and client credentials.
  // - destination (AttributeContext.destination): local address and server
  //   credentials.
  // Note: service and labels are not set.
  Peer source;
  Peer destination;

  // Header matching rules for populating
  // AttributeContext.HttpRequest.header_map:
  // For each request header on the data plane RPC:
  // - If matched by disallowed_headers, it will not be added.
  // - Otherwise, if allowed_headers is unset or matches, it will be added.
  // - Otherwise, excluded.
  std::vector<StringMatcher> allowed_headers;
  std::vector<StringMatcher> disallowed_headers;

  // If true, peer certificate is included in source.certificate.
  bool include_peer_certificate = false;
};

// Serializes the CheckRequest message to a binary protobuf string.
// Wraps the AttributeContext created from request into an
// envoy.service.auth.v3.CheckRequest and serializes it.
std::string CreateExtAuthzRequest(const ExtAuthzRequest& request);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_EXT_AUTHZ_EXT_AUTHZ_MESSAGES_H
