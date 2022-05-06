// Copyright 2017 gRPC authors.
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

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/proto/grpc/testing/xds/v3/cluster.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/fault.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/outlier_detection.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/router.grpc.pb.h"
#include "test/cpp/end2end/xds/no_op_http_filter.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::config::endpoint::v3::HealthStatus;
using std::chrono::system_clock;

using OutlierDetectionTest = XdsEnd2endTest;

INSTANTIATE_TEST_SUITE_P(XdsTest, OutlierDetectionTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

TEST_P(OutlierDetectionTest, HashingAccordingToWeightEjectFailurePercent50) {
  CreateAndStartBackends(2);
  const size_t kWeight1 = 1;
  const size_t kWeight2 = 2;
  const size_t kWeightTotal = kWeight1 + kWeight2;
  const double kWeight33Percent = static_cast<double>(kWeight1) / kWeightTotal;
  const double kWeight66Percent = static_cast<double>(kWeight2) / kWeightTotal;
  const double kErrorTolerance = 0.05;
  const uint32_t kRpcTimeoutMs = 10000;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kWeight33Percent, kErrorTolerance);
  auto cluster = default_cluster_;
  // Increasing min ring size for random distribution.
  cluster.mutable_ring_hash_lb_config()->mutable_minimum_ring_size()->set_value(
      100000);
  cluster.set_lb_policy(Cluster::RING_HASH);
  auto* duration = cluster.mutable_outlier_detection()->mutable_interval();
  duration->set_nanos(100000000);
  // Any failure will cause an potential ejection with the probability of 50%.
  cluster.mutable_outlier_detection()
      ->mutable_failure_percentage_threshold()
      ->set_value(0);
  cluster.mutable_outlier_detection()
      ->mutable_enforcing_failure_percentage()
      ->set_value(50);
  cluster.mutable_outlier_detection()
      ->mutable_failure_percentage_minimum_hosts()
      ->set_value(1);
  cluster.mutable_outlier_detection()
      ->mutable_failure_percentage_request_volume()
      ->set_value(1);
  balancer_->ads_service()->SetCdsResource(cluster);
  EdsResourceArgs args({{"locality0",
                         {CreateEndpoint(0, HealthStatus::UNKNOWN, 1),
                          CreateEndpoint(1, HealthStatus::UNKNOWN, 2)}}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  for (size_t i = 0; i < kNumRpcs; ++i) {
    SendRpc();
    gpr_log(GPR_INFO, "donna sent %d backend 0 got %d and backend 1 got %d", i,
            backends_[0]->backend_service()->request_count(),
            backends_[1]->backend_service()->request_count());
  }
  const int weight_33_request_count =
      backends_[0]->backend_service()->request_count();
  const int weight_66_request_count =
      backends_[1]->backend_service()->request_count();
  // Assuming outlier detection will eject 1 out of the 2 endpoints, the request
  // count at the backends should be more like 10-90 split instead of the 33-66
  // without outlier detection.
  EXPECT_THAT(static_cast<double>(weight_33_request_count) / kNumRpcs,
              ::testing::DoubleNear(0.9, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(weight_66_request_count) / kNumRpcs,
              ::testing::DoubleNear(0.1, kErrorTolerance));
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  // Make the backup poller poll very frequently in order to pick up
  // updates from all the subchannels's FDs.
  GPR_GLOBAL_CONFIG_SET(grpc_client_channel_backup_poll_interval_ms, 1);
#if TARGET_OS_IPHONE
  // Workaround Apple CFStream bug
  gpr_setenv("grpc_cfstream", "0");
#endif
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
