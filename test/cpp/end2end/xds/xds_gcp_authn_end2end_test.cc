//
// Copyright 2024 gRPC authors.
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

#include <grpc/support/string_util.h>

#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/client_channel/backup_poller.h"
#include "src/core/config/config_vars.h"
#include "src/core/util/http_client/httpcli.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::extensions::filters::http::gcp_authn::v3::Audience;
using ::envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig;
using ::envoy::extensions::filters::network::http_connection_manager::v3::
    HttpFilter;

constexpr absl::string_view kFilterInstanceName = "gcp_authn_instance";
constexpr absl::string_view kAudience = "audience";

class XdsGcpAuthnEnd2endTest : public XdsEnd2endTest {
 public:
  void SetUp() override {
    g_audience = "";
    g_token = nullptr;
    g_num_token_fetches = 0;
    grpc_core::HttpRequest::SetOverride(HttpGetOverride, nullptr, nullptr);
    InitClient(MakeBootstrapBuilder(), /*lb_expected_authority=*/"",
               /*xds_resource_does_not_exist_timeout_ms=*/0,
               /*balancer_authority_override=*/"", /*args=*/nullptr,
               CreateTlsChannelCredentials());
  }

  void TearDown() override {
    XdsEnd2endTest::TearDown();
    grpc_core::HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  }

  static void ValidateHttpRequest(const grpc_http_request* request,
                                  const grpc_core::URI& uri) {
    EXPECT_THAT(
        uri.query_parameter_map(),
        ::testing::ElementsAre(::testing::Pair("audience", g_audience)));
    ASSERT_EQ(request->hdr_count, 1);
    EXPECT_EQ(absl::string_view(request->hdrs[0].key), "Metadata-Flavor");
    EXPECT_EQ(absl::string_view(request->hdrs[0].value), "Google");
  }

  static int HttpGetOverride(const grpc_http_request* request,
                             const grpc_core::URI& uri,
                             grpc_core::Timestamp /*deadline*/,
                             grpc_closure* on_done,
                             grpc_http_response* response) {
    // Intercept only requests for GCP service account identity tokens.
    if (uri.authority() != "metadata.google.internal." ||
        uri.path() !=
            "/computeMetadata/v1/instance/service-accounts/default/identity") {
      return 0;
    }
    g_num_token_fetches.fetch_add(1);
    // Validate request.
    ValidateHttpRequest(request, uri);
    // Generate response.
    response->status = 200;
    response->body = gpr_strdup(const_cast<char*>(g_token));
    response->body_length = strlen(g_token);
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
    return 1;
  }

  // Constructs a synthetic JWT token that's just valid enough for the
  // call creds to extract the expiration date.
  static std::string MakeToken(grpc_core::Timestamp expiration) {
    gpr_timespec ts = expiration.as_timespec(GPR_CLOCK_REALTIME);
    std::string json = absl::StrCat("{\"exp\":", ts.tv_sec, "}");
    return absl::StrCat("foo.", absl::WebSafeBase64Escape(json), ".bar");
  }

  Listener BuildListenerWithGcpAuthnFilter(bool optional = false) {
    Listener listener = default_listener_;
    HttpConnectionManager hcm = ClientHcmAccessor().Unpack(listener);
    HttpFilter* filter0 = hcm.mutable_http_filters(0);
    *hcm.add_http_filters() = *filter0;
    filter0->set_name(kFilterInstanceName);
    if (optional) filter0->set_is_optional(true);
    filter0->mutable_typed_config()->PackFrom(GcpAuthnFilterConfig());
    ClientHcmAccessor().Pack(hcm, &listener);
    return listener;
  }

  Cluster BuildClusterWithAudience(absl::string_view audience) {
    Audience audience_proto;
    audience_proto.set_url(audience);
    Cluster cluster = default_cluster_;
    auto& filter_map =
        *cluster.mutable_metadata()->mutable_typed_filter_metadata();
    auto& entry = filter_map[kFilterInstanceName];
    entry.PackFrom(audience_proto);
    return cluster;
  }

  static absl::string_view g_audience;
  static const char* g_token;
  static std::atomic<size_t> g_num_token_fetches;
};

absl::string_view XdsGcpAuthnEnd2endTest::g_audience;
const char* XdsGcpAuthnEnd2endTest::g_token;
std::atomic<size_t> XdsGcpAuthnEnd2endTest::g_num_token_fetches;

INSTANTIATE_TEST_SUITE_P(XdsTest, XdsGcpAuthnEnd2endTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

TEST_P(XdsGcpAuthnEnd2endTest, Basic) {
  grpc_core::testing::ScopedExperimentalEnvVar env(
      "GRPC_EXPERIMENTAL_XDS_GCP_AUTHENTICATION_FILTER");
  // Construct auth token.
  g_audience = kAudience;
  std::string token = MakeToken(grpc_core::Timestamp::InfFuture());
  g_token = token.c_str();
  // Set xDS resources.
  CreateAndStartBackends(1, /*xds_enabled=*/false,
                         CreateTlsServerCredentials());
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithGcpAuthnFilter(),
                                   default_route_config_);
  balancer_->ads_service()->SetCdsResource(BuildClusterWithAudience(kAudience));
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Send an RPC and check that it arrives with the right auth token.
  std::multimap<std::string, std::string> server_initial_metadata;
  Status status = SendRpc(RpcOptions().set_echo_metadata_initially(true),
                          /*response=*/nullptr, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_THAT(server_initial_metadata,
              ::testing::Contains(::testing::Pair(
                  "authorization", absl::StrCat("Bearer ", g_token))));
  EXPECT_EQ(g_num_token_fetches.load(), 1);
}

TEST_P(XdsGcpAuthnEnd2endTest, NoOpWhenClusterHasNoAudience) {
  grpc_core::testing::ScopedExperimentalEnvVar env(
      "GRPC_EXPERIMENTAL_XDS_GCP_AUTHENTICATION_FILTER");
  // Set xDS resources.
  CreateAndStartBackends(1, /*xds_enabled=*/false,
                         CreateTlsServerCredentials());
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithGcpAuthnFilter(),
                                   default_route_config_);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Send an RPC and check that it does not have an auth token.
  std::multimap<std::string, std::string> server_initial_metadata;
  Status status = SendRpc(RpcOptions().set_echo_metadata_initially(true),
                          /*response=*/nullptr, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_THAT(
      server_initial_metadata,
      ::testing::Not(::testing::Contains(::testing::Key("authorization"))));
}

TEST_P(XdsGcpAuthnEnd2endTest, CacheRetainedAcrossXdsUpdates) {
  grpc_core::testing::ScopedExperimentalEnvVar env(
      "GRPC_EXPERIMENTAL_XDS_GCP_AUTHENTICATION_FILTER");
  // Construct auth token.
  g_audience = kAudience;
  std::string token = MakeToken(grpc_core::Timestamp::InfFuture());
  g_token = token.c_str();
  // Set xDS resources.
  CreateAndStartBackends(1, /*xds_enabled=*/false,
                         CreateTlsServerCredentials());
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithGcpAuthnFilter(),
                                   default_route_config_);
  balancer_->ads_service()->SetCdsResource(BuildClusterWithAudience(kAudience));
  EdsResourceArgs args({{"locality0", {CreateEndpoint(0)}}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Send an RPC and check that it arrives with the right auth token.
  std::multimap<std::string, std::string> server_initial_metadata;
  Status status = SendRpc(RpcOptions().set_echo_metadata_initially(true),
                          /*response=*/nullptr, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_THAT(server_initial_metadata,
              ::testing::Contains(::testing::Pair(
                  "authorization", absl::StrCat("Bearer ", g_token))));
  EXPECT_EQ(g_num_token_fetches.load(), 1);
  // Trigger update that changes the route config, thus causing the
  // dynamic filters to be recreated.
  // We insert a route that matches requests with the header "foo" and
  // has a non-forwarding action, which will cause the client to fail RPCs
  // that hit this route.
  RouteConfiguration route_config = default_route_config_;
  *route_config.mutable_virtual_hosts(0)->add_routes() =
      route_config.virtual_hosts(0).routes(0);
  auto* header_matcher = route_config.mutable_virtual_hosts(0)
                             ->mutable_routes(0)
                             ->mutable_match()
                             ->add_headers();
  header_matcher->set_name("foo");
  header_matcher->set_present_match(true);
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_non_forwarding_action();
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithGcpAuthnFilter(), route_config);
  // Send RPCs with the header "foo" and wait for them to start failing.
  // When they do, we know that the client has seen the update.
  SendRpcsUntilFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                       "Matching route has inappropriate action",
                       /*timeout_ms=*/15000,
                       RpcOptions().set_metadata({{"foo", "bar"}}));
  // Now send an RPC without the header, which will go through the new
  // instance of the GCP auth filter.
  CheckRpcSendOk(DEBUG_LOCATION);
  // Make sure we didn't re-fetch the token.
  EXPECT_EQ(g_num_token_fetches.load(), 1);
}

TEST_P(XdsGcpAuthnEnd2endTest, FilterIgnoredWhenEnvVarNotSet) {
  // Construct auth token.
  g_audience = kAudience;
  std::string token = MakeToken(grpc_core::Timestamp::InfFuture());
  g_token = token.c_str();
  // Set xDS resources.
  CreateAndStartBackends(1, /*xds_enabled=*/false,
                         CreateTlsServerCredentials());
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithGcpAuthnFilter(/*optional=*/true),
      default_route_config_);
  balancer_->ads_service()->SetCdsResource(BuildClusterWithAudience(kAudience));
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Send an RPC and check that it does not have an auth token.
  std::multimap<std::string, std::string> server_initial_metadata;
  Status status = SendRpc(RpcOptions().set_echo_metadata_initially(true),
                          /*response=*/nullptr, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_THAT(
      server_initial_metadata,
      ::testing::Not(::testing::Contains(::testing::Key("authorization"))));
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
