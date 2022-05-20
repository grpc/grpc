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
#include "upb/upb.h"
#include "upb/upb.hpp"

#include <grpc/grpc.h>

#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/proto/grpc/testing/xds/v3/cluster.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/ring_hash.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/round_robin.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/typed_struct.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/wrr_locality.grpc.pb.h"
#include "test/core/util/test_config.h"

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
absl::StatusOr<Json::Array> ToJson(LoadBalancingPolicyProto policy) {
  std::string serialized_policy = policy.SerializeAsString();
  upb::Arena arena;
  auto* upb_policy = envoy_config_cluster_v3_LoadBalancingPolicy_parse(
      serialized_policy.data(), serialized_policy.size(), arena.ptr());
  return XdsLbPolicyRegistry::ToJson(upb_policy, arena.ptr());
}

TEST(XdsLbPolicyRegistryTest, EmptyLoadBalancingPolicy) {
  auto result = ToJson(LoadBalancingPolicyProto());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(StatusToString(result.status()),
              ::testing::HasSubstr("No supported LoadBalancingPolicy found"));
}

TEST(XdsLbPolicyRegistryTest, UnsupportedBuiltinType) {
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      LoadBalancingPolicyProto());
  auto result = ToJson(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(StatusToString(result.status()),
              ::testing::HasSubstr("No supported LoadBalancingPolicy found"));
}

TEST(XdsLbPolicyRegistryTest, MissingTypedExtensionConfig) {
  LoadBalancingPolicyProto policy;
  policy.add_policies();
  auto result = ToJson(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(StatusToString(result.status()),
              ::testing::HasSubstr("Error parsing LoadBalancingPolicy::Policy "
                                   "- Missing typed_extension_config field"));
}

TEST(XdsLbPolicyRegistryTest, MissingTypedConfig) {
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config();
  auto result = ToJson(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      StatusToString(result.status()),
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
  auto result = ToJson(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      StatusToString(result.status()),
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
  auto result = ToJson(policy);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  grpc_error_handle error = GRPC_ERROR_NONE;
  EXPECT_EQ((*result)[0], Json::Parse("{"
                                      "\"ring_hash_experimental\": {"
                                      "	\"minRingSize\": 1024,"
                                      "	\"maxRingSize\": 8388608"
                                      "}}",
                                      &error));
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
  auto result = ToJson(policy);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  grpc_error_handle error = GRPC_ERROR_NONE;
  EXPECT_EQ((*result)[0], Json::Parse("{"
                                      "\"ring_hash_experimental\": {"
                                      "	\"minRingSize\": 1234,"
                                      "	\"maxRingSize\": 4567"
                                      "}}",
                                      &error));
}

TEST(XdsLbPolicyRegistryTest, RingHashRingSizeLimits) {
  RingHash ring_hash;
  ring_hash.set_hash_function(RingHash::XX_HASH);
  ring_hash.mutable_minimum_ring_size()->set_value(1024 * 1024 * 1024);
  ring_hash.mutable_maximum_ring_size()->set_value(1024 * 1024 * 1024);
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      ring_hash);
  auto result = ToJson(policy);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  grpc_error_handle error = GRPC_ERROR_NONE;
  EXPECT_EQ((*result)[0], Json::Parse("{"
                                      "\"ring_hash_experimental\": {"
                                      "	\"minRingSize\": 8388608,"
                                      "	\"maxRingSize\": 8388608"
                                      "}}",
                                      &error));
}

TEST(XdsLbPolicyRegistryTest, RoundRobin) {
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      RoundRobin());
  auto result = ToJson(policy);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  grpc_error_handle error = GRPC_ERROR_NONE;
  EXPECT_EQ((*result)[0], Json::Parse("{"
                                      "\"round_robin\": {}"
                                      "}",
                                      &error));
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
  auto result = ToJson(policy);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  grpc_error_handle error = GRPC_ERROR_NONE;
  EXPECT_EQ((*result)[0], Json::Parse("{"
                                      "\"xds_wrr_locality_experimental\": {"
                                      "	\"child_policy\": [{"
                                      "		\"round_robin\": {}"
                                      "	}]"
                                      "}}",
                                      &error));
}

TEST(XdsLbPolicyRegistryTest, WrrLocalityMissingEndpointPickingPolicy) {
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      WrrLocality());
  auto result = ToJson(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(StatusToString(result.status()),
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
  auto result = ToJson(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(StatusToString(result.status()),
              ::testing::ContainsRegex(
                  "Error parsing LoadBalancingPolicy.*Error parsing "
                  "WrrLocality load balancing policy.*Error parsing "
                  "LoadBalancingPolicy.*Invalid hash function provided for "
                  "RingHash loadbalancing policy. Only XX_HASH is supported."));
}

class CustomLbPolicyFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args /* args */) const override {
    GPR_ASSERT(false);
    return nullptr;
  }

  const char* name() const override { return "test.CustomLb"; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& /* json */, grpc_error_handle* /* error */) const override {
    return nullptr;
  }
};

TEST(XdsLbPolicyRegistryTest, CustomLbPolicy) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("myorg/foo/bar/test.CustomLb");
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  auto result = ToJson(policy);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  grpc_error_handle error = GRPC_ERROR_NONE;
  EXPECT_EQ((*result)[0], Json::Parse("{"
                                      "\"test.CustomLb\": null}",
                                      &error));
}

TEST(XdsLbPolicyRegistryTest, CustomLbPolicyInvalidUrl) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("test.CustomLb");
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  auto result = ToJson(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(StatusToString(result.status()),
              ::testing::ContainsRegex(
                  "Error parsing LoadBalancingPolicy.*CustomLbPolicy: Invalid "
                  "type_url test.CustomLb"));
}

TEST(XdsLbPolicyRegistryTest, CustomLbPolicyInvalidValue) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("myorg/test.CustomLb");
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  (*fields)["key"];
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  auto result = ToJson(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      StatusToString(result.status()),
      ::testing::ContainsRegex(
          "Error parsing LoadBalancingPolicy.*Error parsing Custom load "
          "balancing policy myorg/test.CustomLb.*Failed to parse "
          "Struct.*Failed to parse value for key:key.*Invalid value type"));
}

TEST(XdsLbPolicyRegistryTest, CustomLbPolicyNullValue) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("myorg/foo/bar/test.CustomLb");
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  google::protobuf::Value value;
  value.set_null_value(google::protobuf::NullValue::NULL_VALUE);
  (*fields)["key"] = value;
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  auto result = ToJson(policy);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  grpc_error_handle error = GRPC_ERROR_NONE;
  EXPECT_EQ((*result)[0], Json::Parse("{"
                                      "	\"test.CustomLb\":{"
                                      "		\"key\": null"
                                      "	}"
                                      "}",
                                      &error));
}

TEST(XdsLbPolicyRegistryTest, CustomLbPolicyNumberValue) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("myorg/foo/bar/test.CustomLb");
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  google::protobuf::Value value;
  value.set_number_value(123);
  (*fields)["key"] = value;
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  auto result = ToJson(policy);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  grpc_error_handle error = GRPC_ERROR_NONE;
  EXPECT_EQ((*result)[0],
            Json::Parse(absl::StrFormat("{"
                                        "	\"test.CustomLb\":{"
                                        "		\"key\": %s"
                                        "	}"
                                        "}",
                                        std::to_string(double(123))),
                        &error));
}

TEST(XdsLbPolicyRegistryTest, CustomLbPolicyStringValue) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("myorg/foo/bar/test.CustomLb");
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  google::protobuf::Value value;
  value.set_string_value("value");
  (*fields)["key"] = value;
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  auto result = ToJson(policy);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  grpc_error_handle error = GRPC_ERROR_NONE;
  EXPECT_EQ((*result)[0], Json::Parse("{"
                                      "	\"test.CustomLb\":{"
                                      "		\"key\": \"value\""
                                      "	}"
                                      "}",
                                      &error));
}

TEST(XdsLbPolicyRegistryTest, CustomLbPolicyBoolValue) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("myorg/foo/bar/test.CustomLb");
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  google::protobuf::Value value;
  value.set_bool_value(true);
  (*fields)["key"] = value;
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  auto result = ToJson(policy);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  grpc_error_handle error = GRPC_ERROR_NONE;
  EXPECT_EQ((*result)[0], Json::Parse("{"
                                      "	\"test.CustomLb\":{"
                                      "		\"key\": true"
                                      "	}"
                                      "}",
                                      &error));
}

TEST(XdsLbPolicyRegistryTest, CustomLbPolicyStructValue) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("myorg/foo/bar/test.CustomLb");
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  google::protobuf::Value value;
  google::protobuf::Value inner_value;
  inner_value.set_bool_value(true);
  auto* inner_struct = value.mutable_struct_value()->mutable_fields();
  (*inner_struct)["inner_key"] = inner_value;
  (*inner_struct)["inner_key_2"] = inner_value;
  (*fields)["key"] = value;
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  auto result = ToJson(policy);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  grpc_error_handle error = GRPC_ERROR_NONE;
  EXPECT_EQ((*result)[0], Json::Parse("{"
                                      "	\"test.CustomLb\":{"
                                      "		\"key\": {"
                                      "			\"inner_key\": true,"
                                      "			\"inner_key_2\": true"
                                      "		}"
                                      "	}"
                                      "}",
                                      &error));
}

TEST(XdsLbPolicyRegistryTest, CustomLbPolicyListValue) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("myorg/foo/bar/test.CustomLb");
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  google::protobuf::Value value;
  value.mutable_list_value()->add_values()->set_bool_value(false);
  value.mutable_list_value()->add_values()->set_null_value(
      google::protobuf::NullValue::NULL_VALUE);
  (*fields)["key"] = value;
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  auto result = ToJson(policy);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->size(), 1);
  grpc_error_handle error = GRPC_ERROR_NONE;
  EXPECT_EQ((*result)[0], Json::Parse("{"
                                      "	\"test.CustomLb\":{"
                                      "		\"key\": [false, null]"
                                      "	}"
                                      "}",
                                      &error));
}

TEST(XdsLbPolicyRegistryTest, CustomLbPolicyListError) {
  TypedStruct typed_struct;
  typed_struct.set_type_url("myorg/test.CustomLb");
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  google::protobuf::Value value;
  value.mutable_list_value()->add_values();
  (*fields)["key"] = value;
  LoadBalancingPolicyProto policy;
  auto* lb_policy = policy.add_policies();
  lb_policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  auto result = ToJson(policy);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      StatusToString(result.status()),
      ::testing::ContainsRegex(
          "Error parsing LoadBalancingPolicy.*Error parsing Custom load "
          "balancing policy myorg/test.CustomLb.*Failed to parse "
          "Struct.*Failed to parse value for key:key.*Error parsing "
          "ListValue.*Invalid value type"));
}

// Build a recurse load balancing policy that goes beyond the max allowable
// depth of 16.
LoadBalancingPolicyProto BuildRecursiveLoadBalancingPolicy(int depth) {
  LoadBalancingPolicyProto policy;
  if (depth > 16) {
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
  auto result = ToJson(BuildRecursiveLoadBalancingPolicy(1));
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(StatusToString(result.status()),
              ::testing::ContainsRegex("LoadBalancingPolicy configuration has "
                                       "a recursion depth of more than 16"));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          absl::make_unique<grpc_core::testing::CustomLbPolicyFactory>());
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
