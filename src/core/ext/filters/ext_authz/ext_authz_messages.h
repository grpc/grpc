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

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "envoy/service/auth/v3/attribute_context.upb.h"
#include "envoy/service/auth/v3/external_auth.upb.h"
#include "upb/mem/arena.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/util/matchers.h"
#include "src/core/util/time.h"
#include "src/core/xds/grpc/xds_common_types.h"

// Data structures and message creation/parsing helpers for the xDS External
// Authorization (ext_authz) filter in gRPC, as specified in gRFC A92.

namespace grpc_core {

// Represents the parsed response from an external authorization service,
// corresponding to envoy.service.auth.v3.CheckResponse in gRFC A92.
struct ExtAuthzResponse {
  struct HeaderMutation {
    std::vector<XdsHeaderValueOption> set_headers;
    std::vector<std::string> remove_headers;
  };

  struct OkResponse {
    HeaderMutation header_mutation;
    std::vector<XdsHeaderValueOption> response_headers_to_add;
  };

  struct DeniedResponse {
    grpc_status_code status = GRPC_STATUS_UNKNOWN;
    std::vector<XdsHeaderValueOption> headers;
  };

  // The variant representing the actual response content.
  // It can hold OkResponse, DeniedResponse, or std::monostate if no response
  // is set.
  using ResponseValue =
      std::variant<std::monostate, OkResponse, DeniedResponse>;

  ResponseValue response;
  grpc_status_code status_code = GRPC_STATUS_OK;
  std::string status_message;

  // Parses a serialized envoy.service.auth.v3.CheckResponse proto.
  //
  // Parameters:
  //  - serialized_response: The raw string containing the protobuf-serialized
  //  CheckResponse.
  //
  // Returns:
  //  The parsed ExtAuthzResponse structure representing an OK or denied
  //  response, or an absl::Status error if deserialization or validation fails.
  static absl::StatusOr<ExtAuthzResponse> Parse(
      absl::string_view serialized_response);
};

struct ExtAuthzPeer {
  // Address of the endpoint.
  std::optional<grpc_resolved_address> address;

  // TLS SANs and subject for principal resolution.
  // Priority: first URI SAN -> first DNS SAN -> subject in RFC 2253 format.
  std::vector<std::string> uri_sans;
  std::vector<std::string> dns_sans;
  std::string subject;

  // Peer certificate (e.g. PEM or certificate data).
  std::string certificate;
};

struct ExtAuthzRequestParams {
  bool is_client_call = false;
  std::string path;
  std::vector<std::pair<std::string, std::string>> headers;
  std::optional<Timestamp> start_time;

  // Connection endpoints & TLS information (for server-side calls).
  ExtAuthzPeer peer;        // source
  ExtAuthzPeer local;       // destination

  // Header matching rules
  std::vector<StringMatcher> allowed_headers;
  std::vector<StringMatcher> disallowed_headers;

  // If true, peer certificate is included in source.certificate.
  bool include_peer_certificate = false;
};

// Constructs an envoy.service.auth.v3.AttributeContext upb message according
// to gRFC A92 specifications.
envoy_service_auth_v3_AttributeContext* CreateAttributeContext(
    upb_Arena* arena, const ExtAuthzRequestParams& params);

// Constructs an envoy.service.auth.v3.CheckRequest upb message wrapping
// the AttributeContext message according to gRFC A92 specifications.
envoy_service_auth_v3_CheckRequest* CreateExtAuthzCheckRequest(
    upb_Arena* arena, const ExtAuthzRequestParams& params);

// Serializes the CheckRequest message to a binary protobuf string.
std::string CreateExtAuthzRequest(const ExtAuthzRequestParams& params);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_EXT_AUTHZ_EXT_AUTHZ_MESSAGES_H
