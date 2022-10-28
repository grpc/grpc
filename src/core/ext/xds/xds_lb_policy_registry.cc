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
#include "envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.upb.h"
#include "envoy/extensions/load_balancing_policies/wrr_locality/v3/wrr_locality.upb.h"
#include "google/protobuf/wrappers.upb.h"

#include "src/core/ext/xds/xds_common_types.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"

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
    return Json::Object{{"round_robin", Json::Object()}};
  }

  absl::string_view type() override { return Type(); }

  static absl::string_view Type() {
    return "envoy.extensions.load_balancing_policies.round_robin.v3.RoundRobin";
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
         Json::Object{
             {"minRingSize", min_ring_size},
             {"maxRingSize", max_ring_size},
         }},
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
         Json::Object{{"childPolicy", std::move(child_policy)}}}};
  }

  absl::string_view type() override { return Type(); }

  static absl::string_view Type() {
    return "envoy.extensions.load_balancing_policies.wrr_locality.v3."
           "WrrLocality";
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
      WrrLocalityLbPolicyConfigFactory::Type(),
      std::make_unique<WrrLocalityLbPolicyConfigFactory>());
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
    if (typed_config == nullptr) {
      errors->AddError("field not present");
      return {};
    }
    auto extension = ExtractXdsExtension(context, typed_config, errors);
    if (!extension.has_value()) return {};
    // Check for registered LB policy type.
    absl::string_view* serialized_value =
        absl::get_if<absl::string_view>(&extension->value);
    if (serialized_value != nullptr) {
      auto config_factory_it = policy_config_factories_.find(extension->type);
      if (config_factory_it != policy_config_factories_.end()) {
        return Json::Array{config_factory_it->second->ConvertXdsLbPolicyConfig(
            this, context, *serialized_value, errors, recursion_depth)};
      }
    }
    // Check for custom LB policy type.
    Json* json = absl::get_if<Json>(&extension->value);
    if (json != nullptr &&
        CoreConfiguration::Get().lb_policy_registry().LoadBalancingPolicyExists(
            extension->type, nullptr)) {
      return Json::Array{
          Json::Object{{std::string(extension->type), std::move(*json)}}};
    }
    // Unsupported type.  Continue to next entry.
  }
  if (original_error_size == errors->size()) {
    errors->AddError("no supported load balancing policy config found");
  }
  return {};
}

}  // namespace grpc_core
