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

absl::string_view EvaluateArgs::GetSpiffeId() const {
  absl::string_view spiffe_id;
  if (auth_context_ == nullptr) {
    return spiffe_id;
  }
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context_, GRPC_PEER_SPIFFE_ID_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  if (prop == nullptr) return spiffe_id;
  if (strncmp(prop->value, GRPC_PEER_SPIFFE_ID_PROPERTY_NAME,
              prop->value_length) != 0) {
    return spiffe_id;
  }
  if (grpc_auth_property_iterator_next(&it) != nullptr) return spiffe_id;
  spiffe_id = absl::string_view(
      reinterpret_cast<const char*>(prop->value, prop->value_length));
  return spiffe_id;
}

absl::string_view EvaluateArgs::GetCertServerName() const {
  absl::string_view name;
  if (auth_context_ == nullptr) {
    return name;
  }
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context_, GRPC_X509_CN_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  if (prop == nullptr) return name;
  if (strncmp(prop->value, GRPC_X509_CN_PROPERTY_NAME, prop->value_length) !=
      0) {
    return name;
  }
  if (grpc_auth_property_iterator_next(&it) != nullptr) return name;
  name = absl::string_view(
      reinterpret_cast<const char*>(prop->value, prop->value_length));
  return name;
}

}  // namespace grpc_core
