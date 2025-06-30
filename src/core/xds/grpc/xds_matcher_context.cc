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

namespace grpc_core {

std::optional<absl::string_view> RpcMatchContext::GetHeaderValue(
    absl::string_view header_name, std::string* concatenated_value) const {
  if (absl::EndsWith(header_name, "-bin")) {
    return std::nullopt;
  } else if (header_name == "content-type") {
    return "application/grpc";
  }
  return initial_metadata->GetStringValue(header_name, concatenated_value);
}

}  // namespace grpc_core
