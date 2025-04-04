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

#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/client_channel/backup_poller.h"
#include "src/core/config/config_vars.h"
#include "test/core/end2end/fixtures/http_proxy_fixture.h"
#include "test/core/test_util/resolve_localhost_ip46.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"

namespace grpc {
namespace testing {
namespace {

using envoy::extensions::transport_sockets::http_11_proxy::v3::
    Http11ProxyUpstreamTransport;

class XdsHttpProxyTest : public XdsEnd2endTest {
 protected:
  void SetUp() override {
    http_proxy_ = grpc_end2end_http_proxy_create(nullptr);
    XdsEnd2endTest::SetUp();
  }

  void TearDown() override {
    XdsEnd2endTest::TearDown();
    grpc_end2end_http_proxy_destroy(http_proxy_);
  }

  grpc_end2end_http_proxy* http_proxy_;
};

INSTANTIATE_TEST_SUITE_P(XdsTest, XdsHttpProxyTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

TEST_P(XdsHttpProxyTest, TransportProxyInClusterAndProxyAddressInEndpoint) {
  grpc_core::testing::ScopedExperimentalEnvVar env(
      "GRPC_EXPERIMENTAL_XDS_HTTP_CONNECT");
  CreateAndStartBackends(1);
  // Set transport socket in CDS.
  Cluster cluster = default_cluster_;
  cluster.mutable_transport_socket()->mutable_typed_config()->PackFrom(
      Http11ProxyUpstreamTransport());
  balancer_->ads_service()->SetCdsResource(cluster);
  // Set proxy address in EDS metadata.
  ClusterLoadAssignment endpoints = BuildEdsResource(
      EdsResourceArgs({{"locality0", CreateEndpointsForBackends()}}));
  envoy::config::core::v3::Address proxy_address_proto;
  auto* socket_address = proxy_address_proto.mutable_socket_address();
  socket_address->set_address(grpc_core::LocalIp());
  socket_address->set_port_value(
      grpc_end2end_http_proxy_get_proxy_port(http_proxy_));
  auto& metadata_map = *endpoints.mutable_endpoints(0)
                            ->mutable_lb_endpoints(0)
                            ->mutable_metadata()
                            ->mutable_typed_filter_metadata();
  metadata_map["envoy.http11_proxy_transport_socket.proxy_address"].PackFrom(
      proxy_address_proto);
  balancer_->ads_service()->SetEdsResource(endpoints);
  // Everything should work.
  CheckRpcSendOk(DEBUG_LOCATION);
  // Proxy should have seen one connection.
  EXPECT_EQ(grpc_end2end_http_proxy_num_connections(http_proxy_), 1);
}

TEST_P(XdsHttpProxyTest, TransportProxyInClusterButNoProxyAddressInEndpoint) {
  grpc_core::testing::ScopedExperimentalEnvVar env(
      "GRPC_EXPERIMENTAL_XDS_HTTP_CONNECT");
  CreateAndStartBackends(1);
  // Set transport socket in CDS.
  Cluster cluster = default_cluster_;
  cluster.mutable_transport_socket()->mutable_typed_config()->PackFrom(
      Http11ProxyUpstreamTransport());
  balancer_->ads_service()->SetCdsResource(cluster);
  // Set proxy address in EDS metadata.
  ClusterLoadAssignment endpoints = BuildEdsResource(
      EdsResourceArgs({{"locality0", CreateEndpointsForBackends()}}));
  balancer_->ads_service()->SetEdsResource(endpoints);
  // Everything should work.
  CheckRpcSendOk(DEBUG_LOCATION);
  // Proxy should not have seen any connections.
  EXPECT_EQ(grpc_end2end_http_proxy_num_connections(http_proxy_), 0);
}

TEST_P(XdsHttpProxyTest, ProxyAddressInEndpointButNoTransportProxyInCluster) {
  grpc_core::testing::ScopedExperimentalEnvVar env(
      "GRPC_EXPERIMENTAL_XDS_HTTP_CONNECT");
  CreateAndStartBackends(1);
  // Set proxy address in EDS metadata.
  ClusterLoadAssignment endpoints = BuildEdsResource(
      EdsResourceArgs({{"locality0", CreateEndpointsForBackends()}}));
  envoy::config::core::v3::Address proxy_address_proto;
  auto* socket_address = proxy_address_proto.mutable_socket_address();
  socket_address->set_address(grpc_core::LocalIp());
  socket_address->set_port_value(
      grpc_end2end_http_proxy_get_proxy_port(http_proxy_));
  auto& metadata_map = *endpoints.mutable_endpoints(0)
                            ->mutable_lb_endpoints(0)
                            ->mutable_metadata()
                            ->mutable_typed_filter_metadata();
  metadata_map["envoy.http11_proxy_transport_socket.proxy_address"].PackFrom(
      proxy_address_proto);
  balancer_->ads_service()->SetEdsResource(endpoints);
  // Everything should work.
  CheckRpcSendOk(DEBUG_LOCATION);
  // Proxy should not have seen any connections.
  EXPECT_EQ(grpc_end2end_http_proxy_num_connections(http_proxy_), 0);
}

TEST_P(XdsHttpProxyTest, CdsNackedWhenNotEnabled) {
  // Set transport socket in CDS.
  Cluster cluster = default_cluster_;
  cluster.mutable_transport_socket()->mutable_typed_config()->PackFrom(
      Http11ProxyUpstreamTransport());
  balancer_->ads_service()->SetCdsResource(cluster);
  // Wait for NACK.
  const auto response_state = WaitForCdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_EQ(response_state->error_message,
            "xDS response validation errors: ["
            "resource index 0: cluster_name: "
            "INVALID_ARGUMENT: errors validating Cluster resource: ["
            "field:transport_socket.typed_config.value["
            "envoy.extensions.transport_sockets.http_11_proxy.v3"
            ".Http11ProxyUpstreamTransport].type_url "
            "error:unsupported transport socket type]]");
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
