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

#include "absl/types/variant.h"
#include "envoy/extensions/filters/http/router/v3/router.upb.h"
#include "envoy/extensions/filters/http/router/v3/router.upbdefs.h"

#include <grpc/support/log.h>

#include "src/core/ext/xds/xds_http_fault_filter.h"
#include "src/core/ext/xds/xds_http_rbac_filter.h"
#include "src/core/ext/xds/xds_http_stateful_session_filter.h"

namespace grpc_core {

//
// XdsHttpRouterFilter
//

absl::string_view XdsHttpRouterFilter::ConfigProtoName() const {
  return "envoy.extensions.filters.http.router.v3.Router";
}

absl::string_view XdsHttpRouterFilter::OverrideConfigProtoName() const {
  return "";
}

void XdsHttpRouterFilter::PopulateSymtab(upb_DefPool* symtab) const {
  envoy_extensions_filters_http_router_v3_Router_getmsgdef(symtab);
}

absl::optional<XdsHttpFilterImpl::FilterConfig>
XdsHttpRouterFilter::GenerateFilterConfig(
    const XdsResourceType::DecodeContext& context, XdsExtension extension,
    ValidationErrors* errors) const {
  absl::string_view* serialized_filter_config =
      absl::get_if<absl::string_view>(&extension.value);
  if (serialized_filter_config == nullptr) {
    errors->AddError("could not parse router filter config");
    return absl::nullopt;
  }
  if (envoy_extensions_filters_http_router_v3_Router_parse(
          serialized_filter_config->data(), serialized_filter_config->size(),
          context.arena) == nullptr) {
    errors->AddError("could not parse router filter config");
    return absl::nullopt;
  }
  return FilterConfig{ConfigProtoName(), Json()};
}

absl::optional<XdsHttpFilterImpl::FilterConfig>
XdsHttpRouterFilter::GenerateFilterConfigOverride(
    const XdsResourceType::DecodeContext& /*context*/,
    XdsExtension /*extension*/, ValidationErrors* errors) const {
  errors->AddError("router filter does not support config override");
  return absl::nullopt;
}

//
// XdsHttpFilterRegistry
//

XdsHttpFilterRegistry::XdsHttpFilterRegistry(bool register_builtins) {
  if (register_builtins) {
    RegisterFilter(std::make_unique<XdsHttpRouterFilter>());
    RegisterFilter(std::make_unique<XdsHttpFaultFilter>());
    RegisterFilter(std::make_unique<XdsHttpRbacFilter>());
    RegisterFilter(std::make_unique<XdsHttpStatefulSessionFilter>());
  }
}

void XdsHttpFilterRegistry::RegisterFilter(
    std::unique_ptr<XdsHttpFilterImpl> filter) {
  GPR_ASSERT(
      registry_map_.emplace(filter->ConfigProtoName(), filter.get()).second);
  auto override_proto_name = filter->OverrideConfigProtoName();
  if (!override_proto_name.empty()) {
    GPR_ASSERT(registry_map_.emplace(override_proto_name, filter.get()).second);
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
