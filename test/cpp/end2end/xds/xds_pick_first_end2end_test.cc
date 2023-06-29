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
#include "src/proto/grpc/testing/xds/v3/cluster.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/pick_first.pb.h"
#include "test/core/util/scoped_env_var.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/connection_attempt_injector.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::extensions::load_balancing_policies::pick_first::v3::PickFirst;

class PickFirstTest : public XdsEnd2endTest {
 protected:
  absl::variant<Status, size_t> WaitForAnyBackendHit(size_t start, size_t end) {
    Status status;
    size_t index = 3;
    SendRpcsUntil(DEBUG_LOCATION, [&](const RpcResult& result) -> bool {
      if (!result.status.ok()) {
        status = result.status;
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
  // Change cluster to use pick_first with shuffle option.
  auto cluster = default_cluster_;
  PickFirst pick_first;
  pick_first.set_shuffle_address_list(true);
  cluster.mutable_load_balancing_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(pick_first);
  balancer_->ads_service()->SetCdsResource(cluster);
  bool saw_different_backend = false;
  size_t start_index = 0;
  for (size_t i = 0; i < 100 && !saw_different_backend; ++i) {
    // Update EDS resource.  This will send a new address list update to the LB
    // policy.
    balancer_->ads_service()->SetEdsResource(BuildEdsResource(
        EdsResourceArgs({{"locality0", CreateEndpointsForBackends(
                                           start_index, start_index + 3)}})));
    auto result = WaitForAnyBackendHit(start_index, start_index + 3);
    size_t backend_index = absl::get<size_t>(result);
    ASSERT_FALSE(absl::holds_alternative<Status>(result))
        << absl::get<Status>(result).error_message();
    saw_different_backend = (backend_index - start_index) > 0;
    // Toggle between backends 0-2 and 3-5
    start_index = 3 - start_index;
  }
  EXPECT_TRUE(saw_different_backend);
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
