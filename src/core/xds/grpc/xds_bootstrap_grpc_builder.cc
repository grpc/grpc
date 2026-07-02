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

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "envoy/config/core/v3/extension.upb.h"
#include "envoy/extensions/filters/http/router/v3/router.upb.h"
#include "envoy/extensions/filters/http/router/v3/router.upbdefs.h"
#include "envoy/extensions/load_balancing_policies/client_side_weighted_round_robin/v3/client_side_weighted_round_robin.upb.h"
#include "envoy/extensions/load_balancing_policies/pick_first/v3/pick_first.upb.h"
#include "envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.upb.h"
#include "envoy/extensions/load_balancing_policies/wrr_locality/v3/wrr_locality.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "src/core/config/experiment_env_var.h"
#include "src/core/load_balancing/weighted_round_robin/weighted_round_robin.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/json/json.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/grpc/xds_http_composite_filter.h"
#include "src/core/xds/grpc/xds_http_fault_filter.h"
#include "src/core/xds/grpc/xds_http_gcp_authn_filter.h"
#include "src/core/xds/grpc/xds_http_rbac_filter.h"
#include "src/core/xds/grpc/xds_http_stateful_session_filter.h"
#include "src/core/xds/grpc/xds_lb_policy_registry.h"
#include "absl/strings/str_cat.h"

namespace grpc_core {

absl::StatusOr<std::unique_ptr<GrpcXdsBootstrap>>
GrpcXdsBootstrapBuilder::Build(absl::string_view json_string) {
  auto bootstrap = GrpcXdsBootstrap::Create(json_string);
  if (bootstrap.ok()) {
    (*bootstrap)->http_filter_registry_ = CreateXdsHttpFilterRegistry();
    (*bootstrap)->lb_policy_registry_ = CreateXdsLbPolicyRegistry();
    (*bootstrap)->audit_logger_registry_ = CreateXdsAuditLoggerRegistry();
  }
  return bootstrap;
}

//
// HTTP filter registry
//

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
// LB policy registry
//

namespace {

class RoundRobinLbPolicyConfigFactory final
    : public XdsLbPolicyRegistry::ConfigFactory {
 public:
  Json::Object ConvertXdsLbPolicyConfig(
      const XdsLbPolicyRegistry* /*registry*/,
      const XdsResourceType::DecodeContext& /*context*/,
      absl::string_view /*configuration*/, ValidationErrors* /*errors*/,
      int /*recursion_depth*/) override {
    return Json::Object{{"round_robin", Json::FromObject({})}};
  }

  absl::string_view type() override { return Type(); }

  static absl::string_view Type() {
    return "envoy.extensions.load_balancing_policies.round_robin.v3.RoundRobin";
  }
};

class ClientSideWeightedRoundRobinLbPolicyConfigFactory final
    : public XdsLbPolicyRegistry::ConfigFactory {
 public:
  Json::Object ConvertXdsLbPolicyConfig(
      const XdsLbPolicyRegistry* /*registry*/,
      const XdsResourceType::DecodeContext& context,
      absl::string_view configuration, ValidationErrors* errors,
      int /*recursion_depth*/) override {
    const auto* resource =
        envoy_extensions_load_balancing_policies_client_side_weighted_round_robin_v3_ClientSideWeightedRoundRobin_parse(
            configuration.data(), configuration.size(), context.arena);
    if (resource == nullptr) {
      errors->AddError(
          "can't decode ClientSideWeightedRoundRobin LB policy config");
      return {};
    }
    Json::Object config;
    // enable_oob_load_report
    if (ParseBoolValue(
            envoy_extensions_load_balancing_policies_client_side_weighted_round_robin_v3_ClientSideWeightedRoundRobin_enable_oob_load_report(
                resource))) {
      config["enableOobLoadReport"] = Json::FromBool(true);
    }
    // oob_reporting_period
    auto* duration_proto =
        envoy_extensions_load_balancing_policies_client_side_weighted_round_robin_v3_ClientSideWeightedRoundRobin_oob_reporting_period(
            resource);
    if (duration_proto != nullptr) {
      ValidationErrors::ScopedField field(errors, ".oob_reporting_period");
      Duration duration = ParseDuration(duration_proto, errors);
      config["oobReportingPeriod"] = Json::FromString(duration.ToJsonString());
    }
    // blackout_period
    duration_proto =
        envoy_extensions_load_balancing_policies_client_side_weighted_round_robin_v3_ClientSideWeightedRoundRobin_blackout_period(
            resource);
    if (duration_proto != nullptr) {
      ValidationErrors::ScopedField field(errors, ".blackout_period");
      Duration duration = ParseDuration(duration_proto, errors);
      config["blackoutPeriod"] = Json::FromString(duration.ToJsonString());
    }
    // weight_update_period
    duration_proto =
        envoy_extensions_load_balancing_policies_client_side_weighted_round_robin_v3_ClientSideWeightedRoundRobin_weight_update_period(
            resource);
    if (duration_proto != nullptr) {
      ValidationErrors::ScopedField field(errors, ".weight_update_period");
      Duration duration = ParseDuration(duration_proto, errors);
      config["weightUpdatePeriod"] = Json::FromString(duration.ToJsonString());
    }
    // weight_expiration_period
    duration_proto =
        envoy_extensions_load_balancing_policies_client_side_weighted_round_robin_v3_ClientSideWeightedRoundRobin_weight_expiration_period(
            resource);
    if (duration_proto != nullptr) {
      ValidationErrors::ScopedField field(errors, ".weight_expiration_period");
      Duration duration = ParseDuration(duration_proto, errors);
      config["weightExpirationPeriod"] =
          Json::FromString(duration.ToJsonString());
    }
    // error_utilization_penalty
    auto* error_utilization_penalty =
        envoy_extensions_load_balancing_policies_client_side_weighted_round_robin_v3_ClientSideWeightedRoundRobin_error_utilization_penalty(
            resource);
    if (error_utilization_penalty != nullptr) {
      ValidationErrors::ScopedField field(errors, ".error_utilization_penalty");
      const float value =
          google_protobuf_FloatValue_value(error_utilization_penalty);
      if (value < 0.0) {
        errors->AddError("value must be non-negative");
      }
      config["errorUtilizationPenalty"] = Json::FromNumber(value);
    }
    // metric_names_for_computing_utilization
    if (WrrCustomMetricsEnabled()) {
      size_t size;
      auto metric_names_for_computing_utilization =
          envoy_extensions_load_balancing_policies_client_side_weighted_round_robin_v3_ClientSideWeightedRoundRobin_metric_names_for_computing_utilization(
              resource, &size);
      if (metric_names_for_computing_utilization != nullptr && size != 0) {
        Json::Array metric_names;
        for (size_t i = 0; i < size; ++i) {
          metric_names.emplace_back(Json::FromString(
              UpbStringToStdString(metric_names_for_computing_utilization[i])));
        }
        config["metricNamesForComputingUtilization"] =
            Json::FromArray(std::move(metric_names));
      }
    }
    return Json::Object{
        {"weighted_round_robin", Json::FromObject(std::move(config))}};
  }

  absl::string_view type() override { return Type(); }

  static absl::string_view Type() {
    return "envoy.extensions.load_balancing_policies.client_side_weighted_"
           "round_robin.v3.ClientSideWeightedRoundRobin";
  }
};

class RingHashLbPolicyConfigFactory final
    : public XdsLbPolicyRegistry::ConfigFactory {
 public:
  Json::Object ConvertXdsLbPolicyConfig(
      const XdsLbPolicyRegistry* /*registry*/,
      const XdsResourceType::DecodeContext& context,
      absl::string_view configuration, ValidationErrors* errors,
      int /*recursion_depth*/) override {
    const auto* resource =
        envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_parse(
            configuration.data(), configuration.size(), context.arena);
    if (resource == nullptr) {
      errors->AddError("can't decode RingHash LB policy config");
      return {};
    }
    if (envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_hash_function(
            resource) !=
            envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_XX_HASH &&
        envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_hash_function(
            resource) !=
            envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_DEFAULT_HASH) {
      ValidationErrors::ScopedField field(errors, ".hash_function");
      errors->AddError("unsupported value (must be XX_HASH)");
    }
    uint64_t max_ring_size =
        ParseUInt64Value(
            envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_maximum_ring_size(
                resource))
            .value_or(8388608);
    if (max_ring_size == 0 || max_ring_size > 8388608) {
      ValidationErrors::ScopedField field(errors, ".maximum_ring_size");
      errors->AddError("value must be in the range [1, 8388608]");
    }
    uint64_t min_ring_size =
        ParseUInt64Value(
            envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_minimum_ring_size(
                resource))
            .value_or(1024);
    {
      ValidationErrors::ScopedField field(errors, ".minimum_ring_size");
      if (min_ring_size == 0 || min_ring_size > 8388608) {
        errors->AddError("value must be in the range [1, 8388608]");
      }
      if (min_ring_size > max_ring_size) {
        errors->AddError("cannot be greater than maximum_ring_size");
      }
    }
    return Json::Object{
        {"ring_hash_experimental",
         Json::FromObject({
             {"minRingSize", Json::FromNumber(min_ring_size)},
             {"maxRingSize", Json::FromNumber(max_ring_size)},
         })},
    };
  }

  absl::string_view type() override { return Type(); }

  static absl::string_view Type() {
    return "envoy.extensions.load_balancing_policies.ring_hash.v3.RingHash";
  }
};

class WrrLocalityLbPolicyConfigFactory final
    : public XdsLbPolicyRegistry::ConfigFactory {
 public:
  Json::Object ConvertXdsLbPolicyConfig(
      const XdsLbPolicyRegistry* registry,
      const XdsResourceType::DecodeContext& context,
      absl::string_view configuration, ValidationErrors* errors,
      int recursion_depth) override {
    const auto* resource =
        envoy_extensions_load_balancing_policies_wrr_locality_v3_WrrLocality_parse(
            configuration.data(), configuration.size(), context.arena);
    if (resource == nullptr) {
      errors->AddError("can't decode WrrLocality LB policy config");
      return {};
    }
    ValidationErrors::ScopedField field(errors, ".endpoint_picking_policy");
    const auto* endpoint_picking_policy =
        envoy_extensions_load_balancing_policies_wrr_locality_v3_WrrLocality_endpoint_picking_policy(
            resource);
    if (endpoint_picking_policy == nullptr) {
      errors->AddError("field not present");
      return {};
    }
    auto child_policy = registry->ConvertXdsLbPolicyConfig(
        context, endpoint_picking_policy, errors, recursion_depth + 1);
    return Json::Object{
        {"xds_wrr_locality_experimental",
         Json::FromObject(
             {{"childPolicy", Json::FromArray(std::move(child_policy))}})}};
  }

  absl::string_view type() override { return Type(); }

  static absl::string_view Type() {
    return "envoy.extensions.load_balancing_policies.wrr_locality.v3."
           "WrrLocality";
  }
};

class PickFirstLbPolicyConfigFactory final
    : public XdsLbPolicyRegistry::ConfigFactory {
 public:
  Json::Object ConvertXdsLbPolicyConfig(
      const XdsLbPolicyRegistry* /*registry*/,
      const XdsResourceType::DecodeContext& context,
      absl::string_view configuration, ValidationErrors* errors,
      int /*recursion_depth*/) override {
    const auto* resource =
        envoy_extensions_load_balancing_policies_pick_first_v3_PickFirst_parse(
            configuration.data(), configuration.size(), context.arena);
    if (resource == nullptr) {
      errors->AddError("can't decode PickFirst LB policy config");
      return {};
    }
    bool shuffle_address_list =
        envoy_extensions_load_balancing_policies_pick_first_v3_PickFirst_shuffle_address_list(
            resource);
    return Json::Object{
        {"pick_first",
         Json::FromObject({
             {"shuffleAddressList", Json::FromBool(shuffle_address_list)},
         })}};
  }

  absl::string_view type() override { return Type(); }

  static absl::string_view Type() {
    return "envoy.extensions.load_balancing_policies.pick_first.v3.PickFirst";
  }
};

}  // namespace

XdsLbPolicyRegistry GrpcXdsBootstrapBuilder::CreateXdsLbPolicyRegistry() {
  XdsLbPolicyRegistry registry;
  registry.RegisterFactory(std::make_unique<RingHashLbPolicyConfigFactory>());
  registry.RegisterFactory(std::make_unique<RoundRobinLbPolicyConfigFactory>());
  registry.RegisterFactory(
      std::make_unique<ClientSideWeightedRoundRobinLbPolicyConfigFactory>());
  registry.RegisterFactory(
      std::make_unique<WrrLocalityLbPolicyConfigFactory>());
  registry.RegisterFactory(std::make_unique<PickFirstLbPolicyConfigFactory>());
  return registry;
}

//
// Audit logger registry
//

namespace {

class StdoutLoggerConfigFactory final
    : public XdsAuditLoggerRegistry::ConfigFactory {
 public:
  Json::Object ConvertXdsAuditLoggerConfig(
      const XdsResourceType::DecodeContext& /*context*/,
      absl::string_view /*configuration*/,
      ValidationErrors* /*errors*/) override {
    // Stdout logger has no configuration right now. So we don't process the
    // config protobuf.
    return {};
  }

  absl::string_view type() override { return Type(); }
  absl::string_view name() override { return "stdout_logger"; }

  static absl::string_view Type() {
    return "envoy.extensions.rbac.audit_loggers.stream.v3.StdoutAuditLog";
  }
};

}  // namespace

XdsAuditLoggerRegistry GrpcXdsBootstrapBuilder::CreateXdsAuditLoggerRegistry() {
  XdsAuditLoggerRegistry registry;
  registry.RegisterFactory(std::make_unique<StdoutLoggerConfigFactory>());
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
