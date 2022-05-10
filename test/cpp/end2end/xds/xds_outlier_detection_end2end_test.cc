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

class OutlierDetectionTest : public XdsEnd2endTest {
 protected:
  std::string CreateMetadataValueThatHashesToBackend(int index) {
    return absl::StrCat(ipv6_only_ ? "[::1]" : "127.0.0.1", ":",
                        backends_[index]->port(), "_0");
  }
};

INSTANTIATE_TEST_SUITE_P(XdsTest, OutlierDetectionTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

// Tests EjectionSuccessRate:
// 1. Use ring hash policy that hashes using a
// header value to ensure rpcs go to all backends.
// 2. Cause a single error on 1
// backend and wait for 1 outlier detection interval to pass.
// 3. We should skip
// exactly 1 backend due to ejection and all the loads sticky to that backend
// should go to 1 other backend.
TEST_P(OutlierDetectionTest, SuccessRate100Percent) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  // Setup outlier failure percentage parameters.
  // Any failure will cause an potential ejection with the probability of 100%
  // (to eliminate flakiness of the test).
  auto* duration = cluster.mutable_outlier_detection()->mutable_interval();
  duration->set_nanos(100000000 * grpc_test_slowdown_factor());
  cluster.mutable_outlier_detection()
      ->mutable_success_rate_stdev_factor()
      ->set_value(100);
  cluster.mutable_outlier_detection()
      ->mutable_enforcing_success_rate()
      ->set_value(100);
  cluster.mutable_outlier_detection()
      ->mutable_success_rate_minimum_hosts()
      ->set_value(1);
  cluster.mutable_outlier_detection()
      ->mutable_success_rate_request_volume()
      ->set_value(1);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contains a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  const auto rpc_options = RpcOptions().set_metadata(metadata);
  const auto rpc_options1 = RpcOptions().set_metadata(std::move(metadata1));
  WaitForBackend(DEBUG_LOCATION, 0, WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, WaitForBackendOptions(), rpc_options1);
  // Cause an error and wait for 1 outlier detection interval to pass
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions()
                  .set_metadata(std::move(metadata))
                  .set_server_expected_error(StatusCode::CANCELLED))
          .set_expected_error_code(StatusCode::CANCELLED));
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(100));
  ResetBackendCounters();
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  EXPECT_EQ(100, backends_[1]->backend_service()->request_count());
}

// We don't eject more than max_ejection_percent (default 10%) of the backends
// beyond the first one.
// TODO(donnadionne): implement stats
TEST_P(OutlierDetectionTest, SuccessRate100PercentMaxPercent10) {
  CreateAndStartBackends(4);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  // Setup outlier failure percentage parameters.
  // Any failure will cause an potential ejection with the probability of 100%
  // (to eliminate flakiness of the test).
  auto* duration = cluster.mutable_outlier_detection()->mutable_interval();
  duration->set_nanos(100000000 * grpc_test_slowdown_factor());
  cluster.mutable_outlier_detection()
      ->mutable_success_rate_stdev_factor()
      ->set_value(100);
  cluster.mutable_outlier_detection()
      ->mutable_enforcing_success_rate()
      ->set_value(100);
  cluster.mutable_outlier_detection()
      ->mutable_success_rate_minimum_hosts()
      ->set_value(1);
  cluster.mutable_outlier_detection()
      ->mutable_success_rate_request_volume()
      ->set_value(1);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contains a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  std::vector<std::pair<std::string, std::string>> metadata2 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(2)}};
  std::vector<std::pair<std::string, std::string>> metadata3 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(3)}};
  const auto rpc_options = RpcOptions().set_metadata(metadata);
  const auto rpc_options1 = RpcOptions().set_metadata(metadata1);
  const auto rpc_options2 = RpcOptions().set_metadata(metadata2);
  const auto rpc_options3 = RpcOptions().set_metadata(metadata3);
  WaitForBackend(DEBUG_LOCATION, 0, WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, WaitForBackendOptions(), rpc_options1);
  WaitForBackend(DEBUG_LOCATION, 2, WaitForBackendOptions(), rpc_options2);
  WaitForBackend(DEBUG_LOCATION, 3, WaitForBackendOptions(), rpc_options3);
  // Cause 2 error and wait for 1 outlier detection interval to pass to cause
  // the backend to be ejected.
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions()
                  .set_metadata(std::move(metadata))
                  .set_server_expected_error(StatusCode::CANCELLED))
          .set_expected_error_code(StatusCode::CANCELLED));
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions()
                  .set_metadata(std::move(metadata1))
                  .set_server_expected_error(StatusCode::CANCELLED))
          .set_expected_error_code(StatusCode::CANCELLED));
  CheckRpcSendOk(DEBUG_LOCATION, 1, rpc_options2);
  CheckRpcSendOk(DEBUG_LOCATION, 1, rpc_options3);
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(100));
  ResetBackendCounters();
  // 1 backend should be ejected, trafficed picked up by another backend.
  // No other backend should be ejected.
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options1);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options2);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options3);
  size_t empty_load_backend_count = 0;
  size_t double_load_backend_count = 0;
  size_t regular_load_backend_count = 0;
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (backends_[i]->backend_service()->request_count() == 0) {
      ++empty_load_backend_count;
    } else if (backends_[i]->backend_service()->request_count() == 200) {
      ++double_load_backend_count;
    } else if (backends_[i]->backend_service()->request_count() == 100) {
      ++regular_load_backend_count;
    } else {
      GPR_ASSERT(1);
    }
  }
  gpr_log(GPR_INFO, "donna backend 1 %d 2 %d 3 %d and 4 %d",
          backends_[0]->backend_service()->request_count(),
          backends_[1]->backend_service()->request_count(),
          backends_[2]->backend_service()->request_count(),
          backends_[3]->backend_service()->request_count());
  EXPECT_EQ(1, empty_load_backend_count);
  EXPECT_EQ(1, double_load_backend_count);
  EXPECT_EQ(2, regular_load_backend_count);
}

// Success rate does not eject if there are less than minimum_hosts backends
// Set success_rate_minimum_hosts to 3 when we only have 2 backends
TEST_P(OutlierDetectionTest, SuccessRate100PercentTooFewHosts) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  // Setup outlier failure percentage parameters.
  // Any failure will cause an potential ejection with the probability of 100%
  // (to eliminate flakiness of the test).
  auto* duration = cluster.mutable_outlier_detection()->mutable_interval();
  duration->set_nanos(100000000 * grpc_test_slowdown_factor());
  cluster.mutable_outlier_detection()
      ->mutable_success_rate_stdev_factor()
      ->set_value(100);
  cluster.mutable_outlier_detection()
      ->mutable_enforcing_success_rate()
      ->set_value(100);
  // Set success_rate_minimum_hosts to 3 when we only have 2 backends
  cluster.mutable_outlier_detection()
      ->mutable_success_rate_minimum_hosts()
      ->set_value(3);
  cluster.mutable_outlier_detection()
      ->mutable_success_rate_request_volume()
      ->set_value(1);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contains a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  const auto rpc_options = RpcOptions().set_metadata(metadata);
  const auto rpc_options1 = RpcOptions().set_metadata(std::move(metadata1));
  WaitForBackend(DEBUG_LOCATION, 0, WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, WaitForBackendOptions(), rpc_options1);
  // Cause an error and wait for 1 outlier detection interval to pass
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions()
                  .set_metadata(std::move(metadata))
                  .set_server_expected_error(StatusCode::CANCELLED))
          .set_expected_error_code(StatusCode::CANCELLED));
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(100));
  ResetBackendCounters();
  // All traffic still reaching the original backends and no backends are
  // ejected.
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options1);
  EXPECT_EQ(100, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(100, backends_[1]->backend_service()->request_count());
}

// Success rate does not eject if there are less than request_volume requests
// Set success_rate_request_volume to 4 when we only send 3 RPC in the
// interval.
TEST_P(OutlierDetectionTest, SuccessRate100PercentVolumeTooLow) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  // Setup outlier failure percentage parameters.
  // Any failure will cause an potential ejection with the probability of 100%
  // (to eliminate flakiness of the test).
  auto* duration = cluster.mutable_outlier_detection()->mutable_interval();
  duration->set_nanos(100000000 * grpc_test_slowdown_factor());
  cluster.mutable_outlier_detection()
      ->mutable_success_rate_stdev_factor()
      ->set_value(100);
  cluster.mutable_outlier_detection()
      ->mutable_enforcing_success_rate()
      ->set_value(100);
  cluster.mutable_outlier_detection()
      ->mutable_success_rate_minimum_hosts()
      ->set_value(1);
  // Set success_rate_request_volume to 2 when we only send 3 RPC in the
  // interval.
  cluster.mutable_outlier_detection()
      ->mutable_success_rate_request_volume()
      ->set_value(4);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contains a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  const auto rpc_options = RpcOptions().set_metadata(metadata);
  const auto rpc_options1 = RpcOptions().set_metadata(std::move(metadata1));
  WaitForBackend(DEBUG_LOCATION, 0, WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, WaitForBackendOptions(), rpc_options1);
  // Cause an error and wait for 1 outlier detection interval to pass
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions()
                  .set_metadata(std::move(metadata))
                  .set_server_expected_error(StatusCode::CANCELLED))
          .set_expected_error_code(StatusCode::CANCELLED));
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(100));
  ResetBackendCounters();
  // All traffic still reaching the original backends and no backends are
  // ejected.
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options1);
  EXPECT_EQ(100, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(100, backends_[1]->backend_service()->request_count());
}

// Tests EjectionFailurePercent:
// 1. Use ring hash policy that hashes using a
// header value to ensure rpcs go to all backends.
// 2. Cause a single error on 1
// backend and wait for 1 outlier detection interval to pass.
// 3. We should skip
// exactly 1 backend due to ejection and all the loads sticky to that backend
// should go to 1 other backend.
TEST_P(OutlierDetectionTest, FailurePercent100Percent) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  // Setup outlier failure percentage parameters.
  // Any failure will cause an potential ejection with the probability of 100%
  // (to eliminate flakiness of the test).
  auto* duration = cluster.mutable_outlier_detection()->mutable_interval();
  duration->set_nanos(100000000 * grpc_test_slowdown_factor());
  cluster.mutable_outlier_detection()
      ->mutable_failure_percentage_threshold()
      ->set_value(0);
  cluster.mutable_outlier_detection()
      ->mutable_enforcing_failure_percentage()
      ->set_value(100);
  cluster.mutable_outlier_detection()
      ->mutable_failure_percentage_minimum_hosts()
      ->set_value(1);
  cluster.mutable_outlier_detection()
      ->mutable_failure_percentage_request_volume()
      ->set_value(1);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contains a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  const auto rpc_options = RpcOptions().set_metadata(metadata);
  const auto rpc_options1 = RpcOptions().set_metadata(std::move(metadata1));
  WaitForBackend(DEBUG_LOCATION, 0, WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, WaitForBackendOptions(), rpc_options1);
  // Cause an error and wait for 1 outlier detection interval to pass to cause
  // the backend to be ejected.
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions()
                  .set_metadata(std::move(metadata))
                  .set_server_expected_error(StatusCode::CANCELLED))
          .set_expected_error_code(StatusCode::CANCELLED));
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(100));
  ResetBackendCounters();
  // All traffic going to the ejected backend should now all be going to the
  // other backend.
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  EXPECT_EQ(100, backends_[1]->backend_service()->request_count());
}

// We don't eject more than max_ejection_percent (default 10%) of the backends
// beyond the first one.
// TODO(donnadionne): implement stats
TEST_P(OutlierDetectionTest, FailurePercent100PercentMaxPercent10) {
  CreateAndStartBackends(4);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  // Setup outlier failure percentage parameters.
  // Any failure will cause an potential ejection with the probability of 100%
  // (to eliminate flakiness of the test).
  auto* duration = cluster.mutable_outlier_detection()->mutable_interval();
  duration->set_nanos(100000000 * grpc_test_slowdown_factor());
  cluster.mutable_outlier_detection()
      ->mutable_failure_percentage_threshold()
      ->set_value(0);
  cluster.mutable_outlier_detection()
      ->mutable_enforcing_failure_percentage()
      ->set_value(100);
  cluster.mutable_outlier_detection()
      ->mutable_failure_percentage_minimum_hosts()
      ->set_value(1);
  cluster.mutable_outlier_detection()
      ->mutable_failure_percentage_request_volume()
      ->set_value(1);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contains a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  std::vector<std::pair<std::string, std::string>> metadata2 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(2)}};
  std::vector<std::pair<std::string, std::string>> metadata3 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(3)}};
  const auto rpc_options = RpcOptions().set_metadata(metadata);
  const auto rpc_options1 = RpcOptions().set_metadata(metadata1);
  const auto rpc_options2 = RpcOptions().set_metadata(metadata2);
  const auto rpc_options3 = RpcOptions().set_metadata(metadata3);
  WaitForBackend(DEBUG_LOCATION, 0, WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, WaitForBackendOptions(), rpc_options1);
  WaitForBackend(DEBUG_LOCATION, 2, WaitForBackendOptions(), rpc_options2);
  WaitForBackend(DEBUG_LOCATION, 3, WaitForBackendOptions(), rpc_options3);
  // Cause 2 error and wait for 1 outlier detection interval to pass to cause
  // the backend to be ejected.
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions()
                  .set_metadata(std::move(metadata))
                  .set_server_expected_error(StatusCode::CANCELLED))
          .set_expected_error_code(StatusCode::CANCELLED));
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions()
                  .set_metadata(std::move(metadata1))
                  .set_server_expected_error(StatusCode::CANCELLED))
          .set_expected_error_code(StatusCode::CANCELLED));
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(100));
  ResetBackendCounters();
  // 1 backend should be ejected, trafficed picked up by another backend.
  // No other backend should be ejected.
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options1);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options2);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options3);
  size_t empty_load_backend_count = 0;
  size_t double_load_backend_count = 0;
  size_t regular_load_backend_count = 0;
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (backends_[i]->backend_service()->request_count() == 0) {
      ++empty_load_backend_count;
    } else if (backends_[i]->backend_service()->request_count() == 200) {
      ++double_load_backend_count;
    } else if (backends_[i]->backend_service()->request_count() == 100) {
      ++regular_load_backend_count;
    } else {
      GPR_ASSERT(1);
    }
  }
  gpr_log(GPR_INFO, "donna backend 1 %d 2 %d 3 %d and 4 %d",
          backends_[0]->backend_service()->request_count(),
          backends_[1]->backend_service()->request_count(),
          backends_[2]->backend_service()->request_count(),
          backends_[3]->backend_service()->request_count());
  EXPECT_EQ(1, empty_load_backend_count);
  EXPECT_EQ(1, double_load_backend_count);
  EXPECT_EQ(2, regular_load_backend_count);
}

// Failure percentage does not eject if there are less than minimum_hosts
// backends Set success_rate_minimum_hosts to 3 when we only have 2 backends
TEST_P(OutlierDetectionTest, FailurePercent100PercentTooFewHosts) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  // Setup outlier failure percentage parameters.
  // Any failure will cause an potential ejection with the probability of 100%
  // (to eliminate flakiness of the test).
  auto* duration = cluster.mutable_outlier_detection()->mutable_interval();
  duration->set_nanos(100000000 * grpc_test_slowdown_factor());
  cluster.mutable_outlier_detection()
      ->mutable_failure_percentage_threshold()
      ->set_value(0);
  cluster.mutable_outlier_detection()
      ->mutable_enforcing_failure_percentage()
      ->set_value(100);
  // Set success_rate_minimum_hosts to 3 when we only have 2 backends
  cluster.mutable_outlier_detection()
      ->mutable_failure_percentage_minimum_hosts()
      ->set_value(3);
  cluster.mutable_outlier_detection()
      ->mutable_failure_percentage_request_volume()
      ->set_value(1);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contains a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  const auto rpc_options = RpcOptions().set_metadata(metadata);
  const auto rpc_options1 = RpcOptions().set_metadata(std::move(metadata1));
  WaitForBackend(DEBUG_LOCATION, 0, WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, WaitForBackendOptions(), rpc_options1);
  // Cause an error and wait for 1 outlier detection interval to pass to cause
  // the backend to be ejected.
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions()
                  .set_metadata(std::move(metadata))
                  .set_server_expected_error(StatusCode::CANCELLED))
          .set_expected_error_code(StatusCode::CANCELLED));
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(100));
  ResetBackendCounters();
  // All traffic still reaching the original backends and no backends are
  // ejected.
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options1);
  EXPECT_EQ(100, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(100, backends_[1]->backend_service()->request_count());
}

// Failure percentage does not eject if there are less than request_volume
// requests
// Set success_rate_request_volume to 4 when we only send 3 RPC in the
// interval.
TEST_P(OutlierDetectionTest, FailurePercent100PercentVolumeTooLow) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  // Setup outlier failure percentage parameters.
  // Any failure will cause an potential ejection with the probability of 100%
  // (to eliminate flakiness of the test).
  auto* duration = cluster.mutable_outlier_detection()->mutable_interval();
  duration->set_nanos(100000000 * grpc_test_slowdown_factor());
  cluster.mutable_outlier_detection()
      ->mutable_failure_percentage_threshold()
      ->set_value(0);
  cluster.mutable_outlier_detection()
      ->mutable_enforcing_failure_percentage()
      ->set_value(100);
  cluster.mutable_outlier_detection()
      ->mutable_failure_percentage_minimum_hosts()
      ->set_value(1);
  // Set failure_percentage_request_volume to 4 when we only send 3 RPC in the
  // interval.
  cluster.mutable_outlier_detection()
      ->mutable_failure_percentage_request_volume()
      ->set_value(4);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contains a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  const auto rpc_options = RpcOptions().set_metadata(metadata);
  const auto rpc_options1 = RpcOptions().set_metadata(std::move(metadata1));
  WaitForBackend(DEBUG_LOCATION, 0, WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, WaitForBackendOptions(), rpc_options1);
  // Cause an error and wait for 1 outlier detection interval to pass to cause
  // the backend to be ejected.
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions()
                  .set_metadata(std::move(metadata))
                  .set_server_expected_error(StatusCode::CANCELLED))
          .set_expected_error_code(StatusCode::CANCELLED));
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(100));
  ResetBackendCounters();
  // All traffic still reaching the original backends and no backends are
  // ejected.
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options1);
  EXPECT_EQ(100, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(100, backends_[1]->backend_service()->request_count());
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
