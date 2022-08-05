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

#include "src/core/ext/xds/xds_http_filters.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "envoy/extensions/filters/http/router/v3/router.upb.h"
#include "envoy/extensions/filters/http/router/v3/router.upbdefs.h"

namespace grpc_core {

//
// XdsHttpRouterFilter
//

absl::string_view XdsHttpRouterFilter::ConfigProtoType() const {
  return "envoy.extensions.filters.http.router.v3.Router";
}

absl::string_view XdsHttpRouterFilter::OverrideConfigProtoType() const {
  return "";
}

void XdsHttpRouterFilter::PopulateSymtab(upb_DefPool* symtab) const {
  envoy_extensions_filters_http_router_v3_Router_getmsgdef(symtab);
}

absl::StatusOr<XdsHttpFilter::FilterConfig>
XdsHttpRouterFilter::GenerateFilterConfig(
    upb_StringView serialized_filter_config, upb_Arena* arena) const {
  if (envoy_extensions_filters_http_router_v3_Router_parse(
          serialized_filter_config.data, serialized_filter_config.size,
          arena) == nullptr) {
    return absl::InvalidArgumentError("could not parse router filter config");
  }
  return FilterConfig{ConfigProtoType(), Json()};
}

absl::StatusOr<XdsHttpFilter::FilterConfig>
XdsHttpRouterFilter::GenerateFilterConfigOverride(
    upb_StringView /*serialized_filter_config*/, upb_Arena* /*arena*/) const {
  return absl::InvalidArgumentError(
      "router filter does not support config override");
}

//
// XdsHttpFilterRegistry
//

void XdsHttpFilterRegistry::RegisterFilter(
    std::unique_ptr<XdsHttpFilter> filter) {
  filter_registry_[filter->ConfigProtoType()] = filter.get();
  auto override_config_type = filter->OverrideConfigProtoType();
  if (!override_config_type.empty()) {
    filter_registry_[override_config_type] = filter.get();
  }
  filters_.push_back(std::move(filter));
}

const XdsHttpFilter* XdsHttpFilterRegistry::GetFilterForType(
    absl::string_view proto_type_name) const {
  auto it = filter_registry_.find(proto_type_name);
  if (it == filter_registry_.end()) return nullptr;
  return it->second;
}

void XdsHttpFilterRegistry::PopulateSymtab(upb_DefPool* symtab) const {
  for (const auto& filter : filters_) {
    filter->PopulateSymtab(symtab);
  }
}

}  // namespace grpc_core
