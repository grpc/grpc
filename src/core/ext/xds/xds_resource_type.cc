//
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
//

#include <grpc/support/port_platform.h>

#include "src/core/ext/xds/xds_resource_type.h"

#include <vector>

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {

XdsResourceTypeRegistry* XdsResourceTypeRegistry::g_registry_ = nullptr;

absl::StatusOr<XdsResourceName> ParseXdsResourceName(absl::string_view name,
                                                     const XdsResourceType* type) {
  // Old-style names use the empty string for authority.
  // authority is prefixed with "old:" to indicate that it's an old-style name.
  if (!absl::StartsWith(name, "xdstp:")) {
    return XdsResourceName{"old:", std::string(name)};
  }
  // New style name.  Parse URI.
  auto uri = URI::Parse(name);
  if (!uri.ok()) return uri.status();
  // Split the resource type off of the path to get the id.
  std::pair<absl::string_view, absl::string_view> path_parts =
      absl::StrSplit(uri->path(), absl::MaxSplits('/', 1));
  if (!type->IsType(path_parts.first, nullptr)) {
    return absl::InvalidArgumentError(
        "xdstp URI path must indicate valid xDS resource type");
  }
  std::vector<std::pair<absl::string_view, absl::string_view>> query_parameters(
      uri->query_parameter_map().begin(), uri->query_parameter_map().end());
  std::sort(query_parameters.begin(), query_parameters.end());
  return XdsResourceName{
      absl::StrCat("xdstp:", uri->authority()),
      absl::StrCat(
          path_parts.second, (query_parameters.empty() ? "?" : ""),
          absl::StrJoin(query_parameters, "&", absl::PairFormatter("=")))};
}

std::string ConstructFullXdsResourceName(absl::string_view authority,
                                         absl::string_view resource_type,
                                         absl::string_view id) {
  if (absl::ConsumePrefix(&authority, "xdstp:")) {
    return absl::StrCat("xdstp://", authority, "/", resource_type, "/", id);
  }
  return std::string(id);
}

}  // namespace grpc_core
