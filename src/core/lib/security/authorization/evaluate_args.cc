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

#include "src/core/lib/security/authorization/evaluate_args.h"

#include "absl/strings/numbers.h"

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/security/credentials/tls/tls_utils.h"
#include "src/core/lib/slice/slice_utils.h"

namespace grpc_core {

namespace {

EvaluateArgs::PerChannelArgs::Address ParseEndpointUri(
    absl::string_view uri_text) {
  EvaluateArgs::PerChannelArgs::Address address;
  absl::StatusOr<URI> uri = URI::Parse(uri_text);
  if (!uri.ok()) {
    gpr_log(GPR_DEBUG, "Failed to parse uri.");
    return address;
  }
  absl::string_view host_view;
  absl::string_view port_view;
  if (!SplitHostPort(uri->path(), &host_view, &port_view)) {
    gpr_log(GPR_DEBUG, "Failed to split %s into host and port.",
            uri->path().c_str());
    return address;
  }
  if (!absl::SimpleAtoi(port_view, &address.port)) {
    gpr_log(GPR_DEBUG, "Port %s is out of range or null.",
            std::string(port_view).c_str());
  }
  address.address_str = std::string(host_view);
  grpc_error_handle error = grpc_string_to_sockaddr(
      &address.address, address.address_str.c_str(), address.port);
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_DEBUG, "Address %s is not IPv4/IPv6. Error: %s",
            address.address_str.c_str(), grpc_error_std_string(error).c_str());
  }
  GRPC_ERROR_UNREF(error);
  return address;
}

}  // namespace

EvaluateArgs::PerChannelArgs::PerChannelArgs(grpc_auth_context* auth_context,
                                             grpc_endpoint* endpoint) {
  if (auth_context != nullptr) {
    transport_security_type = GetAuthPropertyValue(
        auth_context, GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME);
    spiffe_id =
        GetAuthPropertyValue(auth_context, GRPC_PEER_SPIFFE_ID_PROPERTY_NAME);
    uri_sans = GetAuthPropertyArray(auth_context, GRPC_PEER_URI_PROPERTY_NAME);
    dns_sans = GetAuthPropertyArray(auth_context, GRPC_PEER_DNS_PROPERTY_NAME);
    common_name =
        GetAuthPropertyValue(auth_context, GRPC_X509_CN_PROPERTY_NAME);
  }
  if (endpoint != nullptr) {
    local_address = ParseEndpointUri(grpc_endpoint_get_local_address(endpoint));
    peer_address = ParseEndpointUri(grpc_endpoint_get_peer(endpoint));
  }
}

absl::string_view EvaluateArgs::GetPath() const {
  absl::string_view path;
  if (metadata_ != nullptr && metadata_->idx.named.path != nullptr) {
    grpc_linked_mdelem* elem = metadata_->idx.named.path;
    const grpc_slice& val = GRPC_MDVALUE(elem->md);
    path = StringViewFromSlice(val);
  }
  return path;
}

absl::string_view EvaluateArgs::GetHost() const {
  absl::string_view host;
  if (metadata_ != nullptr && metadata_->idx.named.host != nullptr) {
    grpc_linked_mdelem* elem = metadata_->idx.named.host;
    const grpc_slice& val = GRPC_MDVALUE(elem->md);
    host = StringViewFromSlice(val);
  }
  return host;
}

absl::string_view EvaluateArgs::GetMethod() const {
  absl::string_view method;
  if (metadata_ != nullptr && metadata_->idx.named.method != nullptr) {
    grpc_linked_mdelem* elem = metadata_->idx.named.method;
    const grpc_slice& val = GRPC_MDVALUE(elem->md);
    method = StringViewFromSlice(val);
  }
  return method;
}

std::multimap<absl::string_view, absl::string_view> EvaluateArgs::GetHeaders()
    const {
  std::multimap<absl::string_view, absl::string_view> headers;
  if (metadata_ == nullptr) {
    return headers;
  }
  for (grpc_linked_mdelem* elem = metadata_->list.head; elem != nullptr;
       elem = elem->next) {
    const grpc_slice& key = GRPC_MDKEY(elem->md);
    const grpc_slice& val = GRPC_MDVALUE(elem->md);
    headers.emplace(StringViewFromSlice(key), StringViewFromSlice(val));
  }
  return headers;
}

absl::optional<absl::string_view> EvaluateArgs::GetHeaderValue(
    absl::string_view key, std::string* concatenated_value) const {
  if (metadata_ == nullptr) {
    return absl::nullopt;
  }
  return grpc_metadata_batch_get_value(metadata_, key, concatenated_value);
}

grpc_resolved_address EvaluateArgs::GetLocalAddress() const {
  if (channel_args_ == nullptr) {
    return {};
  }
  return channel_args_->local_address.address;
}

absl::string_view EvaluateArgs::GetLocalAddressString() const {
  if (channel_args_ == nullptr) {
    return "";
  }
  return channel_args_->local_address.address_str;
}

int EvaluateArgs::GetLocalPort() const {
  if (channel_args_ == nullptr) {
    return 0;
  }
  return channel_args_->local_address.port;
}

grpc_resolved_address EvaluateArgs::GetPeerAddress() const {
  if (channel_args_ == nullptr) {
    return {};
  }
  return channel_args_->peer_address.address;
}

absl::string_view EvaluateArgs::GetPeerAddressString() const {
  if (channel_args_ == nullptr) {
    return "";
  }
  return channel_args_->peer_address.address_str;
}

int EvaluateArgs::GetPeerPort() const {
  if (channel_args_ == nullptr) {
    return 0;
  }
  return channel_args_->peer_address.port;
}

absl::string_view EvaluateArgs::GetTransportSecurityType() const {
  if (channel_args_ == nullptr) {
    return "";
  }
  return channel_args_->transport_security_type;
}

absl::string_view EvaluateArgs::GetSpiffeId() const {
  if (channel_args_ == nullptr) {
    return "";
  }
  return channel_args_->spiffe_id;
}

std::vector<absl::string_view> EvaluateArgs::GetUriSans() const {
  if (channel_args_ == nullptr) {
    return {};
  }
  return channel_args_->uri_sans;
}

std::vector<absl::string_view> EvaluateArgs::GetDnsSans() const {
  if (channel_args_ == nullptr) {
    return {};
  }
  return channel_args_->dns_sans;
}

absl::string_view EvaluateArgs::GetCommonName() const {
  if (channel_args_ == nullptr) {
    return "";
  }
  return channel_args_->common_name;
}

}  // namespace grpc_core
