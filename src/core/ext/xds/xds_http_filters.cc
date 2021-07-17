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

#include "envoy/extensions/filters/http/router/v3/router.upb.h"
#include "envoy/extensions/filters/http/router/v3/router.upbdefs.h"
#include "src/core/ext/xds/xds_http_fault_filter.h"

namespace grpc_core {

const char* kXdsHttpRouterFilterConfigName =
    "envoy.extensions.filters.http.router.v3.Router";

namespace {

class XdsHttpRouterFilter : public XdsHttpFilterImpl {
 public:
  void PopulateSymtab(upb_symtab* symtab) const override {
    envoy_extensions_filters_http_router_v3_Router_getmsgdef(symtab);
  }

  absl::StatusOr<FilterConfig> GenerateFilterConfig(
      upb_strview serialized_filter_config, upb_arena* arena) const override {
    if (envoy_extensions_filters_http_router_v3_Router_parse(
            serialized_filter_config.data, serialized_filter_config.size,
            arena) == nullptr) {
      return absl::InvalidArgumentError("could not parse router filter config");
    }
    return FilterConfig{kXdsHttpRouterFilterConfigName, Json()};
  }

  absl::StatusOr<FilterConfig> GenerateFilterConfigOverride(
      upb_strview /*serialized_filter_config*/,
      upb_arena* /*arena*/) const override {
    return absl::InvalidArgumentError(
        "router filter does not support config override");
  }

  // No-op -- this filter is special-cased by the xds resolver.
  const grpc_channel_filter* channel_filter() const override { return nullptr; }

  // No-op -- this filter is special-cased by the xds resolver.
  absl::StatusOr<ServiceConfigJsonEntry> GenerateServiceConfig(
      const FilterConfig& /*hcm_filter_config*/,
      const FilterConfig* /*filter_config_override*/) const override {
    return absl::UnimplementedError("router filter should never be called");
  }

  bool IsSupportedOnClients() const override { return true; }

  bool IsSupportedOnServers() const override { return true; }
};

using FilterOwnerList = std::vector<std::unique_ptr<XdsHttpFilterImpl>>;
using FilterRegistryMap = std::map<absl::string_view, XdsHttpFilterImpl*>;

FilterOwnerList* g_filters = nullptr;
FilterRegistryMap* g_filter_registry = nullptr;

}  // namespace

void XdsHttpFilterRegistry::RegisterFilter(
    std::unique_ptr<XdsHttpFilterImpl> filter,
    const std::set<absl::string_view>& config_proto_type_names) {
  for (auto config_proto_type_name : config_proto_type_names) {
    (*g_filter_registry)[config_proto_type_name] = filter.get();
  }
  g_filters->push_back(std::move(filter));
}

const XdsHttpFilterImpl* XdsHttpFilterRegistry::GetFilterForType(
    absl::string_view proto_type_name) {
  auto it = g_filter_registry->find(proto_type_name);
  if (it == g_filter_registry->end()) return nullptr;
  return it->second;
}

void XdsHttpFilterRegistry::PopulateSymtab(upb_symtab* symtab) {
  for (const auto& filter : *g_filters) {
    filter->PopulateSymtab(symtab);
  }
}

void XdsHttpFilterRegistry::Init() {
  g_filters = new FilterOwnerList;
  g_filter_registry = new FilterRegistryMap;
  RegisterFilter(absl::make_unique<XdsHttpRouterFilter>(),
                 {kXdsHttpRouterFilterConfigName});
  RegisterFilter(absl::make_unique<XdsHttpFaultFilter>(),
                 {kXdsHttpFaultFilterConfigName});
}

void XdsHttpFilterRegistry::Shutdown() {
  delete g_filter_registry;
  delete g_filters;
}

}  // namespace grpc_core
