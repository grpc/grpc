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

#include "absl/log/check.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/cluster/v3/outlier_detection.pb.h"
#include "envoy/extensions/filters/http/fault/v3/fault.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/client_channel/backup_poller.h"
#include "src/core/config/config_vars.h"
#include "test/core/test_util/resolve_localhost_ip46.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"

namespace grpc {
namespace testing {
namespace {

class OutlierDetectionTest : public XdsEnd2endTest {
 protected:
  std::string CreateMetadataValueThatHashesToBackend(int index) {
    return absl::StrCat(grpc_core::LocalIp(), ":", backends_[index]->port(),
                        "_0");
  }
};

INSTANTIATE_TEST_SUITE_P(XdsTest, OutlierDetectionTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);
// TODO(donnadionne): add non-xds test a new
// test/cpp/end2end/outlier_detection_end2end_test.cc

// Tests SuccessRateEjectionAndUnejection:
// 1. Use ring hash policy that hashes using a header value to ensure rpcs
//    go to all backends.
// 2. Cause a single error on 1 backend and wait for 1 outlier detection
//    interval to pass.
// 3. We should skip exactly 1 backend due to ejection and all the loads
//    sticky to that backend should go to 1 other backend.
// 4. Let the ejection period pass and verify we can go back to both backends
//    after the uneject.
TEST_P(OutlierDetectionTest, SuccessRateEjectionAndUnejection) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  // Setup outlier failure percentage parameters.
  // Any failure will cause an potential ejection with the probability of 100%
  // (to eliminate flakiness of the test).
  auto* outlier_detection = cluster.mutable_outlier_detection();
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_interval());
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_base_ejection_time());
  outlier_detection->mutable_success_rate_stdev_factor()->set_value(100);
  outlier_detection->mutable_enforcing_success_rate()->set_value(100);
  outlier_detection->mutable_success_rate_minimum_hosts()->set_value(1);
  outlier_detection->mutable_success_rate_request_volume()->set_value(1);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contain a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  const auto rpc_options = RpcOptions().set_metadata(metadata);
  const auto rpc_options1 = RpcOptions().set_metadata(std::move(metadata1));
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options1);
  // Trigger an error to backend 0.
  // The success rate enforcement_percentage is 100%, so this will cause
  // the backend to be ejected when the ejection timer fires.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::CANCELLED, "",
                      RpcOptions()
                          .set_metadata(std::move(metadata))
                          .set_server_expected_error(StatusCode::CANCELLED));
  // Wait for traffic aimed at backend 0 to start going to backend 1.
  // This tells us that backend 0 has been ejected.
  // It should take no more than one ejection timer interval.
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions().set_timeout_ms(
                     3000 * grpc_test_slowdown_factor()),
                 rpc_options);
  // Now wait for traffic aimed at backend 0 to switch back to backend 0.
  // This tells us that backend 0 has been unejected.
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions().set_timeout_ms(
                     3000 * grpc_test_slowdown_factor()),
                 rpc_options);
}

// We don't eject more than max_ejection_percent (default 10%) of the backends
// beyond the first one.
TEST_P(OutlierDetectionTest, SuccessRateMaxPercent) {
  CreateAndStartBackends(4);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  // Setup outlier failure percentage parameters.
  // Any failure will cause an potential ejection with the probability of 100%
  // (to eliminate flakiness of the test).
  auto* outlier_detection = cluster.mutable_outlier_detection();
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_interval());
  outlier_detection->mutable_success_rate_stdev_factor()->set_value(100);
  outlier_detection->mutable_enforcing_success_rate()->set_value(100);
  outlier_detection->mutable_success_rate_minimum_hosts()->set_value(1);
  outlier_detection->mutable_success_rate_request_volume()->set_value(1);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contain a header value that will always be
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
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options1);
  WaitForBackend(DEBUG_LOCATION, 2, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options2);
  WaitForBackend(DEBUG_LOCATION, 3, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options3);
  // Cause 2 errors and wait until one ejection happens.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::CANCELLED, "",
                      RpcOptions()
                          .set_metadata(std::move(metadata))
                          .set_server_expected_error(StatusCode::CANCELLED));
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::CANCELLED, "",
                      RpcOptions()
                          .set_metadata(std::move(metadata1))
                          .set_server_expected_error(StatusCode::CANCELLED));
  absl::Time deadline =
      absl::Now() + absl::Seconds(3) * grpc_test_slowdown_factor();
  while (true) {
    ResetBackendCounters();
    CheckRpcSendOk(DEBUG_LOCATION, 1, rpc_options);
    CheckRpcSendOk(DEBUG_LOCATION, 1, rpc_options1);
    if (!SeenAllBackends(0, 2)) {
      break;
    }
    EXPECT_LE(absl::Now(), deadline);
    if (absl::Now() >= deadline) break;
  }
  // 1 backend should be ejected, traffic picked up by another backend.
  // No other backend should be ejected.
  ResetBackendCounters();
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
      CHECK(1);
    }
  }
  EXPECT_EQ(1, empty_load_backend_count);
  EXPECT_EQ(1, double_load_backend_count);
  EXPECT_EQ(2, regular_load_backend_count);
}

// Success rate stdev_factor is honored, a higher value would ensure ejection
// does not occur.
TEST_P(OutlierDetectionTest, SuccessRateStdevFactor) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  // Setup outlier failure percentage parameters.
  // Any failure will cause an potential ejection with the probability of 100%
  // (to eliminate flakiness of the test).
  auto* outlier_detection = cluster.mutable_outlier_detection();
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_interval());
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_base_ejection_time());
  // We know a stdev factor of 100 will ensure the ejection occurs, so setting
  // it to something higher like 1000 to test that ejection will not occur.
  // Note this parameter is the only difference between this test and
  // SuccessRateEjectionAndUnejection (ejection portion, value set to 100) and
  // this one value changes means the difference between not ejecting in this
  // test and ejecting in the other test.
  outlier_detection->mutable_success_rate_stdev_factor()->set_value(1000);
  outlier_detection->mutable_enforcing_success_rate()->set_value(100);
  outlier_detection->mutable_success_rate_minimum_hosts()->set_value(1);
  outlier_detection->mutable_success_rate_request_volume()->set_value(1);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contain a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  const auto rpc_options = RpcOptions().set_metadata(metadata);
  const auto rpc_options1 = RpcOptions().set_metadata(std::move(metadata1));
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options1);
  // Cause an error and wait for 1 outlier detection interval to pass
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::CANCELLED, "",
                      RpcOptions()
                          .set_metadata(std::move(metadata))
                          .set_server_expected_error(StatusCode::CANCELLED));
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(
      3000 * grpc_test_slowdown_factor()));
  ResetBackendCounters();
  // 1 backend experienced failure, but since the stdev_factor is high, no
  // backend will be noticed as an outlier so no ejection.
  // Both backends are still getting the RPCs intended for them.
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options1);
  EXPECT_EQ(100, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(100, backends_[1]->backend_service()->request_count());
}

// Success rate enforcement percentage is honored, setting it to 0 so guarantee
// the randomized number between 1 to 100 will always be great, so nothing will
// be ejected.
TEST_P(OutlierDetectionTest, SuccessRateEnforcementPercentage) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  auto* outlier_detection = cluster.mutable_outlier_detection();
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_interval());
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_base_ejection_time());
  outlier_detection->mutable_success_rate_stdev_factor()->set_value(100);
  // Setting enforcing_success_rate to 0 to ensure we will never eject.
  // Note this parameter is the only difference between this test and
  // SuccessRateEjectionAndUnejection (ejection portion, value set to 100) and
  // this one value changes means the difference between guaranteed not ejecting
  // in this test and guaranteed ejecting in the other test.
  outlier_detection->mutable_enforcing_success_rate()->set_value(0);
  outlier_detection->mutable_success_rate_minimum_hosts()->set_value(1);
  outlier_detection->mutable_success_rate_request_volume()->set_value(1);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contain a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  const auto rpc_options = RpcOptions().set_metadata(metadata);
  const auto rpc_options1 = RpcOptions().set_metadata(std::move(metadata1));
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options1);
  // Cause an error and wait for 1 outlier detection interval to pass
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::CANCELLED, "",
                      RpcOptions()
                          .set_metadata(std::move(metadata))
                          .set_server_expected_error(StatusCode::CANCELLED));
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(
      3000 * grpc_test_slowdown_factor()));
  ResetBackendCounters();
  // 1 backend experienced failure, but since the enforcement percentage is 0,
  // no backend will be ejected. Both backends are still getting the RPCs
  // intended for them.
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options1);
  EXPECT_EQ(100, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(100, backends_[1]->backend_service()->request_count());
}

// Success rate does not eject if there are less than minimum_hosts backends
// Set success_rate_minimum_hosts to 3 when we only have 2 backends
TEST_P(OutlierDetectionTest, SuccessRateMinimumHosts) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  // Setup outlier failure percentage parameters.
  // Any failure will cause an potential ejection with the probability of 100%
  // (to eliminate flakiness of the test).
  auto* outlier_detection = cluster.mutable_outlier_detection();
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_interval());
  outlier_detection->mutable_success_rate_stdev_factor()->set_value(100);
  outlier_detection->mutable_enforcing_success_rate()->set_value(100);
  // Set success_rate_minimum_hosts to 3 when we only have 2 backends
  // Note this parameter is the only difference between this test and
  // SuccessRateEjectionAndUnejection (ejection portion, value set to 1) and
  // this one value changes means the difference between not ejecting in this
  // test and ejecting in the other test.
  outlier_detection->mutable_success_rate_minimum_hosts()->set_value(3);
  outlier_detection->mutable_success_rate_request_volume()->set_value(1);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contain a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  const auto rpc_options = RpcOptions().set_metadata(metadata);
  const auto rpc_options1 = RpcOptions().set_metadata(std::move(metadata1));
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options1);
  // Cause an error and wait for 1 outlier detection interval to pass
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::CANCELLED, "",
                      RpcOptions()
                          .set_metadata(std::move(metadata))
                          .set_server_expected_error(StatusCode::CANCELLED));
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(
      3000 * grpc_test_slowdown_factor()));
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
TEST_P(OutlierDetectionTest, SuccessRateRequestVolume) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  // Setup outlier failure percentage parameters.
  // Any failure will cause an potential ejection with the probability of 100%
  // (to eliminate flakiness of the test).
  auto* outlier_detection = cluster.mutable_outlier_detection();
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_interval());
  outlier_detection->mutable_success_rate_stdev_factor()->set_value(100);
  outlier_detection->mutable_enforcing_success_rate()->set_value(100);
  outlier_detection->mutable_success_rate_minimum_hosts()->set_value(1);
  // Set success_rate_request_volume to 4 when we only send 3 RPC in the
  // interval.
  // Note this parameter is the only difference between this test and
  // SuccessRateEjectionAndUnejection (ejection portion, value set to 1) and
  // this one value changes means the difference between not ejecting in this
  // test and ejecting in the other test.
  outlier_detection->mutable_success_rate_request_volume()->set_value(4);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contain a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  const auto rpc_options = RpcOptions().set_metadata(metadata);
  const auto rpc_options1 = RpcOptions().set_metadata(std::move(metadata1));
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options1);
  // Cause an error and wait for 1 outlier detection interval to pass
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::CANCELLED, "",
                      RpcOptions()
                          .set_metadata(std::move(metadata))
                          .set_server_expected_error(StatusCode::CANCELLED));
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(
      3000 * grpc_test_slowdown_factor()));
  ResetBackendCounters();
  // All traffic still reaching the original backends and no backends are
  // ejected.
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options1);
  EXPECT_EQ(100, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(100, backends_[1]->backend_service()->request_count());
}

// Tests FailurePercentageEjectionAndUnejection:
// 1. Use ring hash policy that hashes using a header value to ensure RPCs
//    go to all backends.
// 2. Cause a single error on 1 backend and wait for 1 outlier detection
//    interval to pass.
// 3. We should skip exactly 1 backend due to ejection and all the loads
//    sticky to that backend should go to 1 other backend.
// 4. Let the ejection period pass and verify that traffic will again go both
//    backends as we have unejected the backend.
TEST_P(OutlierDetectionTest, FailurePercentageEjectionAndUnejection) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  // Setup outlier failure percentage parameters.
  // Any failure will cause an potential ejection with the probability of 100%
  // (to eliminate flakiness of the test).
  auto* outlier_detection = cluster.mutable_outlier_detection();
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_interval());
  SetProtoDuration(grpc_core::Duration::Seconds(3),
                   outlier_detection->mutable_base_ejection_time());
  outlier_detection->mutable_failure_percentage_threshold()->set_value(0);
  outlier_detection->mutable_enforcing_failure_percentage()->set_value(100);
  outlier_detection->mutable_failure_percentage_minimum_hosts()->set_value(1);
  outlier_detection->mutable_failure_percentage_request_volume()->set_value(1);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contain a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  const auto rpc_options = RpcOptions().set_metadata(metadata);
  const auto rpc_options1 = RpcOptions().set_metadata(std::move(metadata1));
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options1);
  // Cause an error and wait for traffic aimed at backend 0 to start going to
  // backend 1.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::CANCELLED, "",
                      RpcOptions()
                          .set_metadata(std::move(metadata))
                          .set_server_expected_error(StatusCode::CANCELLED));
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions().set_timeout_ms(
                     3000 * grpc_test_slowdown_factor()),
                 rpc_options);
  // 1 backend is ejected all traffic going to the ejected backend should now
  // all be going to the other backend.
  // failure percentage enforcement_percentage of 100% is honored as this test
  // will consistently reject 1 backend.
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  EXPECT_EQ(100, backends_[1]->backend_service()->request_count());
  // Now wait for traffic aimed at backend 0 to switch back to backend 0.
  // This tells us that backend 0 has been unejected.
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions().set_timeout_ms(
                     30000 * grpc_test_slowdown_factor()),
                 rpc_options);
  // Verify that rpcs go to their expectedly hashed backends.
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options1);
  EXPECT_EQ(100, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(100, backends_[1]->backend_service()->request_count());
}

// We don't eject more than max_ejection_percent (default 10%) of the backends
// beyond the first one.
TEST_P(OutlierDetectionTest, FailurePercentageMaxPercentage) {
  CreateAndStartBackends(4);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  // Setup outlier failure percentage parameters.
  // Any failure will cause an potential ejection with the probability of 100%
  // (to eliminate flakiness of the test).
  auto* outlier_detection = cluster.mutable_outlier_detection();
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_interval());
  outlier_detection->mutable_failure_percentage_threshold()->set_value(0);
  outlier_detection->mutable_enforcing_failure_percentage()->set_value(100);
  outlier_detection->mutable_failure_percentage_minimum_hosts()->set_value(1);
  outlier_detection->mutable_failure_percentage_request_volume()->set_value(1);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contain a header value that will always be
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
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options1);
  WaitForBackend(DEBUG_LOCATION, 2, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options2);
  WaitForBackend(DEBUG_LOCATION, 3, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options3);
  // Cause 2 errors and wait until one ejection happens.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::CANCELLED, "",
                      RpcOptions()
                          .set_metadata(std::move(metadata))
                          .set_server_expected_error(StatusCode::CANCELLED));
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::CANCELLED, "",
                      RpcOptions()
                          .set_metadata(std::move(metadata1))
                          .set_server_expected_error(StatusCode::CANCELLED));
  absl::Time deadline =
      absl::Now() + absl::Seconds(3) * grpc_test_slowdown_factor();
  while (true) {
    ResetBackendCounters();
    CheckRpcSendOk(DEBUG_LOCATION, 1, rpc_options);
    CheckRpcSendOk(DEBUG_LOCATION, 1, rpc_options1);
    if (!SeenAllBackends(0, 2)) {
      break;
    }
    EXPECT_LE(absl::Now(), deadline);
    if (absl::Now() >= deadline) break;
  }
  // 1 backend should be ejected, traffic picked up by another backend.
  // No other backend should be ejected.
  ResetBackendCounters();
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
      CHECK(1);
    }
  }
  EXPECT_EQ(1, empty_load_backend_count);
  EXPECT_EQ(1, double_load_backend_count);
  EXPECT_EQ(2, regular_load_backend_count);
}

// Failure percentage threshold is honored, a higher value would ensure ejection
// does not occur
TEST_P(OutlierDetectionTest, FailurePercentageThreshold) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  auto* outlier_detection = cluster.mutable_outlier_detection();
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_interval());
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_base_ejection_time());
  // Setup outlier failure percentage parameter to 50
  // Note this parameter is the only difference between this test and
  // FailurePercentageEjectionAndUnejection (ejection portion, value set to 0)
  // and this one value changes means the difference between not ejecting in
  // this test and ejecting in the other test.
  outlier_detection->mutable_failure_percentage_threshold()->set_value(50);
  outlier_detection->mutable_enforcing_failure_percentage()->set_value(100);
  outlier_detection->mutable_failure_percentage_minimum_hosts()->set_value(1);
  outlier_detection->mutable_failure_percentage_request_volume()->set_value(1);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contain a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  const auto rpc_options = RpcOptions().set_metadata(metadata);
  const auto rpc_options1 = RpcOptions().set_metadata(std::move(metadata1));
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options1);
  // Cause an error and wait for 1 outlier detection interval to pass to cause
  // the backend to be ejected.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::CANCELLED, "",
                      RpcOptions()
                          .set_metadata(std::move(metadata))
                          .set_server_expected_error(StatusCode::CANCELLED));
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(
      3000 * grpc_test_slowdown_factor()));
  ResetBackendCounters();
  // 1 backend experienced 1 failure, but since the threshold is 50 % no
  // backend will be noticed as an outlier so no ejection.
  // Both backends are still getting the RPCs intended for them.
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options1);
  EXPECT_EQ(100, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(100, backends_[1]->backend_service()->request_count());
}

// Failure percentage enforcement percentage is honored, setting it to 0 so
// guarantee the randomized number between 1 to 100 will always be great, so
// nothing will be ejected.
TEST_P(OutlierDetectionTest, FailurePercentageEnforcementPercentage) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  auto* outlier_detection = cluster.mutable_outlier_detection();
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_interval());
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_base_ejection_time());
  outlier_detection->mutable_failure_percentage_threshold()->set_value(0);
  // Setting enforcing_success_rate to 0 to ensure we will never eject.
  // Note this parameter is the only difference between this test and
  // FailurePercentageEjectionAndUnejection (ejection portion, value set to 100)
  // and this one value changes means the difference between guaranteed not
  // ejecting in this test and guaranteed ejecting in the other test.
  outlier_detection->mutable_enforcing_failure_percentage()->set_value(0);
  outlier_detection->mutable_failure_percentage_minimum_hosts()->set_value(1);
  outlier_detection->mutable_failure_percentage_request_volume()->set_value(1);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contain a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  const auto rpc_options = RpcOptions().set_metadata(metadata);
  const auto rpc_options1 = RpcOptions().set_metadata(std::move(metadata1));
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options1);
  // Cause an error and wait for 1 outlier detection interval to pass to cause
  // the backend to be ejected.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::CANCELLED, "",
                      RpcOptions()
                          .set_metadata(std::move(metadata))
                          .set_server_expected_error(StatusCode::CANCELLED));
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(
      3000 * grpc_test_slowdown_factor()));
  ResetBackendCounters();
  // 1 backend experienced failure, but since the enforcement percentage is 0,
  // no backend will be ejected. Both backends are still getting the RPCs
  // intended for them.
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options1);
  EXPECT_EQ(100, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(100, backends_[1]->backend_service()->request_count());
}

// Failure percentage does not eject if there are less than minimum_hosts
// backends Set success_rate_minimum_hosts to 3 when we only have 2 backends
TEST_P(OutlierDetectionTest, FailurePercentageMinimumHosts) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  // Setup outlier failure percentage parameters.
  // Any failure will cause an potential ejection with the probability of 100%
  // (to eliminate flakiness of the test).
  auto* outlier_detection = cluster.mutable_outlier_detection();
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_interval());
  outlier_detection->mutable_failure_percentage_threshold()->set_value(0);
  outlier_detection->mutable_enforcing_failure_percentage()->set_value(100);
  // Set failure_percentage_minimum_hosts to 3 when we only have 2 backends
  // Note this parameter is the only difference between this test and
  // FailurePercentageEjectionAndUnejection (ejection portion, value set to 1)
  // and this one value changes means the difference between not ejecting in
  // this test and ejecting in the other test.
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
  // Note each type of RPC will contain a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  const auto rpc_options = RpcOptions().set_metadata(metadata);
  const auto rpc_options1 = RpcOptions().set_metadata(std::move(metadata1));
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options1);
  // Cause an error and wait for 1 outlier detection interval to pass to cause
  // the backend to be ejected.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::CANCELLED, "",
                      RpcOptions()
                          .set_metadata(std::move(metadata))
                          .set_server_expected_error(StatusCode::CANCELLED));
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(
      3000 * grpc_test_slowdown_factor()));
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
TEST_P(OutlierDetectionTest, FailurePercentageRequestVolume) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  // Setup outlier failure percentage parameters.
  // Any failure will cause an potential ejection with the probability of 100%
  // (to eliminate flakiness of the test).
  auto* outlier_detection = cluster.mutable_outlier_detection();
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_interval());
  outlier_detection->mutable_failure_percentage_threshold()->set_value(0);
  outlier_detection->mutable_enforcing_failure_percentage()->set_value(100);
  outlier_detection->mutable_failure_percentage_minimum_hosts()->set_value(1);
  // Set failure_percentage_request_volume to 4 when we only send 3 RPC in the
  // interval.
  // // Note this parameter is the only difference between this test and
  // FailurePercentageEjectionAndUnejection (ejection portion, value set to 1)
  // and this one value changes means the difference between not ejecting in
  // this test and ejecting in the other test.
  outlier_detection->mutable_failure_percentage_request_volume()->set_value(4);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contain a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  const auto rpc_options = RpcOptions().set_metadata(metadata);
  const auto rpc_options1 = RpcOptions().set_metadata(std::move(metadata1));
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options1);
  // Cause an error and wait for 1 outlier detection interval to pass to cause
  // the backend to be ejected.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::CANCELLED, "",
                      RpcOptions()
                          .set_metadata(std::move(metadata))
                          .set_server_expected_error(StatusCode::CANCELLED));
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(
      3000 * grpc_test_slowdown_factor()));
  ResetBackendCounters();
  // All traffic still reaching the original backends and no backends are
  // ejected.
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options1);
  EXPECT_EQ(100, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(100, backends_[1]->backend_service()->request_count());
}

// Tests SuccessRate and FailurePercentage both configured
// Configure max_ejection_percent to 50% which means max 2/4 backends can be
// ejected.
// Configure success rate to eject 1 and failure percentage to eject 2.
// Verify that maximum 2 backends are ejected, not 3!
TEST_P(OutlierDetectionTest, SuccessRateAndFailurePercentage) {
  CreateAndStartBackends(4);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  // Setup outlier failure percentage parameters.
  // Any failure will cause an potential ejection with the probability of 100%
  // (to eliminate flakiness of the test).
  auto* outlier_detection = cluster.mutable_outlier_detection();
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_interval());
  outlier_detection->mutable_max_ejection_percent()->set_value(50);
  // This stdev of 500 will ensure the number of ok RPC and error RPC we send
  // will make 1 outlier out of the 4 backends.
  outlier_detection->mutable_success_rate_stdev_factor()->set_value(500);
  outlier_detection->mutable_enforcing_success_rate()->set_value(100);
  outlier_detection->mutable_success_rate_minimum_hosts()->set_value(1);
  outlier_detection->mutable_success_rate_request_volume()->set_value(1);
  outlier_detection->mutable_failure_percentage_threshold()->set_value(0);
  outlier_detection->mutable_enforcing_failure_percentage()->set_value(100);
  outlier_detection->mutable_failure_percentage_minimum_hosts()->set_value(1);
  outlier_detection->mutable_failure_percentage_request_volume()->set_value(1);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contain a header value that will always be
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
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options1);
  WaitForBackend(DEBUG_LOCATION, 2, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options2);
  WaitForBackend(DEBUG_LOCATION, 3, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options3);
  // Cause 2 errors on 1 backend and 1 error on 2 backends and wait for 2
  // backends to be ejected. The 2 errors to the 1 backend will make exactly 1
  // outlier from the success rate algorithm; all 4 errors will make 3 outliers
  // from the failure percentage algorithm because the threshold is set to 0. I
  // have verified through debug logs we eject 1 backend because of success
  // rate, 1 backend because of failure percentage; but as we attempt to eject
  // another backend because of failure percentage we will stop as we have
  // reached our 50% limit.
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::CANCELLED, "",
      RpcOptions().set_metadata(metadata).set_server_expected_error(
          StatusCode::CANCELLED));
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::CANCELLED, "",
      RpcOptions().set_metadata(metadata).set_server_expected_error(
          StatusCode::CANCELLED));
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::CANCELLED, "",
      RpcOptions().set_metadata(metadata1).set_server_expected_error(
          StatusCode::CANCELLED));
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::CANCELLED, "",
      RpcOptions().set_metadata(metadata2).set_server_expected_error(
          StatusCode::CANCELLED));
  absl::Time deadline =
      absl::Now() + absl::Seconds(3) * grpc_test_slowdown_factor();
  std::vector<size_t> idx = {0, 1, 2, 3};
  while (true) {
    ResetBackendCounters();
    CheckRpcSendOk(DEBUG_LOCATION, 1, rpc_options);
    CheckRpcSendOk(DEBUG_LOCATION, 1, rpc_options1);
    CheckRpcSendOk(DEBUG_LOCATION, 1, rpc_options2);
    CheckRpcSendOk(DEBUG_LOCATION, 1, rpc_options3);
    if (std::count_if(idx.begin(), idx.end(),
                      [this](size_t i) { return SeenBackend(i); }) == 2) {
      break;
    }
    EXPECT_LE(absl::Now(), deadline);
    if (absl::Now() >= deadline) break;
  }
  ResetBackendCounters();
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options1);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options2);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options3);
  size_t empty_load_backend_count = 0;
  size_t double_load_backend_count = 0;
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (backends_[i]->backend_service()->request_count() == 0) {
      ++empty_load_backend_count;
    } else if (backends_[i]->backend_service()->request_count() >= 100) {
      // The extra load could go to 2 remaining backends or just 1 of them.
      ++double_load_backend_count;
    } else if (backends_[i]->backend_service()->request_count() > 300) {
      CHECK(1);
    }
  }
  EXPECT_EQ(2, empty_load_backend_count);
  EXPECT_EQ(2, double_load_backend_count);
}

// Tests SuccessRate and FailurePercentage both unconfigured;
// This is the case where according to the gRFC we need to instruct the picker
// not to do counting or even start the timer. The result of not counting is
// that there will be no ejection taking place since we can't do any
// calculations.
TEST_P(OutlierDetectionTest, SuccessRateAndFailurePercentageBothDisabled) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  auto* outlier_detection = cluster.mutable_outlier_detection();
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_interval());
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_base_ejection_time());
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contain a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  const auto rpc_options = RpcOptions().set_metadata(metadata);
  const auto rpc_options1 = RpcOptions().set_metadata(std::move(metadata1));
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options1);
  // Cause an error and wait for 1 outlier detection interval to pass
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::CANCELLED, "",
                      RpcOptions()
                          .set_metadata(std::move(metadata))
                          .set_server_expected_error(StatusCode::CANCELLED));
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(
      3000 * grpc_test_slowdown_factor()));
  ResetBackendCounters();
  // 1 backend experienced failure, but since there is no counting there is no
  // ejection.  Both backends are still getting the RPCs intended for them.
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options1);
  EXPECT_EQ(100, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(100, backends_[1]->backend_service()->request_count());
}

// Tests that we uneject any ejected addresses when the OD policy is
// disabled.
TEST_P(OutlierDetectionTest, DisableOutlierDetectionWhileAddressesAreEjected) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  // Setup outlier failure percentage parameters.
  // Any failure will cause an potential ejection with the probability of 100%
  // (to eliminate flakiness of the test).
  auto* outlier_detection = cluster.mutable_outlier_detection();
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_interval());
  SetProtoDuration(grpc_core::Duration::Seconds(3),
                   outlier_detection->mutable_base_ejection_time());
  outlier_detection->mutable_failure_percentage_threshold()->set_value(0);
  outlier_detection->mutable_enforcing_failure_percentage()->set_value(100);
  outlier_detection->mutable_failure_percentage_minimum_hosts()->set_value(1);
  outlier_detection->mutable_failure_percentage_request_volume()->set_value(1);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contain a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  const auto rpc_options = RpcOptions().set_metadata(metadata);
  const auto rpc_options1 = RpcOptions().set_metadata(std::move(metadata1));
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options1);
  // Cause an error and wait for traffic aimed at backend 0 to start going to
  // backend 1.
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::CANCELLED, "",
      RpcOptions().set_metadata(metadata).set_server_expected_error(
          StatusCode::CANCELLED));
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions().set_timeout_ms(
                     3000 * grpc_test_slowdown_factor()),
                 rpc_options);
  // 1 backend is ejected all traffic going to the ejected backend should now
  // all be going to the other backend.
  // failure percentage enforcement_percentage of 100% is honored as this test
  // will consistently reject 1 backend.
  CheckRpcSendOk(DEBUG_LOCATION, 1, rpc_options);
  EXPECT_EQ(1, backends_[1]->backend_service()->request_count());
  // Send an update that disables outlier detection.
  cluster.clear_outlier_detection();
  balancer_->ads_service()->SetCdsResource(cluster);
  // Wait for the backend to start being used again.
  WaitForBackend(
      DEBUG_LOCATION, 0,
      [](const RpcResult& result) {
        EXPECT_EQ(result.status.error_code(), StatusCode::CANCELLED)
            << "Error: " << result.status.error_message();
      },
      WaitForBackendOptions(),
      RpcOptions()
          .set_metadata(std::move(metadata))
          .set_server_expected_error(StatusCode::CANCELLED));
}

TEST_P(OutlierDetectionTest, EjectionRetainedAcrossPriorities) {
  CreateAndStartBackends(3);
  auto cluster = default_cluster_;
  // Setup outlier failure percentage parameters.
  // Any failure will cause an potential ejection with the probability of 100%
  // (to eliminate flakiness of the test).
  auto* outlier_detection = cluster.mutable_outlier_detection();
  SetProtoDuration(grpc_core::Duration::Seconds(1),
                   outlier_detection->mutable_interval());
  SetProtoDuration(grpc_core::Duration::Minutes(10),
                   outlier_detection->mutable_base_ejection_time());
  outlier_detection->mutable_failure_percentage_threshold()->set_value(0);
  outlier_detection->mutable_enforcing_failure_percentage()->set_value(100);
  outlier_detection->mutable_failure_percentage_minimum_hosts()->set_value(1);
  outlier_detection->mutable_failure_percentage_request_volume()->set_value(1);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Priority 0: backend 0 and a non-existent backend.
  // Priority 1: backend 1.
  EdsResourceArgs args({
      {"locality0", {CreateEndpoint(0), MakeNonExistentEndpoint()}},
      {"locality1", {CreateEndpoint(1)}, kDefaultLocalityWeight, 1},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(DEBUG_LOCATION, 0);
  // Trigger an error to backend 0.
  // The success rate enforcement_percentage is 100%, so this will cause
  // the backend to be ejected when the ejection timer fires.
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::CANCELLED, "",
      RpcOptions().set_server_expected_error(StatusCode::CANCELLED));
  // Wait for traffic aimed at backend 0 to start going to backend 1.
  // This tells us that backend 0 has been ejected.
  // It should take no more than one ejection timer interval.
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions().set_timeout_ms(
                     3000 * grpc_test_slowdown_factor()));
  // Now send an EDS update that moves backend 0 to priority 1.
  // We also add backend 2, so that we know when the client sees the update.
  args = EdsResourceArgs({
      {"locality0", {MakeNonExistentEndpoint()}},
      {"locality1", CreateEndpointsForBackends(), kDefaultLocalityWeight, 1},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(DEBUG_LOCATION, 2);
  // Now send 100 RPCs and make sure they all go to backends 1 and 2,
  // because backend 0 should still be ejected.
  CheckRpcSendOk(DEBUG_LOCATION, 100);
  EXPECT_EQ(0, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(50, backends_[1]->backend_service()->request_count());
  EXPECT_EQ(50, backends_[2]->backend_service()->request_count());
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  // Make the backup poller poll very frequently in order to pick up
  // updates from all the subchannels's FDs.
  grpc_core::ConfigVars::Overrides overrides;
  overrides.client_channel_backup_poll_interval_ms = 1;
  grpc_core::ConfigVars::SetOverrides(overrides);
#if TARGET_OS_IPHONE
  // Workaround Apple CFStream bug
  grpc_core::SetEnv("grpc_cfstream", "0");
#endif
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
