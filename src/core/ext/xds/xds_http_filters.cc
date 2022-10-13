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
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "envoy/extensions/filters/http/router/v3/router.upb.h"
#include "envoy/extensions/filters/http/router/v3/router.upbdefs.h"

#include "src/core/ext/xds/xds_http_fault_filter.h"
#include "src/core/ext/xds/xds_http_rbac_filter.h"

namespace grpc_core {

const char* kXdsHttpRouterFilterConfigName =
    "envoy.extensions.filters.http.router.v3.Router";

namespace {

class XdsHttpRouterFilter : public XdsHttpFilterImpl {
 public:
  void PopulateSymtab(upb_DefPool* symtab) const override {
    envoy_extensions_filters_http_router_v3_Router_getmsgdef(symtab);
  }

  absl::StatusOr<FilterConfig> GenerateFilterConfig(
      upb_StringView serialized_filter_config,
      upb_Arena* arena) const override {
    if (envoy_extensions_filters_http_router_v3_Router_parse(
            serialized_filter_config.data, serialized_filter_config.size,
            arena) == nullptr) {
      return absl::InvalidArgumentError("could not parse router filter config");
    }
    return FilterConfig{kXdsHttpRouterFilterConfigName, Json()};
  }

  absl::StatusOr<FilterConfig> GenerateFilterConfigOverride(
      upb_StringView /*serialized_filter_config*/,
      upb_Arena* /*arena*/) const override {
    return absl::InvalidArgumentError(
        "router filter does not support config override");
  }

  const grpc_channel_filter* channel_filter() const override { return nullptr; }

  // No-op.  This will never be called, since channel_filter() returns null.
  absl::StatusOr<ServiceConfigJsonEntry> GenerateServiceConfig(
      const FilterConfig& /*hcm_filter_config*/,
      const FilterConfig* /*filter_config_override*/) const override {
    return absl::UnimplementedError("router filter should never be called");
  }

  bool IsSupportedOnClients() const override { return true; }

  bool IsSupportedOnServers() const override { return true; }

  bool IsTerminalFilter() const override { return true; }
};

}  // namespace

XdsHttpFilterRegistry::XdsHttpFilterRegistry() {
  // TODO(roth): Restructure this such that filters have a method that
  // returns the proto message names under which they should be registered.
  RegisterFilter(std::make_unique<XdsHttpRouterFilter>(),
                 {kXdsHttpRouterFilterConfigName});
  RegisterFilter(std::make_unique<XdsHttpFaultFilter>(),
                 {kXdsHttpFaultFilterConfigName});
  RegisterFilter(
      std::make_unique<XdsHttpRbacFilter>(),
      {kXdsHttpRbacFilterConfigName, kXdsHttpRbacFilterConfigOverrideName});
}

void XdsHttpFilterRegistry::RegisterFilter(
    std::unique_ptr<XdsHttpFilterImpl> filter,
    const std::set<absl::string_view>& config_proto_type_names) {
  for (auto config_proto_type_name : config_proto_type_names) {
    registry_map_[config_proto_type_name] = filter.get();
  }
  owning_list_.push_back(std::move(filter));
}

const XdsHttpFilterImpl* XdsHttpFilterRegistry::GetFilterForType(
    absl::string_view proto_type_name) const {
  auto it = registry_map_.find(proto_type_name);
  if (it == registry_map_.end()) return nullptr;
  return it->second;
}

void XdsHttpFilterRegistry::PopulateSymtab(upb_DefPool* symtab) const {
  for (const auto& filter : owning_list_) {
    filter->PopulateSymtab(symtab);
  }
}

}  // namespace grpc_core
