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

#include "envoy/extensions/filters/http/router/v3/router.upbdefs.h"

namespace grpc_core {

const char* kXdsHttpRouterFilterConfigName =
    "extensions.filters.http.router.v3.Router";

namespace {

class XdsHttpRouterFilter : public XdsHttpFilterImpl {
 public:
  absl::string_view config_proto_type_name() const override {
    return kXdsHttpRouterFilterConfigName;
  }

  void PopulateSymtab(upb_symtab* symtab) const override {
    envoy_extensions_filters_http_router_v3_Router_getmsgdef(symtab);
  }

  absl::StatusOr<FilterConfig> GenerateFilterConfig(
      upb_strview serialized_xds_config, upb_arena* arena) const override {
    return FilterConfig{kXdsHttpRouterFilterConfigName, Json()};
  }

  absl::StatusOr<FilterConfig> GenerateFilterConfigOverride(
      upb_strview serialized_xds_config, upb_arena* arena) const override {
    return FilterConfig{kXdsHttpRouterFilterConfigName, Json()};
  }

  // No-op -- this filter is special-cased by the xds resolver.
  const grpc_channel_filter* channel_filter() const override {
    return nullptr;
  }

  // No-op -- this filter is special-cased by the xds resolver.
  absl::StatusOr<std::string> GenerateServiceConfig(
        const FilterConfig& hcm_filter_config,
        const FilterConfig* filter_config_override) const override {
    return "";
  }
};

using FilterRegistryMap =
    std::map<absl::string_view, std::unique_ptr<XdsHttpFilterImpl>>;

FilterRegistryMap* g_filter_registry = nullptr;

}  // namespace

void XdsHttpFilterRegistry::RegisterFilter(
    std::unique_ptr<XdsHttpFilterImpl> filter) {
  (*g_filter_registry)[filter->config_proto_type_name()] = std::move(filter);
}

const XdsHttpFilterImpl* XdsHttpFilterRegistry::GetFilterForType(
    absl::string_view proto_type_name) {
  auto it = g_filter_registry->find(proto_type_name);
  if (it == g_filter_registry->end()) return nullptr;
  return it->second.get();
}

void XdsHttpFilterRegistry::PopulateSymtab(upb_symtab* symtab) {
  for (const auto& p : *g_filter_registry) {
    p.second->PopulateSymtab(symtab);
  }
}

void XdsHttpFilterRegistry::Init() {
  g_filter_registry = new FilterRegistryMap;
  RegisterFilter(absl::make_unique<XdsHttpRouterFilter>());
}

void XdsHttpFilterRegistry::Shutdown() { delete g_filter_registry; }

}  // namespace grpc_core
