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
using ::envoy::extensions::filters::network::http_connection_manager::v3::
    HttpFilter;
using ::envoy::extensions::http::stateful_session::cookie::v3 ::
    CookieBasedSessionState;

constexpr absl::string_view kCookieName = "grpc_session_cookie";

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
                                    RpcOptions rpc_options = RpcOptions()) {
    EXPECT_LT(backend_index, backends_.size());
    if (backend_index >= backends_.size()) {
      return {};
    }
    const auto& backend = backends_[backend_index];
    for (size_t i = 0; i < backends_.size(); ++i) {
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
