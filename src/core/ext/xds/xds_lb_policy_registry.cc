//
// Copyright 2022 gRPC authors.
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

#include "src/core/ext/xds/xds_lb_policy_registry.h"

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "envoy/config/core/v3/extension.upb.h"
#include "envoy/extensions/load_balancing_policies/client_side_weighted_round_robin/v3/client_side_weighted_round_robin.upb.h"
#include "envoy/extensions/load_balancing_policies/pick_first/v3/pick_first.upb.h"
#include "envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.upb.h"
#include "envoy/extensions/load_balancing_policies/wrr_locality/v3/wrr_locality.upb.h"
#include "google/protobuf/wrappers.upb.h"

#include <grpc/support/json.h>

#include "src/core/ext/xds/xds_common_types.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/load_balancing/lb_policy_registry.h"

namespace grpc_core {

namespace {

class RoundRobinLbPolicyConfigFactory
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

class ClientSideWeightedRoundRobinLbPolicyConfigFactory
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
    auto* enable_oob_load_report =
        envoy_extensions_load_balancing_policies_client_side_weighted_round_robin_v3_ClientSideWeightedRoundRobin_enable_oob_load_report(
            resource);
    if (enable_oob_load_report != nullptr &&
        google_protobuf_BoolValue_value(enable_oob_load_report)) {
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
    return Json::Object{
        {"weighted_round_robin", Json::FromObject(std::move(config))}};
  }

  absl::string_view type() override { return Type(); }

  static absl::string_view Type() {
    return "envoy.extensions.load_balancing_policies.client_side_weighted_"
           "round_robin.v3.ClientSideWeightedRoundRobin";
  }
};

class RingHashLbPolicyConfigFactory
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
    uint64_t max_ring_size = 8388608;
    const auto* uint64_value =
        envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_maximum_ring_size(
            resource);
    if (uint64_value != nullptr) {
      max_ring_size = google_protobuf_UInt64Value_value(uint64_value);
      if (max_ring_size == 0 || max_ring_size > 8388608) {
        ValidationErrors::ScopedField field(errors, ".maximum_ring_size");
        errors->AddError("value must be in the range [1, 8388608]");
      }
    }
    uint64_t min_ring_size = 1024;
    uint64_value =
        envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_minimum_ring_size(
            resource);
    if (uint64_value != nullptr) {
      min_ring_size = google_protobuf_UInt64Value_value(uint64_value);
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

class WrrLocalityLbPolicyConfigFactory
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

class PickFirstLbPolicyConfigFactory
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

//
// XdsLbPolicyRegistry
//

XdsLbPolicyRegistry::XdsLbPolicyRegistry() {
  policy_config_factories_.emplace(
      RingHashLbPolicyConfigFactory::Type(),
      std::make_unique<RingHashLbPolicyConfigFactory>());
  policy_config_factories_.emplace(
      RoundRobinLbPolicyConfigFactory::Type(),
      std::make_unique<RoundRobinLbPolicyConfigFactory>());
  policy_config_factories_.emplace(
      ClientSideWeightedRoundRobinLbPolicyConfigFactory::Type(),
      std::make_unique<ClientSideWeightedRoundRobinLbPolicyConfigFactory>());
  policy_config_factories_.emplace(
      WrrLocalityLbPolicyConfigFactory::Type(),
      std::make_unique<WrrLocalityLbPolicyConfigFactory>());
  policy_config_factories_.emplace(
      PickFirstLbPolicyConfigFactory::Type(),
      std::make_unique<PickFirstLbPolicyConfigFactory>());
}

Json::Array XdsLbPolicyRegistry::ConvertXdsLbPolicyConfig(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_cluster_v3_LoadBalancingPolicy* lb_policy,
    ValidationErrors* errors, int recursion_depth) const {
  constexpr int kMaxRecursionDepth = 16;
  if (recursion_depth >= kMaxRecursionDepth) {
    errors->AddError(
        absl::StrCat("exceeded max recursion depth of ", kMaxRecursionDepth));
    return {};
  }
  const size_t original_error_size = errors->size();
  size_t size = 0;
  const auto* policies =
      envoy_config_cluster_v3_LoadBalancingPolicy_policies(lb_policy, &size);
  for (size_t i = 0; i < size; ++i) {
    ValidationErrors::ScopedField field(
        errors, absl::StrCat(".policies[", i, "].typed_extension_config"));
    const auto* typed_extension_config =
        envoy_config_cluster_v3_LoadBalancingPolicy_Policy_typed_extension_config(
            policies[i]);
    if (typed_extension_config == nullptr) {
      errors->AddError("field not present");
      return {};
    }
    ValidationErrors::ScopedField field2(errors, ".typed_config");
    const auto* typed_config =
        envoy_config_core_v3_TypedExtensionConfig_typed_config(
            typed_extension_config);
    auto extension = ExtractXdsExtension(context, typed_config, errors);
    if (!extension.has_value()) return {};
    // Check for registered LB policy type.
    absl::string_view* serialized_value =
        absl::get_if<absl::string_view>(&extension->value);
    if (serialized_value != nullptr) {
      auto config_factory_it = policy_config_factories_.find(extension->type);
      if (config_factory_it != policy_config_factories_.end()) {
        return Json::Array{Json::FromObject(
            config_factory_it->second->ConvertXdsLbPolicyConfig(
                this, context, *serialized_value, errors, recursion_depth))};
      }
    }
    // Check for custom LB policy type.
    Json* json = absl::get_if<Json>(&extension->value);
    if (json != nullptr &&
        CoreConfiguration::Get().lb_policy_registry().LoadBalancingPolicyExists(
            extension->type, nullptr)) {
      return Json::Array{
          Json::FromObject({{std::string(extension->type), std::move(*json)}})};
    }
    // Unsupported type.  Continue to next entry.
  }
  if (original_error_size == errors->size()) {
    errors->AddError("no supported load balancing policy config found");
  }
  return {};
}

}  // namespace grpc_core
