// Copyright 2021 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/authorization/evaluate_channel_args.h"

#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/parse_address.h"

namespace grpc_core {

namespace {

absl::string_view GetAuthPropertyValue(grpc_auth_context* context,
                                       const char* property_name) {
  grpc_auth_property_iterator it =
      grpc_auth_context_find_properties_by_name(context, property_name);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  if (prop == nullptr) {
    gpr_log(GPR_DEBUG, "No value found for %s property.", property_name);
    return "";
  }
  if (grpc_auth_property_iterator_next(&it) != nullptr) {
    gpr_log(GPR_DEBUG, "Multiple values found for %s property.", property_name);
    return "";
  }
  return absl::string_view(prop->value, prop->value_length);
}

void ParseEndpointUri(absl::string_view uri_text, std::string* address,
                      int* port) {
  absl::StatusOr<URI> uri = URI::Parse(uri_text);
  if (!uri.ok()) {
    gpr_log(GPR_DEBUG, "Failed to parse uri.");
    return;
  }
  std::string port_str;
  if (!SplitHostPort(uri->path(), address, &port_str)) {
    gpr_log(GPR_DEBUG, "Failed to obtain host and port from %s.",
            uri->path().c_str());
    return;
  }
  if (port_str.empty()) {
    gpr_log(GPR_DEBUG, "No port in %s.", uri->path().c_str());
    return;
  }
  if (!absl::SimpleAtoi(port_str, port)) {
    gpr_log(GPR_DEBUG, "port %s is out of range.", port_str.c_str());
  }
}

}  // namespace

EvaluateChannelArgs::EvaluateChannelArgs(grpc_auth_context* auth_context,
                                         grpc_endpoint* endpoint) {
  if (auth_context != nullptr) {
    transport_security_type_ = std::string(GetAuthPropertyValue(
        auth_context, GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME));
    spiffe_id_ = std::string(
        GetAuthPropertyValue(auth_context, GRPC_PEER_SPIFFE_ID_PROPERTY_NAME));
    common_name_ = std::string(
        GetAuthPropertyValue(auth_context, GRPC_X509_CN_PROPERTY_NAME));
  }
  if (endpoint != nullptr) {
    ParseEndpointUri(grpc_endpoint_get_local_address(endpoint), &local_address_,
                     &local_port_);
    ParseEndpointUri(grpc_endpoint_get_peer(endpoint), &peer_address_,
                     &peer_port_);
  }
}

}  // namespace grpc_core
