//
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
//

#include "src/core/ext/xds/xds_lb_policy_registry.h"

#include <stdint.h>

#include <string>

#include <google/protobuf/any.pb.h>
#include <google/protobuf/duration.pb.h>
#include <google/protobuf/struct.pb.h>
#include <google/protobuf/wrappers.pb.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "upb/reflection/def.hpp"
#include "upb/upb.hpp"

#include <grpc/grpc.h>

#include "src/core/ext/xds/xds_bootstrap_grpc.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/json/json_writer.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_factory.h"
#include "src/proto/grpc/testing/xds/v3/client_side_weighted_round_robin.pb.h"
#include "src/proto/grpc/testing/xds/v3/cluster.pb.h"
#include "src/proto/grpc/testing/xds/v3/extension.pb.h"
#include "src/proto/grpc/testing/xds/v3/ring_hash.pb.h"
#include "src/proto/grpc/testing/xds/v3/round_robin.pb.h"
#include "src/proto/grpc/testing/xds/v3/typed_struct.pb.h"
#include "src/proto/grpc/testing/xds/v3/wrr_locality.pb.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

using LoadBalancingPolicyProto =
    ::envoy::config::cluster::v3::LoadBalancingPolicy;
using ::envoy::extensions::load_balancing_policies::
    client_side_weighted_round_robin::v3::ClientSideWeightedRoundRobin;
using ::envoy::extensions::load_balancing_policies::ring_hash::v3::RingHash;
using ::envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin;
using ::envoy::extensions::load_balancing_policies::wrr_locality::v3::
    WrrLocality;
using ::xds::type::v3::TypedStruct;

// Uses XdsLbPolicyRegistry to convert
// envoy::config::cluster::v3::LoadBalancingPolicy to gRPC's JSON form.
absl::StatusOr<std::string> ConvertXdsPolicy(
    const LoadBalancingPolicyProto& policy) {
  std::string serialized_policy = policy.SerializeAsString();
  upb::Arena arena;
  upb::SymbolTable symtab;
  XdsResourceType::DecodeContext context = {nullptr,
                                            GrpcXdsBootstrap::GrpcXdsServer(),
                                            nullptr, symtab.ptr(), arena.ptr()};
  auto* upb_policy = envoy_config_cluster_v3_LoadBalancingPolicy_parse(
      serialized_policy.data(), serialized_policy.size(), arena.ptr());
  ValidationErrors errors;
  ValidationErrors::ScopedField field(&errors, ".load_balancing_policy");
  auto config = XdsLbPolicyRegistry().ConvertXdsLbPolicyConfig(
      context, upb_policy, &errors);
  if (!errors.ok()) {
    return errors.status(absl::StatusCode::kInvalidArgument,
                         "validation errors");
  }
  EXPECT_EQ(config.size(), 1);
  return JsonDump(Json{config[0]});
}

// A gRPC LB policy factory for a custom policy.  None of the methods
// will actually be used; we just need it to be present in the gRPC LB
// policy registry.
class CustomLbPolicyFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args /*args*/) const override {
    Crash("unreachable");
    return nullptr;
  }

  absl::string_view name() const override { return "test.CustomLb"; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& /*json*/) const override {
    return nullptr;
  }
};

//
// RoundRobin
//

TEST(RoundRobin, Basic) {
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      RoundRobin());
  auto result = ConvertXdsPolicy(policy);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result, "{\"round_robin\":{}}");
}

//
// ClientSideWeightedRoundRobin
//

TEST(ClientSideWeightedRoundRobinTest, DefaultConfig) {
  LoadBalancingPolicyProto policy;
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(ClientSideWeightedRoundRobin());
  auto result = ConvertXdsPolicy(policy);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result, "{\"weighted_round_robin\":{}}");
}

TEST(ClientSideWeightedRoundRobinTest, FieldsExplicitlySet) {
  ClientSideWeightedRoundRobin wrr;
  wrr.mutable_enable_oob_load_report()->set_value(true);
  wrr.mutable_oob_reporting_period()->set_seconds(1);
  wrr.mutable_blackout_period()->set_seconds(2);
  wrr.mutable_weight_expiration_period()->set_seconds(3);
  wrr.mutable_weight_update_period()->set_seconds(4);
  wrr.mutable_error_utilization_penalty()->set_value(5.0);
  LoadBalancingPolicyProto policy;
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(wrr);
  auto result = ConvertXdsPolicy(policy);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result,
            "{\"weighted_round_robin\":{"
            "\"blackoutPeriod\":\"2.000000000s\","
            "\"enableOobLoadReport\":true,"
            "\"errorUtilizationPenalty\":5,"
            "\"oobReportingPeriod\":\"1.000000000s\","
            "\"weightExpirationPeriod\":\"3.000000000s\","
            "\"weightUpdatePeriod\":\"4.000000000s\""
            "}}");
}

TEST(ClientSideWeightedRoundRobinTest, InvalidValues) {
  ClientSideWeightedRoundRobin wrr;
  wrr.mutable_oob_reporting_period()->set_seconds(-1);
  wrr.mutable_blackout_period()->set_seconds(-2);
  wrr.mutable_weight_expiration_period()->set_seconds(-3);
  wrr.mutable_weight_update_period()->set_seconds(-4);
  wrr.mutable_error_utilization_penalty()->set_value(-1);
  LoadBalancingPolicyProto policy;
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(wrr);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".client_side_weighted_round_robin.v3.ClientSideWeightedRoundRobin]"
            ".blackout_period.seconds "
            "error:value must be in the range [0, 315576000000]; "
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".client_side_weighted_round_robin.v3.ClientSideWeightedRoundRobin]"
            ".error_utilization_penalty error:value must be non-negative; "
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".client_side_weighted_round_robin.v3.ClientSideWeightedRoundRobin]"
            ".oob_reporting_period.seconds "
            "error:value must be in the range [0, 315576000000]; "
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".client_side_weighted_round_robin.v3.ClientSideWeightedRoundRobin]"
            ".weight_expiration_period.seconds "
            "error:value must be in the range [0, 315576000000]; "
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".client_side_weighted_round_robin.v3.ClientSideWeightedRoundRobin]"
            ".weight_update_period.seconds "
            "error:value must be in the range [0, 315576000000]]")
      << result.status();
}

//
// RingHash
//

TEST(RingHashConfig, DefaultConfig) {
  LoadBalancingPolicyProto policy;
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(RingHash());
  auto result = ConvertXdsPolicy(policy);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result,
            "{\"ring_hash_experimental\":{"
            "\"maxRingSize\":8388608,\"minRingSize\":1024}}");
}

TEST(RingHashConfig, FieldsExplicitlySet) {
  RingHash ring_hash;
  ring_hash.set_hash_function(RingHash::XX_HASH);
  ring_hash.mutable_minimum_ring_size()->set_value(1234);
  ring_hash.mutable_maximum_ring_size()->set_value(4567);
  LoadBalancingPolicyProto policy;
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(ring_hash);
  auto result = ConvertXdsPolicy(policy);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result,
            "{\"ring_hash_experimental\":{"
            "\"maxRingSize\":4567,\"minRingSize\":1234}}");
}

TEST(RingHashConfig, InvalidHashFunction) {
  RingHash ring_hash;
  ring_hash.set_hash_function(RingHash::MURMUR_HASH_2);
  LoadBalancingPolicyProto policy;
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(ring_hash);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".ring_hash.v3.RingHash].hash_function "
            "error:unsupported value (must be XX_HASH)]")
      << result.status();
}

TEST(RingHashConfig, RingSizesTooHigh) {
  RingHash ring_hash;
  ring_hash.mutable_minimum_ring_size()->set_value(8388609);
  ring_hash.mutable_maximum_ring_size()->set_value(8388609);
  LoadBalancingPolicyProto policy;
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(ring_hash);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".ring_hash.v3.RingHash].maximum_ring_size "
            "error:value must be in the range [1, 8388608]; "
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".ring_hash.v3.RingHash].minimum_ring_size "
            "error:value must be in the range [1, 8388608]]")
      << result.status();
}

TEST(RingHashConfig, RingSizesTooLow) {
  RingHash ring_hash;
  ring_hash.mutable_minimum_ring_size()->set_value(0);
  ring_hash.mutable_maximum_ring_size()->set_value(0);
  LoadBalancingPolicyProto policy;
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(ring_hash);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".ring_hash.v3.RingHash].maximum_ring_size "
            "error:value must be in the range [1, 8388608]; "
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".ring_hash.v3.RingHash].minimum_ring_size "
            "error:value must be in the range [1, 8388608]]")
      << result.status();
}

TEST(RingHashConfig, MinRingSizeGreaterThanMaxRingSize) {
  RingHash ring_hash;
  ring_hash.mutable_minimum_ring_size()->set_value(1000);
  ring_hash.mutable_maximum_ring_size()->set_value(999);
  LoadBalancingPolicyProto policy;
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(ring_hash);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".ring_hash.v3.RingHash].minimum_ring_size "
            "error:cannot be greater than maximum_ring_size]")
      << result.status();
}

//
// WrrLocality
//

TEST(WrrLocality, RoundRobinChild) {
  WrrLocality wrr_locality;
  wrr_locality.mutable_endpoint_picking_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(RoundRobin());
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      wrr_locality);
  auto result = ConvertXdsPolicy(policy);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result,
            "{\"xds_wrr_locality_experimental\":{"
            "\"childPolicy\":[{\"round_robin\":{}}]}}");
}

TEST(WrrLocality, MissingEndpointPickingPolicy) {
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      WrrLocality());
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".wrr_locality.v3.WrrLocality].endpoint_picking_policy "
            "error:field not present]")
      << result.status();
}

TEST(WrrLocality, ChildPolicyInvalid) {
  RingHash ring_hash;
  ring_hash.set_hash_function(RingHash::MURMUR_HASH_2);
  WrrLocality wrr_locality;
  wrr_locality.mutable_endpoint_picking_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(ring_hash);
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      wrr_locality);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".wrr_locality.v3.WrrLocality].endpoint_picking_policy.policies[0]"
            ".typed_extension_config.typed_config.value["
            "envoy.extensions.load_balancing_policies.ring_hash.v3.RingHash]"
            ".hash_function "
            "error:unsupported value (must be XX_HASH)]")
      << result.status();
}

TEST(WrrLocality, NoSupportedChildPolicy) {
  WrrLocality wrr_locality;
  wrr_locality.mutable_endpoint_picking_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(LoadBalancingPolicyProto());
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      wrr_locality);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".wrr_locality.v3.WrrLocality].endpoint_picking_policy "
            "error:no supported load balancing policy config found]")
      << result.status();
}

TEST(WrrLocality, UnsupportedChildPolicyTypeSkipped) {
  // Create WrrLocality policy and add two policies to its list, an unsupported
  // type and then a known RoundRobin type. Expect that the unsupported type is
  // skipped and RoundRobin is selected.
  WrrLocality wrr_locality;
  wrr_locality.mutable_endpoint_picking_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(LoadBalancingPolicyProto());
  wrr_locality.mutable_endpoint_picking_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(RoundRobin());
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      wrr_locality);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(*result,
            "{\"xds_wrr_locality_experimental\":{"
            "\"childPolicy\":[{\"round_robin\":{}}]}}");
}

//
// CustomPolicy
//

TEST(CustomPolicy, Basic) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("type.googleapis.com/test.CustomLb");
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  (*fields)["foo"].set_string_value("bar");
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  auto result = ConvertXdsPolicy(policy);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result, "{\"test.CustomLb\":{\"foo\":\"bar\"}}");
}

//
// XdsLbPolicyRegistryTest
//

TEST(XdsLbPolicyRegistryTest, EmptyLoadBalancingPolicy) {
  auto result = ConvertXdsPolicy(LoadBalancingPolicyProto());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: [field:load_balancing_policy "
            "error:no supported load balancing policy config found]")
      << result.status();
}

TEST(XdsLbPolicyRegistryTest, MissingTypedExtensionConfig) {
  LoadBalancingPolicyProto policy;
  policy.add_policies();
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config "
            "error:field not present]")
      << result.status();
}

TEST(XdsLbPolicyRegistryTest, MissingTypedConfig) {
  LoadBalancingPolicyProto policy;
  policy.add_policies()->mutable_typed_extension_config();
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config error:field not present]")
      << result.status();
}

// This tests that we pass along errors that are generated by
// ExtractXdsExtension().  An exhaustive list of error cases caught by
// ExtractXdsExtension() are covered in xds_common_types_test.
TEST(XdsLbPolicyRegistryTest, ErrorExtractingExtension) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("type.googleapis.com/");
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: ["
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[xds.type.v3.TypedStruct].type_url "
            "error:invalid value \"type.googleapis.com/\"]")
      << result.status();
}

TEST(XdsLbPolicyRegistryTest, NoSupportedType) {
  LoadBalancingPolicyProto policy;
  // Unsupported built-in type.
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(LoadBalancingPolicyProto());
  // Unsupported custom type.
  TypedStruct typed_struct;
  typed_struct.set_type_url("myorg/foo/bar/test.UnknownLb");
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(typed_struct);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: [field:load_balancing_policy "
            "error:no supported load balancing policy config found]")
      << result.status();
}

TEST(XdsLbPolicyRegistryTest, UnsupportedTypesSkipped) {
  LoadBalancingPolicyProto policy;
  // Unsupported built-in type.
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(LoadBalancingPolicyProto());
  // Unsupported custom type.
  TypedStruct typed_struct;
  typed_struct.set_type_url("myorg/foo/bar/test.UnknownLb");
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(typed_struct);
  // Supported type.
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(RoundRobin());
  auto result = ConvertXdsPolicy(policy);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result, "{\"round_robin\":{}}");
}

// Build a recurse load balancing policy that goes beyond the max allowable
// depth of 16.
LoadBalancingPolicyProto BuildRecursiveLoadBalancingPolicy(int depth) {
  LoadBalancingPolicyProto policy;
  if (depth >= 16) {
    policy.add_policies()
        ->mutable_typed_extension_config()
        ->mutable_typed_config()
        ->PackFrom(RoundRobin());
    return policy;
  }
  WrrLocality wrr_locality;
  *wrr_locality.mutable_endpoint_picking_policy() =
      BuildRecursiveLoadBalancingPolicy(depth + 1);
  policy.add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(wrr_locality);
  return policy;
}

TEST(XdsLbPolicyRegistryTest, MaxRecursion) {
  auto result = ConvertXdsPolicy(BuildRecursiveLoadBalancingPolicy(0));
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(result.status().message()),
              ::testing::EndsWith("error:exceeded max recursion depth of 16]"));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_core::CoreConfiguration::RegisterBuilder(
      [](grpc_core::CoreConfiguration::Builder* builder) {
        builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
            std::make_unique<grpc_core::testing::CustomLbPolicyFactory>());
      });
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
