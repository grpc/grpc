// Copyright 2023 gRPC authors.
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

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/gprpp/match.h"
#include "src/proto/grpc/testing/xds/v3/aggregate_cluster.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/cluster.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/outlier_detection.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/router.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/stateful_session.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/stateful_session_cookie.pb.h"
#include "test/core/util/scoped_env_var.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"

namespace grpc {
namespace testing {
namespace {
using ::envoy::config::core::v3::HealthStatus;
using ::envoy::extensions::filters::http::stateful_session::v3::StatefulSession;
using ::envoy::extensions::filters::http::stateful_session::v3::
    StatefulSessionPerRoute;
using ::envoy::extensions::filters::network::http_connection_manager::v3::
    HttpFilter;
using ::envoy::extensions::http::stateful_session::cookie::v3 ::
    CookieBasedSessionState;

constexpr absl::string_view kCookieName = "grpc_session_cookie";
constexpr absl::string_view kFilterName = "envoy.stateful_session";

class OverrideHostTest : public XdsEnd2endTest {
 protected:
  struct Cookie {
    std::string value;
    std::set<std::string> attributes;
    std::string raw;
  };

  static absl::optional<Cookie> ParseCookie(absl::string_view header,
                                            absl::string_view cookie_name) {
    std::pair<absl::string_view, absl::string_view> name_value =
        absl::StrSplit(header, absl::MaxSplits('=', 1));
    if (name_value.first.empty() || name_value.first != cookie_name) {
      return absl::nullopt;
    }
    std::pair<absl::string_view, absl::string_view> value_attrs =
        absl::StrSplit(name_value.second, absl::MaxSplits(';', 1));
    std::set<std::string> attributes;
    for (absl::string_view segment : absl::StrSplit(name_value.second, ';')) {
      attributes.emplace(absl::StripAsciiWhitespace(segment));
    }
    return Cookie({std::string(value_attrs.first), std::move(attributes),
                   std::string(name_value.second)});
  }

  static std::vector<std::pair<std::string, std::string>>
  GetHeadersWithSessionCookie(
      const std::multimap<std::string, std::string>& server_initial_metadata,
      absl::string_view cookie_name = kCookieName) {
    std::vector<std::string> values;
    auto pair = server_initial_metadata.equal_range("set-cookie");
    for (auto it = pair.first; it != pair.second; ++it) {
      auto cookie = ParseCookie(it->second, cookie_name);
      if (!cookie.has_value()) {
        continue;
      }
      EXPECT_FALSE(cookie->value.empty());
      EXPECT_THAT(cookie->attributes, ::testing::Contains("HttpOnly"));
      values.emplace_back(cookie->value);
    }
    EXPECT_EQ(values.size(), 1);
    if (values.size() == 1) {
      return {{"cookie", absl::StrFormat("%s=%s", cookie_name, values[0])}};
    } else {
      return {};
    }
  }

  // Builds a Listener with Fault Injection filter config. If the http_fault
  // is nullptr, then assign an empty filter config. This filter config is
  // required to enable the fault injection features.
  Listener BuildListenerWithStatefulSessionFilter(
      absl::string_view cookie_name = kCookieName) {
    StatefulSession stateful_session;
    if (!cookie_name.empty()) {
      CookieBasedSessionState cookie_state;
      cookie_state.mutable_cookie()->set_name(std::string(cookie_name));
      stateful_session.mutable_session_state()
          ->mutable_typed_config()
          ->PackFrom(cookie_state);
    }
    // HttpConnectionManager http_connection_manager;
    Listener listener = default_listener_;
    HttpConnectionManager http_connection_manager =
        ClientHcmAccessor().Unpack(listener);
    // Insert new filter ahead of the existing router filter.
    HttpFilter* session_filter =
        http_connection_manager.mutable_http_filters(0);
    *http_connection_manager.add_http_filters() = *session_filter;
    session_filter->set_name(kFilterName);
    session_filter->mutable_typed_config()->PackFrom(stateful_session);
    ClientHcmAccessor().Pack(http_connection_manager, &listener);
    return listener;
  }

  // Send requests until a desired backend is hit and returns cookie name/value
  // pairs. Empty collection is returned if the backend was never hit.
  // For weighted clusters, more than one request per backend may be necessary
  // to obtain the cookie. max_requests_per_backend argument specifies
  // the number of requests per backend to send.
  std::vector<std::pair<std::string, std::string>>
  GetAffinityCookieHeaderForBackend(
      grpc_core::DebugLocation debug_location, size_t backend_index,
      size_t max_requests_per_backend = 1,
      absl::string_view cookie_name = kCookieName) {
    EXPECT_LT(backend_index, backends_.size());
    if (backend_index >= backends_.size()) {
      return {};
    }
    const auto& backend = backends_[backend_index];
    for (size_t i = 0; i < max_requests_per_backend * backends_.size(); ++i) {
      std::multimap<std::string, std::string> server_initial_metadata;
      grpc::Status status =
          SendRpc(RpcOptions(), nullptr, &server_initial_metadata);
      EXPECT_TRUE(status.ok())
          << "code=" << status.error_code()
          << ", message=" << status.error_message() << "\n"
          << debug_location.file() << ":" << debug_location.line();
      if (!status.ok()) {
        return {};
      }
      size_t count = backend->backend_service()->request_count() +
                     backend->backend_service1()->request_count() +
                     backend->backend_service2()->request_count();
      ResetBackendCounters();
      if (count == 1) {
        return GetHeadersWithSessionCookie(server_initial_metadata,
                                           cookie_name);
      }
    }
    ADD_FAILURE_AT(debug_location.file(), debug_location.line())
        << "Desired backend had not been hit";
    return {};
  }

  void SetClusterResource(absl::string_view cluster_name,
                          absl::string_view eds_resource_name) {
    Cluster cluster = default_cluster_;
    cluster.set_name(cluster_name);
    cluster.mutable_eds_cluster_config()->set_service_name(eds_resource_name);
    balancer_->ads_service()->SetCdsResource(cluster);
  }

  RouteConfiguration BuildRouteConfigurationWithWeightedClusters(
      const std::map<absl::string_view, uint32_t> clusters) {
    RouteConfiguration new_route_config = default_route_config_;
    auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
    for (const auto& cluster : clusters) {
      auto* weighted_cluster =
          route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
      weighted_cluster->set_name(cluster.first);
      weighted_cluster->mutable_weight()->set_value(cluster.second);
    }
    return new_route_config;
  }

  void SetCdsAndEdsResources(absl::string_view cluster_name,
                             absl::string_view eds_service_name,
                             size_t start_index, size_t end_index) {
    balancer_->ads_service()->SetEdsResource(BuildEdsResource(
        EdsResourceArgs({{"locality0",
                          CreateEndpointsForBackends(start_index, end_index)}}),
        eds_service_name));
    SetClusterResource(cluster_name, eds_service_name);
  }

  static double BackendRequestPercentage(
      const std::unique_ptr<BackendServerThread>& backend,
      size_t num_requests) {
    return static_cast<double>(backend->backend_service()->request_count()) /
           num_requests;
  }
};

INSTANTIATE_TEST_SUITE_P(XdsTest, OverrideHostTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

TEST_P(OverrideHostTest, HappyPath) {
  CreateAndStartBackends(2);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithStatefulSessionFilter(),
                                   default_route_config_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(
      EdsResourceArgs({{"locality0",
                        {CreateEndpoint(0, HealthStatus::HEALTHY),
                         CreateEndpoint(1, HealthStatus::UNKNOWN)}}})));
  WaitForAllBackends(DEBUG_LOCATION);
  // Get cookie for backend #0.
  auto session_cookie = GetAffinityCookieHeaderForBackend(DEBUG_LOCATION, 0);
  ASSERT_FALSE(session_cookie.empty());
  // All requests go to the backend we specified
  CheckRpcSendOk(DEBUG_LOCATION, 5, RpcOptions().set_metadata(session_cookie));
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 5);
  // Round-robin spreads the load
  ResetBackendCounters();
  CheckRpcSendOk(DEBUG_LOCATION, backends_.size() * 2);
  EXPECT_EQ(2, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(2, backends_[1]->backend_service()->request_count());
  // Call a different service with the same cookie
  ResetBackendCounters();
  CheckRpcSendOk(DEBUG_LOCATION, 5,
                 RpcOptions()
                     .set_metadata(session_cookie)
                     .set_rpc_service(RpcService::SERVICE_ECHO2));
  EXPECT_EQ(backends_[0]->backend_service2()->request_count(), 5);
}

TEST_P(OverrideHostTest, DrainingIncludedFromOverrideSet) {
  CreateAndStartBackends(3);
  Cluster cluster = default_cluster_;
  auto* lb_config = cluster.mutable_common_lb_config();
  auto* override_health_status_set = lb_config->mutable_override_host_status();
  override_health_status_set->add_statuses(HealthStatus::HEALTHY);
  override_health_status_set->add_statuses(HealthStatus::UNKNOWN);
  override_health_status_set->add_statuses(HealthStatus::DRAINING);
  balancer_->ads_service()->SetCdsResource(cluster);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithStatefulSessionFilter(),
                                   default_route_config_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(
      EdsResourceArgs({{"locality0",
                        {CreateEndpoint(0, HealthStatus::HEALTHY),
                         CreateEndpoint(1, HealthStatus::HEALTHY)}}})));
  WaitForAllBackends(DEBUG_LOCATION, 0, 2);
  CheckRpcSendOk(DEBUG_LOCATION, 4);
  EXPECT_EQ(2, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(2, backends_[1]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  ResetBackendCounters();
  // Get cookie for backend #0.
  auto session_cookie = GetAffinityCookieHeaderForBackend(DEBUG_LOCATION, 0);
  ASSERT_FALSE(session_cookie.empty());
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(
      EdsResourceArgs({{"locality0",
                        {CreateEndpoint(0, HealthStatus::DRAINING),
                         CreateEndpoint(1, HealthStatus::HEALTHY),
                         CreateEndpoint(2, HealthStatus::HEALTHY)}}})));
  WaitForAllBackends(DEBUG_LOCATION, 2);
  // Draining subchannel works when used as an override host.
  CheckRpcSendOk(DEBUG_LOCATION, 4, RpcOptions().set_metadata(session_cookie));
  EXPECT_EQ(4, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  ResetBackendCounters();
  // Round robin does not see the draining backend
  CheckRpcSendOk(DEBUG_LOCATION, 4);
  EXPECT_EQ(0, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(2, backends_[1]->backend_service()->request_count());
  EXPECT_EQ(2, backends_[2]->backend_service()->request_count());
  ResetBackendCounters();
}

TEST_P(OverrideHostTest, DrainingExcludedFromOverrideSet) {
  CreateAndStartBackends(3);
  Cluster cluster = default_cluster_;
  auto* lb_config = cluster.mutable_common_lb_config();
  auto* override_health_status_set = lb_config->mutable_override_host_status();
  override_health_status_set->add_statuses(HealthStatus::HEALTHY);
  override_health_status_set->add_statuses(HealthStatus::UNKNOWN);
  balancer_->ads_service()->SetCdsResource(cluster);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithStatefulSessionFilter(),
                                   default_route_config_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(
      EdsResourceArgs({{"locality0",
                        {CreateEndpoint(0, HealthStatus::HEALTHY),
                         CreateEndpoint(1, HealthStatus::HEALTHY)}}})));
  WaitForAllBackends(DEBUG_LOCATION, 0, 2);
  CheckRpcSendOk(DEBUG_LOCATION, 4);
  EXPECT_EQ(2, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(2, backends_[1]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  ResetBackendCounters();
  // Get a cookie for backends_[0].
  auto session_cookie = GetAffinityCookieHeaderForBackend(DEBUG_LOCATION, 0);
  ASSERT_FALSE(session_cookie.empty());
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(
      EdsResourceArgs({{"locality0",
                        {CreateEndpoint(0, HealthStatus::DRAINING),
                         CreateEndpoint(1, HealthStatus::HEALTHY),
                         CreateEndpoint(2, HealthStatus::UNKNOWN)}}})));
  WaitForAllBackends(DEBUG_LOCATION, 2);
  // Override for the draining host is not honored, RR is used instead.
  CheckRpcSendOk(DEBUG_LOCATION, 4, RpcOptions().set_metadata(session_cookie));
  EXPECT_EQ(0, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(2, backends_[1]->backend_service()->request_count());
  EXPECT_EQ(2, backends_[2]->backend_service()->request_count());
  ResetBackendCounters();
}

TEST_P(OverrideHostTest, OverrideWithWeightedClusters) {
  CreateAndStartBackends(3);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const uint32_t kWeight1 = 1;
  const uint32_t kWeight2 = 3;
  const double kErrorTolerance = 0.025;
  const size_t kNumEchoRpcs = ComputeIdealNumRpcs(
      static_cast<double>(kWeight1) / (kWeight1 + kWeight2), kErrorTolerance);
  // Populate EDS and CDS resources.
  SetCdsAndEdsResources(kNewCluster1Name, kNewEdsService1Name, 0, 1);
  SetCdsAndEdsResources(kNewCluster2Name, kNewEdsService2Name, 1, 3);
  // Populating Route Configurations for LDS.
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithStatefulSessionFilter(),
      BuildRouteConfigurationWithWeightedClusters(
          {{kNewCluster1Name, kWeight1}, {kNewCluster2Name, kWeight2}}));
  WaitForAllBackends(DEBUG_LOCATION, 0, 3);
  // Get cookie
  auto session_cookie =
      GetAffinityCookieHeaderForBackend(DEBUG_LOCATION, 1, kNumEchoRpcs / 3);
  ASSERT_FALSE(session_cookie.empty());
  // All requests go to the backend we requested.
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs,
                 RpcOptions().set_metadata(session_cookie));
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 0);
  EXPECT_EQ(backends_[1]->backend_service()->request_count(), kNumEchoRpcs);
  EXPECT_EQ(backends_[2]->backend_service()->request_count(), 0);
}

TEST_P(OverrideHostTest, ClusterOverrideHonoredButHostGone) {
  CreateAndStartBackends(4);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const uint32_t kWeight1 = 1;
  const uint32_t kWeight2 = 3;
  const double kErrorTolerance = 0.025;
  const double kWeight2Percent =
      static_cast<double>(kWeight2) / (kWeight1 + kWeight2);
  const size_t kNumEchoRpcs =
      ComputeIdealNumRpcs(kWeight2Percent, kErrorTolerance);
  // Populate EDS and CDS resources.
  SetCdsAndEdsResources(kNewCluster1Name, kNewEdsService1Name, 0, 1);
  SetCdsAndEdsResources(kNewCluster2Name, kNewEdsService2Name, 1, 3);
  // Populating Route Configurations for LDS.
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithStatefulSessionFilter(),
      BuildRouteConfigurationWithWeightedClusters(
          {{kNewCluster1Name, kWeight1}, {kNewCluster2Name, kWeight2}}));
  WaitForAllBackends(DEBUG_LOCATION, 0, 3);
  auto session_cookie =
      GetAffinityCookieHeaderForBackend(DEBUG_LOCATION, 1, kNumEchoRpcs / 4);
  ASSERT_FALSE(session_cookie.empty());
  // Remove backends[1] from cluster2
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(
      EdsResourceArgs({{"locality0", CreateEndpointsForBackends(2, 4)}}),
      kNewEdsService2Name));
  WaitForAllBackends(DEBUG_LOCATION, 3, 4);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs,
                 RpcOptions().set_metadata(session_cookie));
  // Traffic goes to a second cluster, where it is equally distributed between
  // the two remaining hosts
  EXPECT_THAT(BackendRequestPercentage(backends_[2], kNumEchoRpcs),
              ::testing::DoubleNear(.5, kErrorTolerance));
  EXPECT_THAT(BackendRequestPercentage(backends_[3], kNumEchoRpcs),
              ::testing::DoubleNear(.5, kErrorTolerance));
  EXPECT_NE(session_cookie, GetAffinityCookieHeaderForBackend(
                                DEBUG_LOCATION, 2, kNumEchoRpcs / 3));
}

TEST_P(OverrideHostTest, ClusterGoneHostStays) {
  CreateAndStartBackends(3);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const char* kNewCluster3Name = "new_cluster_3";
  const char* kNewEdsService3Name = "new_eds_service_name_3";
  const uint32_t kWeight1 = 1;
  const uint32_t kWeight2 = 3;
  const double kErrorTolerance = 0.025;
  const double kPercentage1 =
      static_cast<double>(kWeight1) / (kWeight1 + kWeight2);
  const size_t kNumEchoRpcs =
      ComputeIdealNumRpcs(kPercentage1, kErrorTolerance);
  // Populate EDS and CDS resources.
  SetCdsAndEdsResources(kNewCluster1Name, kNewEdsService1Name, 0, 1);
  SetCdsAndEdsResources(kNewCluster2Name, kNewEdsService2Name, 1, 2);
  // Populating Route Configurations for LDS.
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithStatefulSessionFilter(),
      BuildRouteConfigurationWithWeightedClusters(
          {{kNewCluster1Name, kWeight1}, {kNewCluster2Name, kWeight2}}));
  WaitForAllBackends(DEBUG_LOCATION, 0, 2);
  auto backend1_in_cluster2_cookie =
      GetAffinityCookieHeaderForBackend(DEBUG_LOCATION, 1, kNumEchoRpcs / 3);
  ASSERT_FALSE(backend1_in_cluster2_cookie.empty());
  // Create a new cluster, cluster 3, containing a new backend, backend 2.
  SetCdsAndEdsResources(kNewCluster3Name, kNewEdsService3Name, 2, 3);
  // Send an EDS update for cluster 1 that adds backend 1. (Now cluster 1 has
  // backends 0 and 1.)
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(
      EdsResourceArgs({{"locality0", CreateEndpointsForBackends(0, 2)}}),
      kNewEdsService1Name));
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithStatefulSessionFilter(),
      BuildRouteConfigurationWithWeightedClusters(
          {{kNewCluster1Name, kWeight1}, {kNewCluster3Name, kWeight2}}));
  WaitForAllBackends(DEBUG_LOCATION, 2);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs,
                 RpcOptions().set_metadata(backend1_in_cluster2_cookie));
  // Traffic is split between clusters. Cluster1 traffic is sent to backends_[1]
  EXPECT_THAT(BackendRequestPercentage(backends_[0], kNumEchoRpcs),
              ::testing::DoubleNear(0, kErrorTolerance));
  EXPECT_THAT(BackendRequestPercentage(backends_[1], kNumEchoRpcs),
              ::testing::DoubleNear(kPercentage1, kErrorTolerance));
  EXPECT_THAT(BackendRequestPercentage(backends_[2], kNumEchoRpcs),
              ::testing::DoubleNear(1 - kPercentage1, kErrorTolerance));
  // backends_[1] cookie is updated with a new cluster
  EXPECT_NE(
      backend1_in_cluster2_cookie,
      GetAffinityCookieHeaderForBackend(DEBUG_LOCATION, 1, kNumEchoRpcs / 3));
}

TEST_P(OverrideHostTest, EnablePerRoute) {
  const absl::string_view kCustomCookieName = "GSSA";
  CreateAndStartBackends(2);
  RouteConfiguration route_config = default_route_config_;
  StatefulSessionPerRoute stateful_session_per_route;
  auto* session_state = stateful_session_per_route.mutable_stateful_session()
                            ->mutable_session_state();
  session_state->set_name("envoy.http.stateful_session.cookie");
  CookieBasedSessionState cookie_config;
  cookie_config.mutable_cookie()->set_name(kCustomCookieName);
  session_state->mutable_typed_config()->PackFrom(cookie_config);
  auto* route = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  google::protobuf::Any any;
  any.PackFrom(stateful_session_per_route);
  route->mutable_typed_per_filter_config()->emplace(kFilterName, any);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithStatefulSessionFilter(""),
                                   route_config);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs(
      {{"locality0", {CreateEndpoint(0), CreateEndpoint(1)}}})));
  WaitForAllBackends(DEBUG_LOCATION);
  // Get cookie for backend #0.
  auto session_cookie = GetAffinityCookieHeaderForBackend(DEBUG_LOCATION, 0, 1,
                                                          kCustomCookieName);
  ASSERT_FALSE(session_cookie.empty());
  // All requests go to the backend we specified
  CheckRpcSendOk(DEBUG_LOCATION, 5, RpcOptions().set_metadata(session_cookie));
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 5);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_core::testing::ScopedExperimentalEnvVar env_var(
      "GRPC_EXPERIMENTAL_XDS_ENABLE_OVERRIDE_HOST");
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
