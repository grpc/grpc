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

class XdsCompositeFilterEnd2endTest : public XdsEnd2endTest {
 public:
  XdsCompositeFilterEnd2endTest()
      : env_("GRPC_EXPERIMENTAL_XDS_COMPOSITE_FILTER") {}

  void SetUp() override {
    if (!grpc_core::IsXdsChannelFilterChainPerRouteEnabled()) {
      GTEST_SKIP()
          << "test requires xds_channel_filter_chain_per_route experiment";
    }
    grpc_core::SetXdsHttpFilterFactoryForTest([]() {
      return std::make_unique<grpc_core::XdsHttpAddHeaderFilterFactory>();
    });
    CreateAndStartBackends(1);
    EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
    balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
    InitClient();
  }

  void TearDown() override {
    grpc_core::SetXdsHttpFilterFactoryForTest(nullptr);
    XdsEnd2endTest::TearDown();
  }

  TypedExtensionConfig BuildAddHeaderFilterConfig(
      const std::string& header_name, const std::string& header_value) {
    TypedStruct typed_struct;
    typed_struct.set_type_url(
        absl::StrCat("type.googleapis.com/",
                     grpc_core::XdsHttpAddHeaderFilterFactory::kFilterName));
    auto* value_map = typed_struct.mutable_value()->mutable_fields();
    (*value_map)["header_name"].set_string_value(header_name);
    (*value_map)["header_value"].set_string_value(header_value);
    TypedExtensionConfig typed_extension_config;
    typed_extension_config.set_name("add_header_filter");
    typed_extension_config.mutable_typed_config()->PackFrom(typed_struct);
    return typed_extension_config;
  }

  Listener BuildListenerWithCompositeFilter(
      const std::string& input_header_name,
      std::map<std::string /*input_header_value*/,
               std::pair<std::string, std::string> /*header_to_add*/>
          matcher_data,
      bool optional = false) {
    Listener listener = default_listener_;
    HttpConnectionManager hcm = ClientHcmAccessor().Unpack(listener);
    HttpFilter* filter0 = hcm.mutable_http_filters(0);
    *hcm.add_http_filters() = *filter0;
    filter0->set_name("composite_filter");
    if (optional) filter0->set_is_optional(true);
    // Composite filter config is an ExtensionWithMatcher proto with the
    // extension_config field containing an empty Composite filter
    // message and the xds_matcher field containing the matcher tree.
    ExtensionWithMatcher extension_with_matcher;
    extension_with_matcher.mutable_extension_config()
        ->mutable_typed_config()
        ->PackFrom(Composite());
    auto* matcher_tree =
        extension_with_matcher.mutable_xds_matcher()->mutable_matcher_tree();
    // The input for the matcher tree is an HttpRequestHeaderMatchInput
    // with the specified input_header_name.
    HttpRequestHeaderMatchInput input;
    input.set_header_name(input_header_name);
    matcher_tree->mutable_input()->mutable_typed_config()->PackFrom(input);
    // The matcher tree itself is based on matcher_data.
    auto* matcher_map = matcher_tree->mutable_exact_match_map()->mutable_map();
    for (const auto& [input_header_value, header_to_add] : matcher_data) {
      const auto& [add_header_name, add_header_value] = header_to_add;
      // Each leaf in the tree is an ExecuteFilterAction whose
      // typed_config field contains the filter to delegate to, which
      // will be an AddHeaderFilter.
      ExecuteFilterAction action;
      *action.mutable_typed_config() =
          BuildAddHeaderFilterConfig(add_header_name, add_header_value);
      (*matcher_map)[input_header_value]
          .mutable_action()
          ->mutable_typed_config()
          ->PackFrom(action);
    }
    filter0->mutable_typed_config()->PackFrom(extension_with_matcher);
    ClientHcmAccessor().Pack(hcm, &listener);
    return listener;
  }

  grpc_core::testing::ScopedExperimentalEnvVar env_;
};

INSTANTIATE_TEST_SUITE_P(XdsTest, XdsCompositeFilterEnd2endTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

TEST_P(XdsCompositeFilterEnd2endTest, Basic) {
  // Configure the composite filter with a matcher tree, as follows:
  // - match on name=enterprise, add header status=legend
  // - match on name=yorktown, add header sunk=midway
  SetListenerAndRouteConfiguration(
      balancer_.get(),
      BuildListenerWithCompositeFilter("name",
                                       {{"enterprise", {"status", "legend"}},
                                        {"yorktown", {"sunk", "midway"}}}),
      default_route_config_);
  // Send RPC with name=enterprise.
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
  server_initial_metadata.clear();
  status = SendRpc(RpcOptions()
                       .set_metadata({{"name", "yorktown"}})
                       .set_echo_metadata_initially(true),
                   /*response=*/nullptr, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_THAT(server_initial_metadata,
              ::testing::Contains(::testing::Pair("sunk", "midway")));
  // Now send an RPC with no matching header.  Nothing should be added.
  server_initial_metadata.clear();
  status = SendRpc(RpcOptions().set_echo_metadata_initially(true),
                   /*response=*/nullptr, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_THAT(server_initial_metadata,
              ::testing::Not(::testing::Contains(
                  ::testing::Key(::testing::AnyOf("sunk", "status")))));
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
