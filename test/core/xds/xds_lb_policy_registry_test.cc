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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/strings/str_format.h"
#include "upb/def.hpp"
#include "upb/upb.h"
#include "upb/upb.hpp"

#include <grpc/grpc.h>

#include "src/core/ext/xds/xds_bootstrap_grpc.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/load_balancing/lb_policy_factory.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"
#include "src/proto/grpc/testing/xds/v3/cluster.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/ring_hash.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/round_robin.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/typed_struct.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/udpa_typed_struct.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/wrr_locality.grpc.pb.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/config_grpc_cli.h"

namespace grpc_core {
namespace testing {
namespace {

using LoadBalancingPolicyProto =
    ::envoy::config::cluster::v3::LoadBalancingPolicy;
using ::envoy::extensions::load_balancing_policies::ring_hash::v3::RingHash;
using ::envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin;
using ::envoy::extensions::load_balancing_policies::wrr_locality::v3::
    WrrLocality;
using ::xds::type::v3::TypedStruct;

// Uses XdsLbPolicyRegistry to convert
// envoy::config::cluster::v3::LoadBalancingPolicy to gRPC's JSON form.
absl::StatusOr<Json::Array> ConvertXdsPolicy(LoadBalancingPolicyProto policy) {
  std::string serialized_policy = policy.SerializeAsString();
  upb::Arena arena;
  upb::SymbolTable symtab;
  XdsResourceType::DecodeContext context = {nullptr,
                                            GrpcXdsBootstrap::GrpcXdsServer(),
                                            nullptr, symtab.ptr(), arena.ptr()};
  auto* upb_policy = envoy_config_cluster_v3_LoadBalancingPolicy_parse(
      serialized_policy.data(), serialized_policy.size(), arena.ptr());
  return XdsLbPolicyRegistry::ConvertXdsLbPolicyConfig(context, upb_policy);
}

TEST(XdsLbPolicyRegistryTest, EmptyLoadBalancingPolicy) {
  auto result = ConvertXdsPolicy(LoadBalancingPolicyProto());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(result.status().message()),
      ::testing::HasSubstr("No supported load balancing policy config found"));
}

TEST(XdsLbPolicyRegistryTest, UnsupportedBuiltinType) {
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      LoadBalancingPolicyProto());
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(result.status().message()),
      ::testing::HasSubstr("No supported load balancing policy config found"));
}

TEST(XdsLbPolicyRegistryTest, MissingTypedExtensionConfig) {
  LoadBalancingPolicyProto policy;
  policy.add_policies();
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(result.status().message()),
              ::testing::HasSubstr("Error parsing LoadBalancingPolicy::Policy "
                                   "- Missing typed_extension_config field"));
}

TEST(XdsLbPolicyRegistryTest, MissingTypedConfig) {
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config();
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(result.status().message()),
      ::testing::HasSubstr("Error parsing LoadBalancingPolicy::Policy - "
                           "Missing TypedExtensionConfig::typed_config field"));
}

TEST(XdsLbPolicyRegistryTest, RingHashInvalidHash) {
  RingHash ring_hash;
  ring_hash.set_hash_function(RingHash::DEFAULT_HASH);
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      ring_hash);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(result.status().message()),
      ::testing::HasSubstr("Invalid hash function provided for RingHash "
                           "loadbalancing policy. Only XX_HASH is supported"));
}

TEST(XdsLbPolicyRegistryTest, RingHashRingSizeDefaults) {
  RingHash ring_hash;
  ring_hash.set_hash_function(RingHash::XX_HASH);
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      ring_hash);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  EXPECT_EQ((*result)[0], Json::Parse("{"
                                      "\"ring_hash_experimental\": {"
                                      "}}")
                              .value());
}

TEST(XdsLbPolicyRegistryTest, RingHashRingSizeCustom) {
  RingHash ring_hash;
  ring_hash.set_hash_function(RingHash::XX_HASH);
  ring_hash.mutable_minimum_ring_size()->set_value(1234);
  ring_hash.mutable_maximum_ring_size()->set_value(4567);
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      ring_hash);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  EXPECT_EQ((*result)[0], Json::Parse("{"
                                      "\"ring_hash_experimental\": {"
                                      "  \"minRingSize\": 1234,"
                                      "  \"maxRingSize\": 4567"
                                      "}}")
                              .value());
}

TEST(XdsLbPolicyRegistryTest, RoundRobin) {
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      RoundRobin());
  auto result = ConvertXdsPolicy(policy);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  EXPECT_EQ((*result)[0], Json::Parse("{"
                                      "\"round_robin\": {}"
                                      "}")
                              .value());
}

TEST(XdsLbPolicyRegistryTest, WrrLocality) {
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
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  EXPECT_EQ((*result)[0], Json::Parse("{"
                                      "\"xds_wrr_locality_experimental\": {"
                                      "  \"child_policy\": [{"
                                      "    \"round_robin\": {}"
                                      "  }]"
                                      "}}")
                              .value());
}

TEST(XdsLbPolicyRegistryTest, WrrLocalityMissingEndpointPickingPolicy) {
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      WrrLocality());
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(result.status().message()),
              ::testing::ContainsRegex(
                  "Error parsing LoadBalancingPolicy.*WrrLocality: "
                  "endpoint_picking_policy not found"));
}

TEST(XdsLbPolicyRegistryTest, WrrLocalityChildPolicyError) {
  WrrLocality wrr_locality;
  wrr_locality.mutable_endpoint_picking_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(RingHash());
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      wrr_locality);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(result.status().message()),
              ::testing::ContainsRegex(
                  "Error parsing LoadBalancingPolicy.*Error parsing "
                  "WrrLocality load balancing policy.*Error parsing "
                  "LoadBalancingPolicy.*Invalid hash function provided for "
                  "RingHash loadbalancing policy. Only XX_HASH is supported."));
}

TEST(XdsLbPolicyRegistryTest, WrrLocalityUnsupportedTypeSkipped) {
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
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  EXPECT_EQ((*result)[0], Json::Parse("{"
                                      "\"xds_wrr_locality_experimental\": {"
                                      "  \"child_policy\": [{"
                                      "    \"round_robin\": {}"
                                      "  }]"
                                      "}}")
                              .value());
}

class CustomLbPolicyFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args /* args */) const override {
    GPR_ASSERT(false);
    return nullptr;
  }

  absl::string_view name() const override { return "test.CustomLb"; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& /*json*/) const override {
    return nullptr;
  }
};

TEST(XdsLbPolicyRegistryTest, CustomLbPolicy) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("type.googleapis.com/test.CustomLb");
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  EXPECT_EQ((*result)[0], Json::Parse("{\"test.CustomLb\": null}").value());
}

TEST(XdsLbPolicyRegistryTest, CustomLbPolicyUdpaTyped) {
  ::udpa::type::v1::TypedStruct typed_struct;
  typed_struct.set_type_url("type.googleapis.com/test.CustomLb");
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  EXPECT_EQ((*result)[0], Json::Parse("{\"test.CustomLb\": null}").value());
}

TEST(XdsLbPolicyRegistryTest, UnsupportedCustomTypeError) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("myorg/foo/bar/test.UnknownLb");
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(result.status().message()),
      ::testing::HasSubstr("No supported load balancing policy config found"));
}

TEST(XdsLbPolicyRegistryTest, CustomTypeInvalidUrlMissingSlash) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("test.UnknownLb");
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(result.status().message()),
      ::testing::HasSubstr("Error parsing "
                           "LoadBalancingPolicy::Policy::TypedExtensionConfig::"
                           "typed_config: Invalid type_url test.UnknownLb"));
}

TEST(XdsLbPolicyRegistryTest, CustomTypeInvalidUrlEmptyType) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("myorg/");
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(result.status().message()),
      ::testing::HasSubstr("Error parsing "
                           "LoadBalancingPolicy::Policy::TypedExtensionConfig::"
                           "typed_config: Invalid type_url myorg/"));
}

TEST(XdsLbPolicyRegistryTest, CustomLbPolicyJsonConversion) {
  TypedStruct typed_struct;
  ASSERT_TRUE(grpc::protobuf::TextFormat::ParseFromString(
      R"pb(
        type_url: "type.googleapis.com/test.CustomLb"
        value {
          fields {
            key: "key"
            value { null_value: NULL_VALUE }
          }
          fields {
            key: "number"
            value { number_value: 123 }
          }
          fields {
            key: "string"
            value { string_value: "value" }
          }
          fields {
            key: "struct"
            value {
              struct_value {
                fields {
                  key: "key"
                  value { null_value: NULL_VALUE }
                }
              }
            }
          }
          fields {
            key: "list"
            value {
              list_value {
                values { null_value: NULL_VALUE }
                values { number_value: 234 }
              }
            }
          }
        }
      )pb",
      &typed_struct));
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  EXPECT_EQ((*result)[0], Json::Parse(
                              R"json({
                                "test.CustomLb":{
                                  "key": null,
                                  "number": 123,
                                  "string": "value",
                                  "struct": {
                                    "key": null
                                  },
                                  "list": [null, 234]
                                }
                              })json")
                              .value());
}

TEST(XdsLbPolicyRegistryTest, CustomLbPolicyListError) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("type.googleapis.com/test.CustomLb");
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  google::protobuf::Value value;
  value.mutable_list_value()->add_values();
  (*fields)["key"] = value;
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  auto result = ConvertXdsPolicy(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(result.status().message()),
              ::testing::HasSubstr(
                  "Error parsing LoadBalancingPolicy: Custom Policy: "
                  "test.CustomLb: Error parsing google::Protobuf::Struct: No "
                  "value set in Value proto"));
}

TEST(XdsLbPolicyRegistryTest, UnsupportedBuiltInTypeSkipped) {
  // Add two policies to list, an unsupported type and then a known RoundRobin
  // type. Expect that the unsupported type is skipped and RoundRobin is
  // selected.
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      LoadBalancingPolicyProto());
  lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      RoundRobin());
  auto result = ConvertXdsPolicy(policy);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  EXPECT_EQ((*result)[0], Json::Parse("{"
                                      "\"round_robin\": {}"
                                      "}")
                              .value());
}

TEST(XdsLbPolicyRegistryTest, UnsupportedCustomTypeSkipped) {
  // Add two policies to list, an unsupported custom type and then a known
  // RoundRobin type. Expect that the unsupported type is skipped and RoundRobin
  // is selected.
  TypedStruct typed_struct;
  typed_struct.set_type_url("myorg/foo/bar/test.UnknownLb");
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      RoundRobin());
  auto result = ConvertXdsPolicy(policy);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  EXPECT_EQ((*result)[0], Json::Parse("{"
                                      "\"round_robin\": {}"
                                      "}")
                              .value());
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
              ::testing::ContainsRegex("LoadBalancingPolicy configuration has "
                                       "a recursion depth of more than 16"));
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
            absl::make_unique<grpc_core::testing::CustomLbPolicyFactory>());
      });

  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
