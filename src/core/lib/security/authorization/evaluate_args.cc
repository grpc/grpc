//
//
// Copyright 2020 gRPC authors.
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

#include "src/core/lib/security/authorization/evaluate_args.h"

#include "src/core/lib/iomgr/parse_address.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/slice/slice_utils.h"

namespace grpc_core {

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

absl::string_view EvaluateArgs::GetLocalAddress() const {
  absl::string_view addr = grpc_endpoint_get_local_address(endpoint_);
  size_t first_colon = addr.find(":");
  size_t last_colon = addr.rfind(":");
  if (first_colon == std::string::npos || last_colon == std::string::npos) {
    return "";
  } else {
    return addr.substr(first_colon + 1, last_colon - first_colon - 1);
  }
}

int EvaluateArgs::GetLocalPort() const {
  if (endpoint_ == nullptr) {
    return 0;
  }
  grpc_uri* uri = grpc_uri_parse(
      std::string(grpc_endpoint_get_local_address(endpoint_)).c_str(), true);
  grpc_resolved_address resolved_addr;
  if (uri == nullptr || !grpc_parse_uri(uri, &resolved_addr)) {
    grpc_uri_destroy(uri);
    return 0;
  }
  grpc_uri_destroy(uri);
  return grpc_sockaddr_get_port(&resolved_addr);
}

absl::string_view EvaluateArgs::GetPeerAddress() const {
  absl::string_view addr = grpc_endpoint_get_peer(endpoint_);
  size_t first_colon = addr.find(":");
  size_t last_colon = addr.rfind(":");
  if (first_colon == std::string::npos || last_colon == std::string::npos) {
    return "";
  } else {
    return addr.substr(first_colon + 1, last_colon - first_colon - 1);
  }
}

int EvaluateArgs::GetPeerPort() const {
  if (endpoint_ == nullptr) {
    return 0;
  }
  grpc_uri* uri = grpc_uri_parse(
      std::string(grpc_endpoint_get_peer(endpoint_)).c_str(), true);
  grpc_resolved_address resolved_addr;
  if (uri == nullptr || !grpc_parse_uri(uri, &resolved_addr)) {
    grpc_uri_destroy(uri);
    return 0;
  }
  grpc_uri_destroy(uri);
  return grpc_sockaddr_get_port(&resolved_addr);
}

absl::string_view EvaluateArgs::GetSpiffeId() const {
  if (auth_context_ == nullptr) {
    return "";
  }
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context_, GRPC_PEER_SPIFFE_ID_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  if (prop == nullptr || grpc_auth_property_iterator_next(&it) != nullptr) {
    return "";
  }
  return absl::string_view(prop->value, prop->value_length);
}

absl::string_view EvaluateArgs::GetCertServerName() const {
  if (auth_context_ == nullptr) {
    return "";
  }
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context_, GRPC_X509_CN_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  if (prop == nullptr || grpc_auth_property_iterator_next(&it) != nullptr) {
    return "";
  }
  return absl::string_view(prop->value, prop->value_length);
}

}  // namespace grpc_core
