// Copyright 2026 gRPC authors.
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

#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"
#include "xds/type/v3/typed_struct.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc {
namespace testing {
namespace {

using ::xds::type::v3::TypedStruct;

class RandomSubsettingTest : public XdsEnd2endTest {
 protected:
  void SetUp() override {
    InitClient();
    ResetStub();
  }
};

// Run both with and without load reporting, just for test coverage.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, RandomSubsettingTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_enable_load_reporting()),
    &XdsTestType::Name);

TEST_P(RandomSubsettingTest, Basic) {
  CreateAndStartBackends(4);
  auto cluster = default_cluster_;
  // Create custom LB policy config using TypedStruct
  TypedStruct typed_struct;
  typed_struct.set_type_url("type.googleapis.com/random_subsetting");
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  (*fields)["subset_size"].set_number_value(2);
  // child_policy should be a list of policies in gRPC JSON.
  // We use the format that makes it painless to morph into option 1.
  auto* child_policy_list = (*fields)["childPolicy"].mutable_list_value();
  auto* child_policy_obj =
      child_policy_list->add_values()->mutable_struct_value();
  (*child_policy_obj->mutable_fields())["round_robin"]
      .mutable_struct_value();  // empty struct for round_robin
  auto* policy = cluster.mutable_load_balancing_policy()->add_policies();
  policy->mutable_typed_extension_config()->set_name("random_subsetting");
  policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  balancer_->ads_service()->SetCdsResource(cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Send RPCs and verify
  CheckRpcSendOk(DEBUG_LOCATION, 100);
  int active_backends = 0;
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (backends_[i]->backend_service()->request_count() > 0) {
      active_backends++;
    }
  }
  // We expect at most 2 backends to receive traffic because subset_size is 2.
  EXPECT_LE(active_backends, 2);
  EXPECT_GT(active_backends, 0);
}

TEST_P(RandomSubsettingTest, SubsetSizeLargerThanEndpoints) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  TypedStruct typed_struct;
  typed_struct.set_type_url("type.googleapis.com/random_subsetting");
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  (*fields)["subset_size"].set_number_value(5);  // Larger than 2
  auto* child_policy_list = (*fields)["childPolicy"].mutable_list_value();
  auto* child_policy_obj =
      child_policy_list->add_values()->mutable_struct_value();
  (*child_policy_obj->mutable_fields())["round_robin"].mutable_struct_value();
  auto* policy = cluster.mutable_load_balancing_policy()->add_policies();
  policy->mutable_typed_extension_config()->set_name("random_subsetting");
  policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  balancer_->ads_service()->SetCdsResource(cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  CheckRpcSendOk(DEBUG_LOCATION, 100);
  int active_backends = 0;
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (backends_[i]->backend_service()->request_count() > 0) {
      active_backends++;
    }
  }
  // We expect all 2 backends to receive traffic because subset_size (5) >=
  // endpoints (2).
  EXPECT_EQ(active_backends, 2);
}

TEST_P(RandomSubsettingTest, ZeroSubsetSize) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  TypedStruct typed_struct;
  typed_struct.set_type_url("type.googleapis.com/random_subsetting");
  auto* fields = typed_struct.mutable_value()->mutable_fields();
  (*fields)["subset_size"].set_number_value(0);  // Invalid
  auto* child_policy_list = (*fields)["childPolicy"].mutable_list_value();
  auto* child_policy_obj =
      child_policy_list->add_values()->mutable_struct_value();
  (*child_policy_obj->mutable_fields())["round_robin"].mutable_struct_value();
  auto* policy = cluster.mutable_load_balancing_policy()->add_policies();
  policy->mutable_typed_extension_config()->set_name("random_subsetting");
  policy->mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
      typed_struct);
  balancer_->ads_service()->SetCdsResource(cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // We expect RPCs to fail with UNAVAILABLE and the error message from
  // validation.
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      ".*errors validating random_subsetting LB policy config.*");
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
