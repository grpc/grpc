//
// Copyright 2025 gRPC authors.
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

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/common/matching/v3/extension_matcher.pb.h"
#include "envoy/extensions/filters/common/matcher/action/v3/skip_action.pb.h"
#include "envoy/extensions/filters/http/composite/v3/composite.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/type/matcher/v3/http_inputs.pb.h"
#include "src/core/client_channel/backup_poller.h"
#include "src/core/config/config_vars.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc_builder.h"
#include "src/core/xds/grpc/xds_http_filter.h"
#include "src/core/xds/grpc/xds_http_filter_registry.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/xds_http_add_header_filter.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"
#include "xds/type/matcher/v3/matcher.pb.h"
#include "xds/type/v3/typed_struct.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::config::core::v3::TypedExtensionConfig;
using ::envoy::extensions::common::matching::v3::ExtensionWithMatcher;
using ::envoy::extensions::common::matching::v3::ExtensionWithMatcherPerRoute;
using ::envoy::extensions::filters::common::matcher::action::v3::SkipFilter;
using ::envoy::extensions::filters::http::composite::v3::Composite;
using ::envoy::extensions::filters::http::composite::v3::ExecuteFilterAction;
using ::envoy::extensions::filters::network::http_connection_manager::v3::
    HttpFilter;
using ::envoy::type::matcher::v3::HttpRequestHeaderMatchInput;
using ::xds::type::matcher::v3::Matcher;
using ::xds::type::v3::TypedStruct;

// TODO(roth): Add some unit tests once we have a v3 filter/interceptor
// test framework.

class XdsCompositeFilterEnd2endTest : public XdsEnd2endTest {
 protected:
  void SetUp() override {
    if (GetParam().filter_on_server() &&
        !grpc_core::IsXdsServerFilterChainPerRouteEnabled()) {
      GTEST_SKIP()
          << "test requires xds_server_filter_chain_per_route experiment";
    }
    grpc_core::GrpcXdsBootstrapBuilder::SetXdsHttpFilterFactoryInitForTest(
        [](grpc_core::XdsHttpFilterRegistry& registry) {
          registry.RegisterFilter(
              std::make_unique<grpc_core::XdsHttpAddHeaderFilterFactory>());
          registry.RegisterFilter(
              std::make_unique<
                  grpc_core::XdsHttpServerAddHeaderFilterFactory>());
        });
    CreateBackends(1, /*xds_enabled=*/GetParam().filter_on_server());
    EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
    balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
    InitClient();
  }

  void TearDown() override {
    grpc_core::GrpcXdsBootstrapBuilder::SetXdsHttpFilterFactoryInitForTest(
        nullptr);
    XdsEnd2endTest::TearDown();
  }

  static TypedExtensionConfig BuildAddHeaderFilterConfig(
      const std::string& header_name, const std::string& header_value,
      bool server_filter) {
    TypedStruct typed_struct;
    typed_struct.set_type_url(absl::StrCat(
        "type.googleapis.com/",
        server_filter
            ? grpc_core::XdsHttpServerAddHeaderFilterFactory::kFilterName
            : grpc_core::XdsHttpAddHeaderFilterFactory::kFilterName));
    auto* value_map = typed_struct.mutable_value()->mutable_fields();
    (*value_map)["header_name"].set_string_value(header_name);
    (*value_map)["header_value"].set_string_value(header_value);
    TypedExtensionConfig typed_extension_config;
    typed_extension_config.set_name("add_header_filter");
    typed_extension_config.mutable_typed_config()->PackFrom(typed_struct);
    return typed_extension_config;
  }

  // Matcher action.  Either a header to add, or nullopt for SkipFilter.
  using ActionData =
      std::optional<std::tuple<std::string /*key*/, std::string /*value*/,
                               bool /*server_filter*/>>;

  static ActionData::value_type MakeAction(
      std::string key, std::string value,
      std::optional<bool> server_filter = std::nullopt) {
    return {std::move(key), std::move(value),
            server_filter.value_or(GetParam().filter_on_server())};
  }

  // Matcher data.  Maps input header value to action.
  using MatcherData = std::map<std::string, ActionData>;

  static void SetAction(ActionData action_data, Matcher::OnMatch* on_match) {
    auto* any = on_match->mutable_action()->mutable_typed_config();
    // If there's no action, then add SkipFilter.
    if (!action_data.has_value()) {
      any->PackFrom(SkipFilter());
      return;
    }
    // Otherwise, add an ExecuteFilterAction whose typed_config field
    // contains the filter to delegate to, which will be an AddHeaderFilter.
    const auto& [add_header_name, add_header_value, server_filter] =
        *action_data;
    ExecuteFilterAction action;
    *action.mutable_typed_config() = BuildAddHeaderFilterConfig(
        add_header_name, add_header_value, server_filter);
    any->PackFrom(action);
  }

  static Matcher BuildMatcher(
      const std::string& input_header_name, MatcherData matcher_data,
      std::optional<ActionData> on_no_match = std::nullopt) {
    Matcher matcher;
    auto* matcher_tree = matcher.mutable_matcher_tree();
    // The input for the matcher tree is an HttpRequestHeaderMatchInput
    // with the specified input_header_name.
    HttpRequestHeaderMatchInput input;
    input.set_header_name(input_header_name);
    matcher_tree->mutable_input()->mutable_typed_config()->PackFrom(input);
    // The matcher tree itself is based on matcher_data.
    auto* matcher_map = matcher_tree->mutable_exact_match_map()->mutable_map();
    for (const auto& [input_header_value, header_to_add] : matcher_data) {
      SetAction(header_to_add, &(*matcher_map)[input_header_value]);
    }
    if (on_no_match.has_value()) {
      SetAction(*on_no_match, matcher.mutable_on_no_match());
    }
    return matcher;
  }

  static constexpr char kFilterInstanceName[] = "composite_filter";

  Listener BuildListenerWithCompositeFilter(
      std::optional<Matcher> matcher) const {
    Listener listener;
    std::unique_ptr<HcmAccessor> hcm_accessor;
    if (GetParam().filter_on_server()) {
      listener = default_server_listener_;
      hcm_accessor = std::make_unique<ServerHcmAccessor>();
    } else {
      listener = default_listener_;
      hcm_accessor = std::make_unique<ClientHcmAccessor>();
    }
    HttpConnectionManager hcm = hcm_accessor->Unpack(listener);
    HttpFilter* filter0 = hcm.mutable_http_filters(0);
    *hcm.add_http_filters() = *filter0;
    filter0->set_name(kFilterInstanceName);
    // Composite filter config is an ExtensionWithMatcher proto with the
    // extension_config field containing an empty Composite filter
    // message and the xds_matcher field containing the matcher tree.
    ExtensionWithMatcher extension_with_matcher;
    extension_with_matcher.mutable_extension_config()
        ->mutable_typed_config()
        ->PackFrom(Composite());
    if (matcher.has_value()) {
      *extension_with_matcher.mutable_xds_matcher() = std::move(*matcher);
    }
    filter0->mutable_typed_config()->PackFrom(extension_with_matcher);
    hcm_accessor->Pack(hcm, &listener);
    return listener;
  }

  RouteConfiguration BuildRouteConfigWithOverrideConfig(Matcher matcher) const {
    ExtensionWithMatcherPerRoute override_config;
    *override_config.mutable_xds_matcher() = std::move(matcher);
    RouteConfiguration route_config = GetParam().filter_on_server()
                                          ? default_server_route_config_
                                          : default_route_config_;
    auto& typed_per_filter_config = *route_config.mutable_virtual_hosts(0)
                                         ->mutable_routes(0)
                                         ->mutable_typed_per_filter_config();
    typed_per_filter_config[kFilterInstanceName].PackFrom(override_config);
    return route_config;
  }

  void SetListenerAndRouteConfig(
      Listener listener,
      std::optional<RouteConfiguration> route_config = std::nullopt) {
    if (GetParam().filter_on_server()) {
      if (!route_config.has_value()) {
        route_config = default_server_route_config_;
      }
      SetServerListenerNameAndRouteConfiguration(
          balancer_.get(), listener, backends_[0]->port(), *route_config);
    } else {
      if (!route_config.has_value()) route_config = default_route_config_;
      SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                       *route_config);
    }
  }
};

INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsCompositeFilterEnd2endTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_filter_on_server()),
    &XdsTestType::Name);

TEST_P(XdsCompositeFilterEnd2endTest, TopLevelConfig) {
  grpc_core::testing::ScopedExperimentalEnvVar env(
      "GRPC_EXPERIMENTAL_XDS_COMPOSITE_FILTER");
  // Configure the composite filter.
  MatcherData matcher_data;
  matcher_data["enterprise"] = MakeAction("status", "legend");
  matcher_data["yorktown"] = MakeAction("sunk", "midway");
  matcher_data["hornet"] = std::nullopt;  // SkipFilter
  SetListenerAndRouteConfig(BuildListenerWithCompositeFilter(
      BuildMatcher("name", std::move(matcher_data))));
  StartBackend(0);
  // Send RPC with name=enterprise.
  LOG(INFO) << "Sending RPC with name=enterprise...";
  std::multimap<std::string, std::string> server_initial_metadata;
  Status status = SendRpc(RpcOptions()
                              .set_metadata({{"name", "enterprise"}})
                              .set_echo_metadata_initially(true),
                          /*response=*/nullptr, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_THAT(server_initial_metadata,
              ::testing::Contains(::testing::Pair("status", "legend")));
  // Send RPC with name=yorktown.
  LOG(INFO) << "Sending RPC with name=yorktown...";
  server_initial_metadata.clear();
  status = SendRpc(RpcOptions()
                       .set_metadata({{"name", "yorktown"}})
                       .set_echo_metadata_initially(true),
                   /*response=*/nullptr, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_THAT(server_initial_metadata,
              ::testing::Contains(::testing::Pair("sunk", "midway")));
  // Send RPC with name=hornet.
  LOG(INFO) << "Sending RPC with name=hornet...";
  server_initial_metadata.clear();
  status = SendRpc(RpcOptions()
                       .set_metadata({{"name", "hornet"}})
                       .set_echo_metadata_initially(true),
                   /*response=*/nullptr, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_THAT(server_initial_metadata,
              ::testing::Not(::testing::Contains(
                  ::testing::Key(::testing::AnyOf("sunk", "status")))));
  // Now send an RPC with no matching header.  This should fail.
  LOG(INFO) << "Sending RPC with no name header...";
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      "no match found in composite filter");
}

TEST_P(XdsCompositeFilterEnd2endTest, OnNoMatch) {
  grpc_core::testing::ScopedExperimentalEnvVar env(
      "GRPC_EXPERIMENTAL_XDS_COMPOSITE_FILTER");
  // Configure the composite filter.
  MatcherData matcher_data;
  matcher_data["enterprise"] = MakeAction("status", "legend");
  ActionData::value_type on_no_match = MakeAction("status", "unknown");
  SetListenerAndRouteConfig(BuildListenerWithCompositeFilter(
      BuildMatcher("name", std::move(matcher_data), std::move(on_no_match))));
  StartBackend(0);
  // Send RPC with name=enterprise.
  LOG(INFO) << "Sending RPC with name=enterprise...";
  std::multimap<std::string, std::string> server_initial_metadata;
  Status status = SendRpc(RpcOptions()
                              .set_metadata({{"name", "enterprise"}})
                              .set_echo_metadata_initially(true),
                          /*response=*/nullptr, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_THAT(server_initial_metadata,
              ::testing::Contains(::testing::Pair("status", "legend")));
  // Send RPC with no matching header.  Should hit the on_no_match.
  LOG(INFO) << "Sending RPC with no name header...";
  server_initial_metadata.clear();
  status = SendRpc(RpcOptions().set_echo_metadata_initially(true),
                   /*response=*/nullptr, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_THAT(server_initial_metadata,
              ::testing::Contains(::testing::Pair("status", "unknown")));
}

TEST_P(XdsCompositeFilterEnd2endTest, TopLevelConfigEmptyMatcher) {
  grpc_core::testing::ScopedExperimentalEnvVar env(
      "GRPC_EXPERIMENTAL_XDS_COMPOSITE_FILTER");
  SetListenerAndRouteConfig(BuildListenerWithCompositeFilter(std::nullopt));
  StartBackend(0);
  CheckRpcSendOk(DEBUG_LOCATION);
}

TEST_P(XdsCompositeFilterEnd2endTest, OverrideConfig) {
  grpc_core::testing::ScopedExperimentalEnvVar env(
      "GRPC_EXPERIMENTAL_XDS_COMPOSITE_FILTER");
  // Configure the composite filter.
  // The top-level filter has an empty matcher, but there is an override
  // config in the route.
  MatcherData matcher_data;
  matcher_data["enterprise"] = MakeAction("status", "legend");
  Listener listener = BuildListenerWithCompositeFilter(std::nullopt);
  RouteConfiguration route_config = BuildRouteConfigWithOverrideConfig(
      BuildMatcher("name", std::move(matcher_data)));
  SetListenerAndRouteConfig(listener, route_config);
  StartBackend(0);
  // Send RPC with name=enterprise.
  LOG(INFO) << "Sending RPC with name=enterprise...";
  std::multimap<std::string, std::string> server_initial_metadata;
  Status status = SendRpc(RpcOptions()
                              .set_metadata({{"name", "enterprise"}})
                              .set_echo_metadata_initially(true),
                          /*response=*/nullptr, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_THAT(server_initial_metadata,
              ::testing::Contains(::testing::Pair("status", "legend")));
}

TEST_P(XdsCompositeFilterEnd2endTest,
       ChildFilterNotSupportedOnClientOrServerSide) {
  grpc_core::testing::ScopedExperimentalEnvVar env(
      "GRPC_EXPERIMENTAL_XDS_COMPOSITE_FILTER");
  // Configure the composite filter.
  // The top-level filter has an empty matcher, but there is an override
  // config in the route.
  MatcherData matcher_data;
  matcher_data["enterprise"] = MakeAction(
      "status", "legend", /*server_filter=*/!GetParam().filter_on_server());
  Listener listener = BuildListenerWithCompositeFilter(std::nullopt);
  RouteConfiguration route_config = BuildRouteConfigWithOverrideConfig(
      BuildMatcher("name", std::move(matcher_data)));
  SetListenerAndRouteConfig(listener, route_config);
  StartBackend(0);
  // Send RPC with name=enterprise.
  LOG(INFO) << "Sending RPC with name=enterprise...";
  std::multimap<std::string, std::string> server_initial_metadata;
  Status status = SendRpc(RpcOptions()
                              .set_metadata({{"name", "enterprise"}})
                              .set_echo_metadata_initially(true));
  EXPECT_EQ(status.error_code(), StatusCode::UNAVAILABLE);
  EXPECT_EQ(status.error_message(),
            GetParam().filter_on_server()
                ? "io.grpc.test.AddHeaderFilter filter not supported on servers"
                : "io.grpc.test.ServerAddHeaderFilter filter "
                  "not supported on clients");
}

TEST_P(XdsCompositeFilterEnd2endTest, FilterUnsupportedWithoutEnvVar) {
  SetListenerAndRouteConfig(BuildListenerWithCompositeFilter(std::nullopt));
  StartBackend(0);
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      "empty address list \\(LDS resource server.example.com: "
      "invalid resource: errors validating ApiListener: "
      "\\[field:api_listener.api_listener.value\\["
      "envoy.extensions.filters.network.http_connection_manager.v3"
      ".HttpConnectionManager\\].http_filters\\[0\\].typed_config.value\\["
      "envoy.extensions.common.matching.v3.ExtensionWithMatcher\\] "
      "error:unsupported filter type\\].*");
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
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
