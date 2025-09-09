//
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
//

#include "src/core/xds/grpc/xds_http_filter.h"

namespace grpc_core {

constexpr int kMaxDepth = 8;

std::optional<XdsHttpFilterImpl::FilterConfig>
XdsHttpFilterImpl::GenerateFilterConfig(
    absl::string_view instance_name,
    const XdsResourceType::DecodeContext& context, XdsExtension extension,
    int recursion_depth, std::set<std::string>* ecds_resources_needed,
    ValidationErrors* errors) const {
  if (recursion_depth >= kMaxDepth) {
    errors->AddError("hit max filter config recursion depth (8)");
    return std::nullopt;
  }
  return GenerateFilterConfigImpl(instance_name, context, std::move(extension),
                                  recursion_depth + 1, ecds_resources_needed,
                                  errors);
}

std::optional<XdsHttpFilterImpl::FilterConfig>
XdsHttpFilterImpl::GenerateFilterConfigOverride(
    absl::string_view instance_name,
    const XdsResourceType::DecodeContext& context, XdsExtension extension,
    int recursion_depth, std::set<std::string>* ecds_resources_needed,
    ValidationErrors* errors) const {
  if (recursion_depth >= kMaxDepth) {
    errors->AddError("hit max filter config recursion depth (8)");
    return std::nullopt;
  }
  return GenerateFilterConfigOverrideImpl(
      instance_name, context, std::move(extension), recursion_depth + 1,
      ecds_resources_needed, errors);
}

}  // namespace grpc_core
