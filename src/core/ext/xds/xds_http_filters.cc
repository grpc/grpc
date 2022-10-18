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
#include "absl/types/variant.h"
#include "envoy/extensions/filters/http/router/v3/router.upb.h"
#include "envoy/extensions/filters/http/router/v3/router.upbdefs.h"

#include "src/core/ext/xds/xds_http_fault_filter.h"
#include "src/core/ext/xds/xds_http_rbac_filter.h"

namespace grpc_core {

//
// XdsHttpRouterFilter
//

absl::string_view XdsHttpRouterFilter::StaticConfigName() {
  return "envoy.extensions.filters.http.router.v3.Router";
}

absl::string_view XdsHttpRouterFilter::ConfigProtoName() const {
  return StaticConfigName();
}

absl::string_view XdsHttpRouterFilter::OverrideConfigProtoName() const {
  return "";
}

void XdsHttpRouterFilter::PopulateSymtab(upb_DefPool* symtab) const {
  envoy_extensions_filters_http_router_v3_Router_getmsgdef(symtab);
}

absl::optional<XdsHttpFilterImpl::FilterConfig>
XdsHttpRouterFilter::GenerateFilterConfig(XdsExtension extension,
                                          upb_Arena* arena,
                                          ValidationErrors* errors) const {
  absl::string_view* serialized_filter_config =
      absl::get_if<absl::string_view>(&extension.value);
  if (serialized_filter_config == nullptr) {
    errors->AddError("could not parse router filter config");
    return absl::nullopt;
  }
  if (envoy_extensions_filters_http_router_v3_Router_parse(
          serialized_filter_config->data(), serialized_filter_config->size(),
          arena) == nullptr) {
    errors->AddError("could not parse router filter config");
    return absl::nullopt;
  }
  return FilterConfig{ConfigProtoName(), Json()};
}

absl::optional<XdsHttpFilterImpl::FilterConfig>
XdsHttpRouterFilter::GenerateFilterConfigOverride(
    XdsExtension /*extension*/, upb_Arena* /*arena*/,
    ValidationErrors* errors) const {
  errors->AddError("router filter does not support config override");
  return absl::nullopt;
}

//
// XdsHttpFilterRegistry
//

namespace {

using FilterOwnerList = std::vector<std::unique_ptr<XdsHttpFilterImpl>>;
using FilterRegistryMap = std::map<absl::string_view, XdsHttpFilterImpl*>;

FilterOwnerList* g_filters = nullptr;
FilterRegistryMap* g_filter_registry = nullptr;

}  // namespace

void XdsHttpFilterRegistry::RegisterFilter(
    std::unique_ptr<XdsHttpFilterImpl> filter) {
  GPR_ASSERT(g_filter_registry->emplace(filter->ConfigProtoName(), filter.get())
                 .second);
  auto override_proto_name = filter->OverrideConfigProtoName();
  if (!override_proto_name.empty()) {
    GPR_ASSERT(
        g_filter_registry->emplace(override_proto_name, filter.get()).second);
  }
  g_filters->push_back(std::move(filter));
}

const XdsHttpFilterImpl* XdsHttpFilterRegistry::GetFilterForType(
    absl::string_view proto_type_name) {
  auto it = g_filter_registry->find(proto_type_name);
  if (it == g_filter_registry->end()) return nullptr;
  return it->second;
}

void XdsHttpFilterRegistry::PopulateSymtab(upb_DefPool* symtab) {
  for (const auto& filter : *g_filters) {
    filter->PopulateSymtab(symtab);
  }
}

void XdsHttpFilterRegistry::Init() {
  g_filters = new FilterOwnerList;
  g_filter_registry = new FilterRegistryMap;
  RegisterFilter(std::make_unique<XdsHttpRouterFilter>());
  RegisterFilter(std::make_unique<XdsHttpFaultFilter>());
  RegisterFilter(std::make_unique<XdsHttpRbacFilter>());
}

void XdsHttpFilterRegistry::Shutdown() {
  delete g_filter_registry;
  delete g_filters;
}

}  // namespace grpc_core
