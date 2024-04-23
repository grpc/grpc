//
// Copyright 2022 gRPC authors.
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

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/service_config/service_config.h"
#include "src/core/service_config/service_config_impl.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

TEST(WeightedRoundRobinConfigTest, EmptyConfig) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"weighted_round_robin\":{\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  ASSERT_TRUE(service_config.ok());
  EXPECT_NE(*service_config, nullptr);
}

TEST(WeightedRoundRobinConfigTest, InvalidTypes) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"weighted_round_robin\":{\n"
      "      \"enableOobLoadReport\": 5,\n"
      "      \"oobReportingPeriod\": true,\n"
      "      \"blackoutPeriod\": [],\n"
      "      \"weightUpdatePeriod\": {},\n"
      "      \"weightExpirationPeriod\": {},\n"
      "      \"errorUtilizationPenalty\": []\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  ASSERT_FALSE(service_config.ok());
  EXPECT_EQ(
      service_config.status(),
      absl::InvalidArgumentError(
          "errors validating service config: [field:loadBalancingConfig "
          "error:errors validating weighted_round_robin LB policy config: ["
          "field:blackoutPeriod error:is not a string; "
          "field:enableOobLoadReport error:is not a boolean; "
          "field:errorUtilizationPenalty error:is not a number; "
          "field:oobReportingPeriod error:is not a string; "
          "field:weightExpirationPeriod error:is not a string; "
          "field:weightUpdatePeriod error:is not a string]]"));
}

TEST(WeightedRoundRobinConfigTest, InvalidValues) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"weighted_round_robin\":{\n"
      "      \"errorUtilizationPenalty\": -1.0\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  ASSERT_FALSE(service_config.ok());
  EXPECT_EQ(
      service_config.status(),
      absl::InvalidArgumentError(
          "errors validating service config: [field:loadBalancingConfig "
          "error:errors validating weighted_round_robin LB policy config: ["
          "field:errorUtilizationPenalty error:must be non-negative]]"));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
