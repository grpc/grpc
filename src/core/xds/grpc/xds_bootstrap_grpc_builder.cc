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

#include "src/core/xds/grpc/xds_bootstrap_grpc_builder.h"

#include <map>
#include <utility>
#include <variant>
#include <vector>

#include "envoy/extensions/filters/http/router/v3/router.upb.h"
#include "envoy/extensions/filters/http/router/v3/router.upbdefs.h"
#include "src/core/config/experiment_env_var.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/json/json.h"
#include "src/core/util/sync.h"
#include "src/core/xds/grpc/xds_http_composite_filter.h"
#include "src/core/xds/grpc/xds_http_fault_filter.h"
#include "src/core/xds/grpc/xds_http_gcp_authn_filter.h"
#include "src/core/xds/grpc/xds_http_rbac_filter.h"
#include "src/core/xds/grpc/xds_http_stateful_session_filter.h"

namespace grpc_core {

//
// GrpcXdsBootstrapBuilder
//

absl::StatusOr<std::unique_ptr<GrpcXdsBootstrap>>
GrpcXdsBootstrapBuilder::Build(absl::string_view json_string) {
  auto bootstrap = GrpcXdsBootstrap::Create(json_string);
  if (bootstrap.ok()) {
    (*bootstrap)->http_filter_registry_ = CreateXdsHttpFilterRegistry();
  }
  return bootstrap;
}

namespace {

Mutex* g_mu = new Mutex;
NoDestruct<absl::AnyInvocable<std::unique_ptr<XdsHttpFilterImpl>()>>
    g_http_filter_factory_factory ABSL_GUARDED_BY(*g_mu);

}  // namespace

void GrpcXdsBootstrapBuilder::SetXdsHttpFilterFactoryForTest(
    absl::AnyInvocable<std::unique_ptr<XdsHttpFilterImpl>()> factory) {
  MutexLock lock(g_mu);
  *g_http_filter_factory_factory = std::move(factory);
}

XdsHttpFilterRegistry GrpcXdsBootstrapBuilder::CreateXdsHttpFilterRegistry(
    bool register_builtins) {
  XdsHttpFilterRegistry registry;
  if (register_builtins) {
    registry.RegisterFilter(std::make_unique<XdsHttpRouterFilter>());
    registry.RegisterFilter(std::make_unique<XdsHttpFaultFilter>());
    registry.RegisterFilter(std::make_unique<XdsHttpRbacFilter>());
    registry.RegisterFilter(std::make_unique<XdsHttpStatefulSessionFilter>());
    registry.RegisterFilter(std::make_unique<XdsHttpGcpAuthnFilter>());
    if (IsExperimentEnvVarEnabled("GRPC_EXPERIMENTAL_XDS_COMPOSITE_FILTER")) {
      registry.RegisterFilter(std::make_unique<XdsHttpCompositeFilter>());
    }
    MutexLock lock(g_mu);
    if (*g_http_filter_factory_factory != nullptr) {
      registry.RegisterFilter((*g_http_filter_factory_factory)());
    }
  }
  return registry;
}

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

std::optional<Json> XdsHttpRouterFilter::GenerateFilterConfig(
    absl::string_view /*instance_name*/,
    const XdsResourceType::DecodeContext& context,
    const XdsExtension& extension, ValidationErrors* errors) const {
  const absl::string_view* serialized_filter_config =
      std::get_if<absl::string_view>(&extension.value);
  if (serialized_filter_config == nullptr) {
    errors->AddError("could not parse router filter config");
    return std::nullopt;
  }
  if (envoy_extensions_filters_http_router_v3_Router_parse(
          serialized_filter_config->data(), serialized_filter_config->size(),
          context.arena) == nullptr) {
    errors->AddError("could not parse router filter config");
    return std::nullopt;
  }
  return Json();
}

std::optional<Json> XdsHttpRouterFilter::GenerateFilterConfigOverride(
    absl::string_view /*instance_name*/,
    const XdsResourceType::DecodeContext& /*context*/,
    const XdsExtension& /*extension*/, ValidationErrors* errors) const {
  errors->AddError("router filter does not support config override");
  return std::nullopt;
}

RefCountedPtr<const FilterConfig> XdsHttpRouterFilter::ParseTopLevelConfig(
    absl::string_view /*instance_name*/,
    const XdsResourceType::DecodeContext& context,
    const XdsExtension& extension, ValidationErrors* errors) const {
  const absl::string_view* serialized_filter_config =
      std::get_if<absl::string_view>(&extension.value);
  if (serialized_filter_config == nullptr) {
    errors->AddError("could not parse router filter config");
    return nullptr;
  }
  if (envoy_extensions_filters_http_router_v3_Router_parse(
          serialized_filter_config->data(), serialized_filter_config->size(),
          context.arena) == nullptr) {
    errors->AddError("could not parse router filter config");
    return nullptr;
  }
  return nullptr;
}

RefCountedPtr<const FilterConfig> XdsHttpRouterFilter::ParseOverrideConfig(
    absl::string_view /*instance_name*/,
    const XdsResourceType::DecodeContext& /*context*/,
    const XdsExtension& /*extension*/, ValidationErrors* errors) const {
  errors->AddError("router filter does not support config override");
  return nullptr;
}

}  // namespace grpc_core
