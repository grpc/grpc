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
//

#include <memory>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/client_channel/backup_poller.h"
#include "src/core/config/config_vars.h"
#include "src/core/util/env.h"
#include "src/proto/grpc/lookup/v1/rls.pb.h"
#include "src/proto/grpc/lookup/v1/rls_config.pb.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/cpp/end2end/rls_server.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"

namespace grpc {
namespace testing {
namespace {

using ::grpc::lookup::v1::RouteLookupClusterSpecifier;
using ::grpc::lookup::v1::RouteLookupConfig;

using ::grpc_core::testing::ScopedExperimentalEnvVar;

constexpr char kRlsTestKey[] = "test_key";
constexpr char kRlsTestKey1[] = "key1";
constexpr char kRlsTestValue[] = "test_value";
constexpr char kRlsHostKey[] = "host_key";
constexpr char kRlsServiceKey[] = "service_key";
constexpr char kRlsServiceValue[] = "grpc.testing.EchoTestService";
constexpr char kRlsMethodKey[] = "method_key";
constexpr char kRlsMethodValue[] = "Echo";
constexpr char kRlsConstantKey[] = "constant_key";
constexpr char kRlsConstantValue[] = "constant_value";
constexpr char kRlsClusterSpecifierPluginInstanceName[] = "rls_plugin_instance";

class RlsTest : public XdsEnd2endTest {
 protected:
  class RlsServerThread : public ServerThread {
   public:
    explicit RlsServerThread(XdsEnd2endTest* test_obj)
        : ServerThread(test_obj, /*use_xds_enabled_server=*/false),
          rls_service_(new RlsServiceImpl()) {}

    RlsServiceImpl* rls_service() { return rls_service_.get(); }

   private:
    const char* Type() override { return "Rls"; }

    void RegisterAllServices(ServerBuilder* builder) override {
      builder->RegisterService(rls_service_.get());
    }

    void StartAllServices() override { rls_service_->Start(); }

    void ShutdownAllServices() override { rls_service_->Shutdown(); }

    std::shared_ptr<RlsServiceImpl> rls_service_;
  };

  RlsTest() {
    rls_server_ = std::make_unique<RlsServerThread>(this);
    rls_server_->Start();
  }

  void TearDown() override {
    rls_server_->Shutdown();
    XdsEnd2endTest::TearDown();
  }

  std::unique_ptr<RlsServerThread> rls_server_;
};

// Test both with and without RDS.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, RlsTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_enable_rds_testing()),
    &XdsTestType::Name);

TEST_P(RlsTest, XdsRoutingClusterSpecifierPlugin) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RLS_LB");
  CreateAndStartBackends(2);
  const char* kNewClusterName = "new_cluster";
  const char* kNewEdsServiceName = "new_eds_service_name";
  const size_t kNumEchoRpcs = 5;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName));
  // Populate new CDS resources.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
  // Prepare the RLSLookupConfig and configure all the keys; change route
  // configurations to use cluster specifier plugin.
  rls_server_->rls_service()->SetResponse(
      BuildRlsRequest({{kRlsTestKey, kRlsTestValue},
                       {kRlsHostKey, kServerName},
                       {kRlsServiceKey, kRlsServiceValue},
                       {kRlsMethodKey, kRlsMethodValue},
                       {kRlsConstantKey, kRlsConstantValue}}),
      BuildRlsResponse({kNewClusterName}));
  RouteLookupConfig route_lookup_config;
  auto* key_builder = route_lookup_config.add_grpc_keybuilders();
  auto* name = key_builder->add_names();
  name->set_service(kRlsServiceValue);
  name->set_method(kRlsMethodValue);
  auto* header = key_builder->add_headers();
  header->set_key(kRlsTestKey);
  header->add_names(kRlsTestKey1);
  header->add_names("key2");
  auto* extra_keys = key_builder->mutable_extra_keys();
  extra_keys->set_host(kRlsHostKey);
  extra_keys->set_service(kRlsServiceKey);
  extra_keys->set_method(kRlsMethodKey);
  (*key_builder->mutable_constant_keys())[kRlsConstantKey] = kRlsConstantValue;
  route_lookup_config.set_lookup_service(
      absl::StrCat("localhost:", rls_server_->port()));
  route_lookup_config.set_cache_size_bytes(5000);
  RouteLookupClusterSpecifier rls;
  *rls.mutable_route_lookup_config() = std::move(route_lookup_config);
  RouteConfiguration new_route_config = default_route_config_;
  auto* plugin = new_route_config.add_cluster_specifier_plugins();
  plugin->mutable_extension()->set_name(kRlsClusterSpecifierPluginInstanceName);
  plugin->mutable_extension()->mutable_typed_config()->PackFrom(rls);
  auto* default_route =
      new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  default_route->mutable_route()->set_cluster_specifier_plugin(
      kRlsClusterSpecifierPluginInstanceName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  auto rpc_options = RpcOptions().set_metadata({{kRlsTestKey1, kRlsTestValue}});
  WaitForAllBackends(DEBUG_LOCATION, 1, 2, /*check_status=*/nullptr,
                     WaitForBackendOptions(), rpc_options);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs, rpc_options);
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[1]->backend_service()->request_count());
}

TEST_P(RlsTest, XdsRoutingClusterSpecifierPluginDisabled) {
  grpc_core::testing::ScopedEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RLS_LB",
                                           "false");
  CreateAndStartBackends(1);
  // Populate new EDS resources.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Prepare the RLSLookupConfig and configure all the keys; change route
  // configurations to use cluster specifier plugin.
  RouteLookupConfig route_lookup_config;
  auto* key_builder = route_lookup_config.add_grpc_keybuilders();
  auto* name = key_builder->add_names();
  name->set_service(kRlsServiceValue);
  name->set_method(kRlsMethodValue);
  auto* header = key_builder->add_headers();
  header->set_key(kRlsTestKey);
  header->add_names(kRlsTestKey1);
  route_lookup_config.set_lookup_service(
      absl::StrCat("localhost:", rls_server_->port()));
  route_lookup_config.set_cache_size_bytes(5000);
  RouteLookupClusterSpecifier rls;
  *rls.mutable_route_lookup_config() = std::move(route_lookup_config);
  RouteConfiguration new_route_config = default_route_config_;
  auto* plugin = new_route_config.add_cluster_specifier_plugins();
  plugin->mutable_extension()->set_name(kRlsClusterSpecifierPluginInstanceName);
  plugin->mutable_extension()->mutable_typed_config()->PackFrom(rls);
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route->mutable_route()->set_cluster_specifier_plugin(
      kRlsClusterSpecifierPluginInstanceName);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  // Ensure we ignore the cluster specifier plugin and send traffic according to
  // the default route.
  auto rpc_options = RpcOptions().set_metadata({{kRlsTestKey1, kRlsTestValue}});
  WaitForAllBackends(DEBUG_LOCATION, 0, 1, /*check_status=*/nullptr,
                     WaitForBackendOptions(), rpc_options);
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
