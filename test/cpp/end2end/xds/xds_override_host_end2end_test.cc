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

  std::multimap<std::string, std::string> SendRpcGetServerMetadata(
      const grpc_core::DebugLocation& debug_location,
      const RpcOptions& rpc_options) {
    std::multimap<std::string, std::string> server_initial_metadata;
    EchoResponse response;
    grpc::Status status =
        SendRpc(rpc_options, &response, &server_initial_metadata);
    EXPECT_TRUE(status.ok())
        << "code=" << status.error_code()
        << ", message=" << status.error_message() << "\n"
        << debug_location.file() << ":" << debug_location.line();
    EXPECT_EQ(response.message(), kRequestMessage)
        << debug_location.file() << ":" << debug_location.line();
    return server_initial_metadata;
  }

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

  static std::vector<std::string> GetStatefulSessionCookie(
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
      EXPECT_NE(cookie->attributes.find("HttpOnly"), cookie->attributes.end())
          << cookie->raw;
      values.emplace_back(cookie->value);
    }
    return values;
  }

  // Builds a Listener with Fault Injection filter config. If the http_fault
  // is nullptr, then assign an empty filter config. This filter config is
  // required to enable the fault injection features.
  static Listener BuildListenerWithStatefulSessionFilter() {
    CookieBasedSessionState cookie_state;
    cookie_state.mutable_cookie()->set_name(std::string(kCookieName));
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
};

INSTANTIATE_TEST_SUITE_P(XdsTest, OverrideHostTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

TEST_P(OverrideHostTest, HappyPath) {
  CreateAndStartBackends(2);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithStatefulSessionFilter(),
                                   default_route_config_);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends(DEBUG_LOCATION);
  // First call gets the cookie. RR policy picks the backend we will use.
  auto server_initial_metadata =
      SendRpcGetServerMetadata(DEBUG_LOCATION, RpcOptions());
  std::vector<std::string> stateful_session_cookie =
      GetStatefulSessionCookie(server_initial_metadata);
  ASSERT_EQ(stateful_session_cookie.size(), 1);
  size_t backend_idx = -1;
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (backends_[i]->backend_service()->request_count() == 1) {
      backend_idx = i;
      break;
    }
  }
  ASSERT_NE(-1, backend_idx);
  ResetBackendCounters();
  std::vector<std::pair<std::string, std::string>> session_cookie = {
      {"cookie",
       absl::StrFormat("%s=%s", kCookieName, stateful_session_cookie[0])}};
  // All requests go to the backend we specified
  CheckRpcSendOk(DEBUG_LOCATION, 5, RpcOptions().set_metadata(session_cookie));
  EXPECT_EQ(backends_[backend_idx]->backend_service()->request_count(), 5);
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
  EXPECT_EQ(backends_[backend_idx]->backend_service2()->request_count(), 5);
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
