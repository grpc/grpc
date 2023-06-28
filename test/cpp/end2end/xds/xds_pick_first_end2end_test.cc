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

#include <unistd.h>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/strings/str_cat.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpcpp/support/status.h>

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_channel_args.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/gprpp/env.h"
#include "src/proto/grpc/testing/xds/v3/aggregate_cluster.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/cluster.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/pick_first.pb.h"
#include "test/core/util/scoped_env_var.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/connection_attempt_injector.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::config::cluster::v3::CustomClusterType;
using ::envoy::config::core::v3::HealthStatus;
using ::envoy::extensions::clusters::aggregate::v3::ClusterConfig;
using ::envoy::extensions::load_balancing_policies::pick_first::v3::PickFirst;

class PickFirstTest : public XdsEnd2endTest {
 protected:
  absl::variant<Status, size_t> WaitForAnyBackendHit(size_t start, size_t end) {
    Status status;
    size_t index = 3;
    SendRpcsUntil(DEBUG_LOCATION, [&](const RpcResult& result) -> bool {
      if (!result.status.ok()) {
        status = std::move(result.status);
        return false;
      }
      for (size_t i = start; i < end; ++i) {
        if (backends_[i]->backend_service()->request_count() > 0) {
          backends_[i]->backend_service()->ResetCounters();
          index = i;
          return false;
        }
      }
      return true;
    });
    if (!status.ok()) {
      return status;
    }
    return index;
  }

  void ConfigureCluster(size_t iteration, bool shuffle,
                        size_t first_endpoint_index,
                        size_t stop_endpoint_index) {
    std::string edsService = absl::StrCat("edsservice", iteration);
    // First 3 endpoints go to a service that will have regular pick_first LB
    balancer_->ads_service()->SetEdsResource(BuildEdsResource(
        EdsResourceArgs(
            {{"locality0", CreateEndpointsForBackends(first_endpoint_index,
                                                      stop_endpoint_index)}}),
        edsService));
    PickFirst pick_first;
    pick_first.set_shuffle_address_list(shuffle);
    auto cluster = default_cluster_;
    cluster.clear_lb_policy();
    cluster.clear_load_balancing_policy();
    cluster.mutable_eds_cluster_config()->set_service_name(edsService);
    cluster.mutable_load_balancing_policy()
        ->add_policies()
        ->mutable_typed_extension_config()
        ->mutable_typed_config()
        ->PackFrom(pick_first);

    // Set ordered cluster configuration
    balancer_->ads_service()->SetCdsResource(cluster);
  }

  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      logical_dns_cluster_resolver_response_generator_;
};

// Run both with and without load reporting, just for test coverage.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, PickFirstTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_enable_load_reporting()),
    &XdsTestType::Name);

TEST_P(PickFirstTest, PickFirstConfigurationIsPropagated) {
  grpc_core::testing::ScopedExperimentalEnvVar env_var(
      "GRPC_EXPERIMENTAL_PICKFIRST_LB_CONFIG");
  CreateAndStartBackends(6);
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   default_route_config_);
  // Checking if we go back and forth between shuffled and ordered several times
  size_t times_shuffle_detected = false;
  constexpr size_t kExpectedTimesShuffled = 2;
  // Shuffling has a chance of returning the same permutation. Make sure we
  // retry it several times if needed
  for (size_t attempt = 0;
       attempt < 100 && times_shuffle_detected < kExpectedTimesShuffled;
       ++attempt) {
    // Use first 3 endpoints without shuffle
    ConfigureCluster(attempt * 2, false, 0, 3);
    WaitForBackendOptions opts;
    opts.set_timeout_ms(30000);
    WaitForBackend(DEBUG_LOCATION, 0, nullptr, opts);
    // Confirm pick first LB is used
    CheckRpcSendOk(DEBUG_LOCATION, 5);
    EXPECT_EQ(5, backends_[0]->backend_service()->request_count());
    backends_[0]->backend_service()->ResetCounters();
    // Use endpoints 3-5 with shuffle
    ConfigureCluster(attempt * 2 + 1, true, 3, 6);
    auto res = WaitForAnyBackendHit(3, backends_.size());
    ASSERT_FALSE(absl::holds_alternative<Status>(res))
        << absl::get<Status>(res).error_message();
    CheckRpcSendOk(DEBUG_LOCATION, 5);
    size_t chosen_endpoint = absl::get<size_t>(res);
    EXPECT_EQ(5,
              backends_[chosen_endpoint]->backend_service()->request_count());
    backends_[0]->backend_service()->ResetCounters();
    times_shuffle_detected += chosen_endpoint != 3 ? 1 : 0;
  }
  EXPECT_EQ(times_shuffle_detected, kExpectedTimesShuffled);
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
  grpc::testing::ConnectionAttemptInjector::Init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
