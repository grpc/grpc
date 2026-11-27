// Copyright 2025 gRPC authors.
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

#include "src/core/xds/grpc/xds_matcher_context.h"

#include <string>

#include "src/core/call/metadata_batch.h"
#include "src/core/lib/slice/slice.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

std::optional<absl::string_view> RpcMatchContext::GetHeaderValue(
    absl::string_view header_name) const {
  if (absl::EndsWith(header_name, "-bin")) {
    return std::nullopt;
  } else if (header_name == "te") {
    // As in RBAC (gRFC A41), te is hidden.
    return std::nullopt;
  } else if (header_name == "content-type") {
    return "application/grpc";
  } else if (IsAuthorityHeaderName(header_name)) {
    const Slice* authority = GetAuthorityOrHost(*initial_metadata_);
    if (authority == nullptr) return std::nullopt;
    return authority->as_string_view();
  }
  return initial_metadata_->GetStringValue(header_name, &buffer_);
}

namespace {

// Collects the names of all encodable headers in a metadata batch.
class HeaderNameCollector {
 public:
  void Encode(const Slice& key, const Slice& /*value*/) {
    names_.insert(std::string(key.as_string_view()));
  }

  template <typename Which>
  void Encode(Which, const typename Which::ValueType& /*value*/) {
    names_.insert(std::string(Which::key()));
  }

  absl::flat_hash_set<std::string>& names() { return names_; }

 private:
  absl::flat_hash_set<std::string> names_;
};

}  // namespace

void RpcMatchContext::ForEachHeaderName(
    absl::FunctionRef<void(absl::string_view)> fn) const {
  HeaderNameCollector collector;
  initial_metadata_->Encode(&collector);
  // GetHeaderValue() always returns a value for content-type.
  collector.names().insert("content-type");
  for (const std::string& name : collector.names()) {
    if (!absl::EndsWith(name, "-bin") && name != "te") fn(name);
  }
}

}  // namespace grpc_core
