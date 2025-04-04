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
//

#include <grpc/event_engine/endpoint_config.h>
#include <grpcpp/ext/server_metric_recorder.h>

#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "envoy/extensions/load_balancing_policies/client_side_weighted_round_robin/v3/client_side_weighted_round_robin.pb.h"
#include "envoy/extensions/load_balancing_policies/wrr_locality/v3/wrr_locality.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/client_channel/backup_poller.h"
#include "src/core/config/config_vars.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::extensions::load_balancing_policies::
    client_side_weighted_round_robin::v3::ClientSideWeightedRoundRobin;
using ::envoy::extensions::load_balancing_policies::wrr_locality::v3::
    WrrLocality;

class WrrTest : public XdsEnd2endTest {
 protected:
  void SetUp() override {
    // No-op -- tests must explicitly call InitClient().
  }
};

INSTANTIATE_TEST_SUITE_P(XdsTest, WrrTest, ::testing::Values(XdsTestType()),
                         &XdsTestType::Name);

TEST_P(WrrTest, Basic) {
  InitClient();
  CreateAndStartBackends(3);
  // Expected weights = qps / (util + (eps/qps)) =
  //   1/(0.2+0.2) : 1/(0.3+0.3) : 2/(1.5+0.1) = 6:4:3
  backends_[0]->server_metric_recorder()->SetQps(100);
  backends_[0]->server_metric_recorder()->SetEps(20);
  backends_[0]->server_metric_recorder()->SetApplicationUtilization(0.2);
  backends_[1]->server_metric_recorder()->SetQps(100);
  backends_[1]->server_metric_recorder()->SetEps(30);
  backends_[1]->server_metric_recorder()->SetApplicationUtilization(0.3);
  backends_[2]->server_metric_recorder()->SetQps(200);
  backends_[2]->server_metric_recorder()->SetEps(20);
  backends_[2]->server_metric_recorder()->SetApplicationUtilization(1.5);
  auto cluster = default_cluster_;
  WrrLocality wrr_locality;
  wrr_locality.mutable_endpoint_picking_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(ClientSideWeightedRoundRobin());
  cluster.mutable_load_balancing_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(wrr_locality);
  balancer_->ads_service()->SetCdsResource(cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  size_t num_picks = 0;
  SendRpcsUntil(DEBUG_LOCATION, [&](const RpcResult&) {
    if (++num_picks == 13) {
      LOG(INFO) << "request counts: "
                << backends_[0]->backend_service()->request_count() << " "
                << backends_[1]->backend_service()->request_count() << " "
                << backends_[2]->backend_service()->request_count();
      if (backends_[0]->backend_service()->request_count() == 6 &&
          backends_[1]->backend_service()->request_count() == 4 &&
          backends_[2]->backend_service()->request_count() == 3) {
        return false;
      }
      num_picks = 0;
      ResetBackendCounters();
    }
    return true;
  });
}

TEST_P(WrrTest, MetricsHaveLocalityLabel) {
  const auto kEndpointWeights =
      grpc_core::GlobalInstrumentsRegistryTestPeer::
          FindDoubleHistogramHandleByName("grpc.lb.wrr.endpoint_weights")
              .value();
  const std::string target = absl::StrCat("xds:", kServerName);
  const absl::string_view kLabelValues[] = {/*target=*/target};
  // Register stats plugin before initializing client.
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  InitClient();
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  WrrLocality wrr_locality;
  wrr_locality.mutable_endpoint_picking_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(ClientSideWeightedRoundRobin());
  cluster.mutable_load_balancing_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(wrr_locality);
  balancer_->ads_service()->SetCdsResource(cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)},
                        {"locality1", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends(DEBUG_LOCATION);
  // Make sure we have a metric value for each of the two localities.
  EXPECT_THAT(
      stats_plugin->GetDoubleHistogramValue(kEndpointWeights, kLabelValues,
                                            {LocalityNameString("locality0")}),
      ::testing::Optional(::testing::Not(::testing::IsEmpty())));
  EXPECT_THAT(
      stats_plugin->GetDoubleHistogramValue(kEndpointWeights, kLabelValues,
                                            {LocalityNameString("locality1")}),
      ::testing::Optional(::testing::Not(::testing::IsEmpty())));
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
