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
using ::envoy::extensions::filters::http::stateful_session::v3::StatefulSession;
using ::envoy::extensions::filters::network::http_connection_manager::v3::
    HttpFilter;
using ::envoy::extensions::http::stateful_session::cookie::v3 ::
    CookieBasedSessionState;

constexpr absl::string_view kCookieName = "grpc_session_cookie";

class ClusterOverrideHostTest : public XdsEnd2endTest {
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
    return Cookie({
        std::string(value_attrs.first),
        std::move(attributes),
        std::string(name_value.second),
    });
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
      return {{"cookie", absl::StrFormat("%s=%s", kCookieName, values[0])}};
    } else {
      return {};
    }
  }

  // Builds a Listener with Fault Injection filter config. If the http_fault
  // is nullptr, then assign an empty filter config. This filter config is
  // required to enable the fault injection features.
  Listener BuildListenerWithStatefulSessionFilter() {
    CookieBasedSessionState cookie_state;
    cookie_state.mutable_cookie()->set_name(std::string(kCookieName));
    StatefulSession stateful_session;
    stateful_session.mutable_session_state()->mutable_typed_config()->PackFrom(
        cookie_state);
    // HttpConnectionManager http_connection_manager;
    Listener listener = default_listener_;
    HttpConnectionManager http_connection_manager =
        ClientHcmAccessor().Unpack(listener);
    // Insert new filter ahead of the existing router filter.
    HttpFilter* session_filter =
        http_connection_manager.mutable_http_filters(0);
    *http_connection_manager.add_http_filters() = *session_filter;
    session_filter->set_name("envoy.stateful_session");
    session_filter->mutable_typed_config()->PackFrom(stateful_session);
    ClientHcmAccessor().Pack(http_connection_manager, &listener);
    return listener;
  }

  std::vector<std::pair<std::string, std::string>>
  GetAffinityCookieHeaderForBackend(grpc_core::DebugLocation debug_location,
                                    size_t backend_index,
                                    size_t max_requests = 0,
                                    RpcOptions rpc_options = RpcOptions()) {
    EXPECT_LT(backend_index, backends_.size());
    if (backend_index >= backends_.size()) {
      return {};
    }
    const auto& backend = backends_[backend_index];
    if (max_requests == 0) {
      max_requests = backends_.size();
    }
    for (size_t i = 0; i < max_requests; ++i) {
      std::multimap<std::string, std::string> server_initial_metadata;
      grpc::Status status =
          SendRpc(rpc_options, nullptr, &server_initial_metadata);
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
        return GetHeadersWithSessionCookie(server_initial_metadata);
      }
    }
    ADD_FAILURE_AT(debug_location.file(), debug_location.line())
        << "Desired backend had not been hit";
    return {};
  }

  void SetClusterResource(absl::string_view cluster_name,
                          absl::string_view service_name) {
    Cluster cluster = default_cluster_;
    cluster.set_name(cluster_name);
    cluster.mutable_eds_cluster_config()->set_service_name(service_name);
    balancer_->ads_service()->SetCdsResource(cluster);
  }
};

INSTANTIATE_TEST_SUITE_P(XdsTest, ClusterOverrideHostTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

TEST_P(ClusterOverrideHostTest, XdsRoutingWeightedClusterNoIntegerOverflow) {
  CreateAndStartBackends(4);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const uint32_t kWeight1 = std::numeric_limits<uint32_t>::max() / 4;
  const uint32_t kWeight2 = std::numeric_limits<uint32_t>::max() - kWeight1;
  const double kErrorTolerance = 0.025;
  const double kWeight1Percent =
      static_cast<double>(kWeight1) / std::numeric_limits<uint32_t>::max();
  const double kWeight2Percent =
      static_cast<double>(kWeight2) / std::numeric_limits<uint32_t>::max();
  const size_t kNumEchoRpcs =
      ComputeIdealNumRpcs(kWeight2Percent, kErrorTolerance);
  // Populate new EDS resources.
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(
      EdsResourceArgs({{"locality0", CreateEndpointsForBackends(0, 1)}}),
      kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(
      EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 4)}}),
      kNewEdsService2Name));
  // Populate new CDS resources.
  SetClusterResource(kNewCluster1Name, kNewEdsService1Name);
  SetClusterResource(kNewCluster2Name, kNewEdsService2Name);
  // Populating Route Configurations for LDS.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("");
  auto* weighted_cluster1 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster1->set_name(kNewCluster1Name);
  weighted_cluster1->mutable_weight()->set_value(kWeight1);
  auto* weighted_cluster2 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster2->set_name(kNewCluster2Name);
  weighted_cluster2->mutable_weight()->set_value(kWeight2);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithStatefulSessionFilter(),
                                   new_route_config);
  WaitForAllBackends(DEBUG_LOCATION, 0, 2);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs);
  EXPECT_THAT(
      static_cast<double>(backends_[0]->backend_service()->request_count()) /
          kNumEchoRpcs,
      ::testing::DoubleNear(kWeight1Percent, kErrorTolerance));
  EXPECT_THAT(
      static_cast<double>(backends_[1]->backend_service()->request_count()) /
          kNumEchoRpcs,
      ::testing::DoubleNear(kWeight2Percent / 3, kErrorTolerance));
  EXPECT_THAT(
      static_cast<double>(backends_[2]->backend_service()->request_count()) /
          kNumEchoRpcs,
      ::testing::DoubleNear(kWeight2Percent / 3, kErrorTolerance));
  EXPECT_THAT(
      static_cast<double>(backends_[3]->backend_service()->request_count()) /
          kNumEchoRpcs,
      ::testing::DoubleNear(kWeight2Percent / 3, kErrorTolerance));
  auto session_cookie =
      GetAffinityCookieHeaderForBackend(DEBUG_LOCATION, 2, 10);
  ASSERT_FALSE(session_cookie.empty());
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs,
                 RpcOptions().set_metadata(session_cookie));
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 0);
  EXPECT_EQ(backends_[1]->backend_service()->request_count(), 0);
  EXPECT_EQ(backends_[2]->backend_service()->request_count(), kNumEchoRpcs);
  EXPECT_EQ(backends_[3]->backend_service()->request_count(), 0);
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
