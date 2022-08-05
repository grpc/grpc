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

#include "src/core/ext/xds/xds_lb_policy_registry_grpc.h"

#include <stddef.h>

#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "envoy/config/core/v3/extension.upb.h"
#include "envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.upb.h"
#include "envoy/extensions/load_balancing_policies/wrr_locality/v3/wrr_locality.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/struct.upb.h"
#include "google/protobuf/struct.upbdefs.h"
#include "google/protobuf/wrappers.upb.h"
#include "upb/arena.h"
#include "upb/json_encode.h"
#include "upb/status.h"
#include "upb/upb.hpp"
#include "xds/type/v3/typed_struct.upb.h"

#include "src/core/ext/xds/upb_utils.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_common_types.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"

namespace grpc_core {

namespace {

class XdsRingHashLbPolicy : public XdsLbPolicy {
 public:
  static absl::string_view Type() {
    return "envoy.extensions.load_balancing_policies.ring_hash.v3.RingHash";
  }

  absl::string_view ConfigProtoType() override { return Type(); }

  absl::StatusOr<Json::Object> ConvertXdsLbPolicyConfig(
      const XdsResourceType::DecodeContext& context, const XdsLbPolicyRegistry&,
      absl::string_view configuration, int /* recursion_depth */) override {
    const auto* resource =
        envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_parse(
            configuration.data(), configuration.size(), context.arena);
    if (resource == nullptr) {
      return absl::InvalidArgumentError(
          "Can't decode RingHash loadbalancing policy");
    }
    if (envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_hash_function(
            resource) !=
        envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_XX_HASH) {
      return absl::InvalidArgumentError(
          "Invalid hash function provided for RingHash loadbalancing policy. "
          "Only XX_HASH is supported.");
    }
    Json::Object json;
    const auto* min_ring_size =
        envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_minimum_ring_size(
            resource);
    if (min_ring_size != nullptr) {
      json.emplace("minRingSize",
                   google_protobuf_UInt64Value_value(min_ring_size));
    }
    const auto* max_ring_size =
        envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_maximum_ring_size(
            resource);
    if (max_ring_size != nullptr) {
      json.emplace("maxRingSize",
                   google_protobuf_UInt64Value_value(max_ring_size));
    }
    return Json::Object{{"ring_hash_experimental", std::move(json)}};
  }
};

class XdsRoundRobinLbPolicy : public XdsLbPolicy {
 public:
  static absl::string_view Type() {
    return "envoy.extensions.load_balancing_policies.round_robin.v3.RoundRobin";
  }

  absl::string_view ConfigProtoType() override { return Type(); }

  absl::StatusOr<Json::Object> ConvertXdsLbPolicyConfig(
      const XdsResourceType::DecodeContext& /*context*/,
      const XdsLbPolicyRegistry&, absl::string_view /*configuration*/,
      int /*recursion_depth*/) override {
    return Json::Object{{"round_robin", Json::Object()}};
  }
};

class XdsWrrLocalityLbPolicy : public XdsLbPolicy {
 public:
  static absl::string_view Type() {
    return "envoy.extensions.load_balancing_policies.wrr_locality.v3."
           "WrrLocality";
  }

  absl::string_view ConfigProtoType() override { return Type(); }

  absl::StatusOr<Json::Object> ConvertXdsLbPolicyConfig(
      const XdsResourceType::DecodeContext& context,
      const XdsLbPolicyRegistry& registry, absl::string_view configuration,
      int recursion_depth) override {
    const auto* resource =
        envoy_extensions_load_balancing_policies_wrr_locality_v3_WrrLocality_parse(
            configuration.data(), configuration.size(), context.arena);
    if (resource == nullptr) {
      return absl::InvalidArgumentError(
          "Can't decode WrrLocality loadbalancing policy");
    }
    const auto* endpoint_picking_policy =
        envoy_extensions_load_balancing_policies_wrr_locality_v3_WrrLocality_endpoint_picking_policy(
            resource);
    if (endpoint_picking_policy == nullptr) {
      return absl::InvalidArgumentError(
          "WrrLocality: endpoint_picking_policy not found");
    }
    auto child_policy = registry.ConvertXdsLbPolicyConfig(
        context, endpoint_picking_policy, recursion_depth + 1);
    if (!child_policy.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Error parsing WrrLocality load balancing policy: ",
                       child_policy.status().message()));
    }
    return Json::Object{
        {"xds_wrr_locality_experimental",
         Json::Object{{"child_policy", *std::move(child_policy)}}}};
  }
};

}  // namespace

void RegisterGrpcXdsLbPolicies(XdsLbPolicyRegistry* registry) {
  registry->RegisterPolicy(absl::make_unique<XdsRingHashLbPolicy>());
  registry->RegisterPolicy(absl::make_unique<XdsRoundRobinLbPolicy>());
  registry->RegisterPolicy(absl::make_unique<XdsWrrLocalityLbPolicy>());
}

}  // namespace grpc_core
