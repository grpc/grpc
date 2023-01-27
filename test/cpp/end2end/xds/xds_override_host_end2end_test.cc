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
using ::envoy::extensions::filters::http::stateful_session::v3::StatefulSession;
using ::envoy::extensions::filters::network::http_connection_manager::v3::
    HttpFilter;
using ::envoy::extensions::http::stateful_session::cookie::v3 ::
    CookieBasedSessionState;

class OverrideHostTest : public XdsEnd2endTest {
 protected:
  struct Cookie {
    std::string value;
    std::set<std::string> attributes;
    std::string raw;
  };

  static std::map<std::string, Cookie, std::less<>> ParseCookies(
      const std::map<std::string, std::string, std::less<>>& initial_metadata) {
    auto it = initial_metadata.find("set-cookie");
    if (it == initial_metadata.end()) {
      return {};
    }
    // Right now we only support a single cookie. This will be revised as
    // semantics are better understood
    std::pair<absl::string_view, absl::string_view> name_value =
        absl::StrSplit(it->second, absl::MaxSplits('=', 1));
    if (name_value.first.empty()) {
      return {};
    }
    std::pair<absl::string_view, absl::string_view> value_attrs =
        absl::StrSplit(name_value.second, absl::MaxSplits(';', 1));
    std::set<std::string> attributes;
    for (absl::string_view segment : absl::StrSplit(name_value.second, ';')) {
      attributes.insert(std::string(absl::StripAsciiWhitespace(segment)));
    }
    return {{std::string(name_value.first),
             Cookie({std::string(value_attrs.first), std::move(attributes),
                     std::string(name_value.second)})}};
  }

  static absl::optional<std::string> GetStatefulSessionCookie(
      const std::map<std::string, std::string, std::less<>>& initial_metadata,
      absl::string_view cookie_name = "grpc_session_cookie") {
    auto cookies = ParseCookies(initial_metadata);
    auto it = cookies.find(cookie_name);
    if (it == cookies.end()) {
      return absl::nullopt;
    }
    Cookie& grpc_session_cookie = it->second;
    EXPECT_FALSE(grpc_session_cookie.value.empty());
    EXPECT_NE(grpc_session_cookie.attributes.find("HttpOnly"),
              grpc_session_cookie.attributes.end())
        << grpc_session_cookie.raw;
    return grpc_session_cookie.value;
  }

  // Builds a Listener with Fault Injection filter config. If the http_fault
  // is nullptr, then assign an empty filter config. This filter config is
  // required to enable the fault injection features.
  static Listener BuildListenerWithStatefulSessionFilter() {
    CookieBasedSessionState cookie_state;
    cookie_state.mutable_cookie()->set_name("grpc_session_cookie");
    StatefulSession stateful_session;
    stateful_session.mutable_session_state()->mutable_typed_config()->PackFrom(
        cookie_state);
    HttpConnectionManager http_connection_manager;
    Listener listener;
    listener.set_name(kServerName);
    HttpFilter* session_filter = http_connection_manager.add_http_filters();
    session_filter->set_name("envoy.stateful_session");
    session_filter->mutable_typed_config()->PackFrom(stateful_session);
    HttpFilter* router_filter = http_connection_manager.add_http_filters();
    router_filter->set_name("router");
    router_filter->mutable_typed_config()->PackFrom(
        envoy::extensions::filters::http::router::v3::Router());
    listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
        http_connection_manager);
    return listener;
  }

  size_t GetAndResetRequestCount(size_t backend_idx, RpcService service) {
    size_t result = 1000;  // Will know there's a new service...
    switch (service) {
      case RpcService::SERVICE_ECHO:
        result = backends_[backend_idx]->backend_service()->request_count();
        backends_[backend_idx]->backend_service()->ResetCounters();
        break;
      case RpcService::SERVICE_ECHO1:
        result = backends_[backend_idx]->backend_service1()->request_count();
        backends_[backend_idx]->backend_service1()->ResetCounters();
        break;
      case RpcService::SERVICE_ECHO2:
        result = backends_[backend_idx]->backend_service2()->request_count();
        backends_[backend_idx]->backend_service2()->ResetCounters();
        break;
    }
    return result;
  }

  void CheckBackendCallCounts(
      absl::variant<std::function<size_t(size_t backend_idx)>, size_t>
          call_count,
      RpcService service = RpcService::SERVICE_ECHO,
      const grpc_core::DebugLocation& debug_location = DEBUG_LOCATION) {
    for (size_t i = 0; i < backends_.size(); i++) {
      EXPECT_EQ(grpc_core::Match(
                    call_count,
                    [i](std::function<size_t(size_t backend_idx)> fn) {
                      return fn(i);
                    },
                    [](size_t count) { return count; }),
                GetAndResetRequestCount(i, service))
          << "Backend " << i << "\n"
          << debug_location.file() << ":" << debug_location.line();
      // Make sure no other calls are there, mostly to prevent coding errors in
      // tests
      EXPECT_EQ(0, backends_[i]->backend_service()->request_count())
          << "Backend " << i << "\n"
          << debug_location.file() << ":" << debug_location.line();
      EXPECT_EQ(0, backends_[i]->backend_service1()->request_count())
          << "Backend " << i << "\n"
          << debug_location.file() << ":" << debug_location.line();
      EXPECT_EQ(0, backends_[i]->backend_service2()->request_count())
          << "Backend " << i << "\n"
          << debug_location.file() << ":" << debug_location.line();
    }
  }
};

INSTANTIATE_TEST_SUITE_P(XdsTest, OverrideHostTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);
TEST_P(OverrideHostTest, HappyPath) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::ROUND_ROBIN);
  balancer_->ads_service()->SetCdsResource(cluster);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithStatefulSessionFilter(),
                                   default_route_config_);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetRdsResource(RouteConfiguration());
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions());
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions());
  std::vector<std::map<std::string, std::string, std::less<>>> initial_metadata;
  // First call gets the cookie. RR policy picks the backend we will use.
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions(), &initial_metadata);
  absl::optional<std::string> stateful_session_cookie =
      GetStatefulSessionCookie(initial_metadata[0]);
  ASSERT_TRUE(stateful_session_cookie.has_value());
  size_t backend_idx = -1;
  for (size_t i = 0; i < backends_.size(); i++) {
    if (backends_[i]->backend_service()->request_count() == 1) {
      backends_[i]->backend_service()->ResetCounters();
      backend_idx = i;
      break;
    }
  }
  ASSERT_NE(-1, backend_idx);
  CheckBackendCallCounts(0);
  // All requests go to the backend we specified
  CheckRpcSendOk(DEBUG_LOCATION, 5,
                 RpcOptions().set_metadata(
                     {{"cookie", absl::StrFormat("%s=%s", "grpc_session_cookie",
                                                 *stateful_session_cookie)}}),
                 &initial_metadata);
  CheckBackendCallCounts(
      [=](size_t idx) { return idx == backend_idx ? 5 : 0; });
  // Round-robin spreads the load
  CheckRpcSendOk(DEBUG_LOCATION, backends_.size() * 2);
  CheckBackendCallCounts(2);
  // Call a different service with the same cookie
  CheckRpcSendOk(
      DEBUG_LOCATION, 5,
      RpcOptions()
          .set_metadata(
              {{"cookie", absl::StrFormat("%s=%s", "grpc_session_cookie",
                                          *stateful_session_cookie)}})
          .set_rpc_service(RpcService::SERVICE_ECHO2),
      &initial_metadata);
  CheckBackendCallCounts([=](size_t idx) { return idx == backend_idx ? 5 : 0; },
                         RpcService::SERVICE_ECHO2);
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
  GPR_GLOBAL_CONFIG_SET(grpc_client_channel_backup_poll_interval_ms, 1);
#if TARGET_OS_IPHONE
  // Workaround Apple CFStream bug
  grpc_core::SetEnv("grpc_cfstream", "0");
#endif
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
