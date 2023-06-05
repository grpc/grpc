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

#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpcpp/ext/server_metric_recorder.h>

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/lib/config/config_vars.h"
#include "src/proto/grpc/testing/xds/v3/client_side_weighted_round_robin.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/wrr_locality.grpc.pb.h"
#include "test/core/util/scoped_env_var.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::extensions::load_balancing_policies::
    client_side_weighted_round_robin::v3::ClientSideWeightedRoundRobin;
using ::envoy::extensions::load_balancing_policies::wrr_locality::v3::
    WrrLocality;
using ::grpc_core::testing::ScopedExperimentalEnvVar;

using WrrTest = XdsEnd2endTest;

INSTANTIATE_TEST_SUITE_P(XdsTest, WrrTest, ::testing::Values(XdsTestType()),
                         &XdsTestType::Name);

TEST_P(WrrTest, Basic) {
  CreateAndStartBackends(3);
  // Expected weights = qps / (util + (eps/qps)) =
  //   1/(0.2+0.2) : 1/(0.3+0.3) : 2/(1.5+0.1) = 6:4:3
  // where util is app_utilization if set, or cpu_utilization.
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
      gpr_log(GPR_INFO, "request counts: %" PRIuPTR " %" PRIuPTR " %" PRIuPTR,
              backends_[0]->backend_service()->request_count(),
              backends_[1]->backend_service()->request_count(),
              backends_[2]->backend_service()->request_count());
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
