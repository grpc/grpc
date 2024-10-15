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

#include "src/core/lib/security/authorization/evaluate_args.h"

#include <grpc/grpc_security_constants.h>
#include <grpc/support/port_platform.h>
#include <string.h>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "src/core/handshaker/endpoint_info/endpoint_info_handshaker.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/security/credentials/tls/tls_utils.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/util/host_port.h"
#include "src/core/util/uri.h"

namespace grpc_core {

namespace {

EvaluateArgs::PerChannelArgs::Address ParseEndpointUri(
    absl::string_view uri_text) {
  EvaluateArgs::PerChannelArgs::Address address;
  absl::StatusOr<URI> uri = URI::Parse(uri_text);
  if (!uri.ok()) {
    VLOG(2) << "Failed to parse uri.";
    return address;
  }
  absl::string_view host_view;
  absl::string_view port_view;
  if (!SplitHostPort(uri->path(), &host_view, &port_view)) {
    VLOG(2) << "Failed to split " << uri->path() << " into host and port.";
    return address;
  }
  if (!absl::SimpleAtoi(port_view, &address.port)) {
    VLOG(2) << "Port " << port_view << " is out of range or null.";
  }
  address.address_str = std::string(host_view);
  auto resolved_address = StringToSockaddr(uri->path());
  if (!resolved_address.ok()) {
    VLOG(2) << "Address \"" << uri->path()
            << "\" is not IPv4/IPv6. Error: " << resolved_address.status();
    memset(&address.address, 0, sizeof(address.address));
  } else {
    address.address = *resolved_address;
  }
  return address;
}

}  // namespace

EvaluateArgs::PerChannelArgs::PerChannelArgs(grpc_auth_context* auth_context,
                                             const ChannelArgs& args) {
  if (auth_context != nullptr) {
    transport_security_type = GetAuthPropertyValue(
        auth_context, GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME);
    spiffe_id =
        GetAuthPropertyValue(auth_context, GRPC_PEER_SPIFFE_ID_PROPERTY_NAME);
    uri_sans = GetAuthPropertyArray(auth_context, GRPC_PEER_URI_PROPERTY_NAME);
    dns_sans = GetAuthPropertyArray(auth_context, GRPC_PEER_DNS_PROPERTY_NAME);
    common_name =
        GetAuthPropertyValue(auth_context, GRPC_X509_CN_PROPERTY_NAME);
    subject =
        GetAuthPropertyValue(auth_context, GRPC_X509_SUBJECT_PROPERTY_NAME);
  }
  local_address = ParseEndpointUri(
      args.GetString(GRPC_ARG_ENDPOINT_LOCAL_ADDRESS).value_or(""));
  peer_address = ParseEndpointUri(
      args.GetString(GRPC_ARG_ENDPOINT_PEER_ADDRESS).value_or(""));
}

absl::string_view EvaluateArgs::GetPath() const {
  if (metadata_ != nullptr) {
    const auto* path = metadata_->get_pointer(HttpPathMetadata());
    if (path != nullptr) {
      return path->as_string_view();
    }
  }
  return absl::string_view();
}

absl::string_view EvaluateArgs::GetAuthority() const {
  absl::string_view authority;
  if (metadata_ != nullptr) {
    if (auto* authority_md = metadata_->get_pointer(HttpAuthorityMetadata())) {
      authority = authority_md->as_string_view();
    }
  }
  return authority;
}

absl::string_view EvaluateArgs::GetMethod() const {
  if (metadata_ != nullptr) {
    auto method_md = metadata_->get(HttpMethodMetadata());
    if (method_md.has_value()) {
      return HttpMethodMetadata::Encode(*method_md).as_string_view();
    }
  }
  return absl::string_view();
}

absl::optional<absl::string_view> EvaluateArgs::GetHeaderValue(
    absl::string_view key, std::string* concatenated_value) const {
  if (metadata_ == nullptr) {
    return absl::nullopt;
  }
  if (absl::EqualsIgnoreCase(key, "te")) {
    return absl::nullopt;
  }
  if (absl::EqualsIgnoreCase(key, "host")) {
    // Maps legacy host header to :authority.
    return GetAuthority();
  }
  return metadata_->GetStringValue(key, concatenated_value);
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

absl::string_view EvaluateArgs::GetSubject() const {
  if (channel_args_ == nullptr) {
    return "";
  }
  return channel_args_->subject;
}

}  // namespace grpc_core
