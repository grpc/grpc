//
// Copyright 2026 gRPC authors.
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

#include <grpc/grpc.h>

#include "src/core/config/core_configuration.h"
#include "src/core/load_balancing/autosharding/autosharding.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/lb_policy_registry.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {
namespace {

// Parses the JSON form of a loadBalancingConfig array.
absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>> ParseConfig(
    absl::string_view json_string) {
  auto json = JsonParse(json_string);
  if (!json.ok()) return json.status();
  return CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
      *json);
}

TEST(AutoShardingConfigTest, ValidConfig) {
  const char* lb_config_json =
      "[{\n"
      "  \"autosharding_experimental\":{\n"
      "    \"channelFactoryKey\": \"sharding_service\",\n"
      "    \"autoshardingTarget\": \"my_autosharding_target\",\n"
      "    \"keyHeaderName\": \"x-slice-key\"\n"
      "  }\n"
      "}]\n";
  auto config = ParseConfig(lb_config_json);
  ASSERT_TRUE(config.ok()) << config.status();
  ASSERT_NE(*config, nullptr);
  ASSERT_EQ((*config)->name(), "autosharding_experimental");
  auto* autosharding_config = DownCast<AutoShardingLbConfig*>(config->get());
  EXPECT_EQ(autosharding_config->channel_factory_key(), "sharding_service");
  EXPECT_EQ(autosharding_config->autosharding_target(),
            "my_autosharding_target");
  EXPECT_EQ(autosharding_config->key_header_name().as_string_view(),
            "x-slice-key");
  // The optional fields get their default values.
  EXPECT_FALSE(autosharding_config->enable_fallback());
  EXPECT_EQ(autosharding_config->initial_assignment_timeout(),
            Duration::Seconds(60));
}

TEST(AutoShardingConfigTest, ValidConfigWithOptionalFields) {
  const char* lb_config_json =
      "[{\n"
      "  \"autosharding_experimental\":{\n"
      "    \"channelFactoryKey\": \"sharding_service\",\n"
      "    \"autoshardingTarget\": \"my_autosharding_target\",\n"
      "    \"keyHeaderName\": \"x-slice-key\",\n"
      "    \"enableFallback\": true,\n"
      "    \"initialAssignmentTimeout\": \"10s\"\n"
      "  }\n"
      "}]\n";
  auto config = ParseConfig(lb_config_json);
  ASSERT_TRUE(config.ok()) << config.status();
  ASSERT_NE(*config, nullptr);
  ASSERT_EQ((*config)->name(), "autosharding_experimental");
  auto* autosharding_config = DownCast<AutoShardingLbConfig*>(config->get());
  EXPECT_EQ(autosharding_config->channel_factory_key(), "sharding_service");
  EXPECT_EQ(autosharding_config->autosharding_target(),
            "my_autosharding_target");
  EXPECT_EQ(autosharding_config->key_header_name().as_string_view(),
            "x-slice-key");
  EXPECT_TRUE(autosharding_config->enable_fallback());
  EXPECT_EQ(autosharding_config->initial_assignment_timeout(),
            Duration::Seconds(10));
}

TEST(AutoShardingConfigTest, InvalidTypes) {
  const char* lb_config_json =
      "[{\n"
      "  \"autosharding_experimental\":{\n"
      "    \"channelFactoryKey\": 5,\n"
      "    \"autoshardingTarget\": true,\n"
      "    \"keyHeaderName\": [],\n"
      "    \"enableFallback\": \"true\",\n"
      "    \"initialAssignmentTimeout\": {}\n"
      "  }\n"
      "}]\n";
  auto config = ParseConfig(lb_config_json);
  ASSERT_FALSE(config.ok());
  EXPECT_EQ(config.status(),
            absl::InvalidArgumentError(
                "errors validating autosharding LB policy config: "
                "[field:autoshardingTarget error:is not a string; "
                "field:channelFactoryKey error:is not a string; "
                "field:enableFallback error:is not a boolean; "
                "field:initialAssignmentTimeout error:is not a string; "
                "field:keyHeaderName error:is not a string]"));
}

TEST(AutoShardingConfigTest, FieldsNotPresent) {
  const char* lb_config_json =
      "[{\n"
      "  \"autosharding_experimental\":{\n"
      "  }\n"
      "}]\n";
  auto config = ParseConfig(lb_config_json);
  ASSERT_FALSE(config.ok());
  EXPECT_EQ(config.status(),
            absl::InvalidArgumentError(
                "errors validating autosharding LB policy config: "
                "[field:autoshardingTarget error:field not present; "
                "field:channelFactoryKey error:field not present; "
                "field:keyHeaderName error:field not present]"));
}

TEST(AutoShardingConfigTest, EmptyFields) {
  const char* lb_config_json =
      "[{\n"
      "  \"autosharding_experimental\":{\n"
      "    \"channelFactoryKey\": \"\",\n"
      "    \"autoshardingTarget\": \"\",\n"
      "    \"keyHeaderName\": \"\"\n"
      "  }\n"
      "}]\n";
  auto config = ParseConfig(lb_config_json);
  ASSERT_FALSE(config.ok());
  EXPECT_EQ(config.status(),
            absl::InvalidArgumentError(
                "errors validating autosharding LB policy config: "
                "[field:autoshardingTarget error:must be non-empty; "
                "field:channelFactoryKey error:must be non-empty; "
                "field:keyHeaderName error:must be non-empty]"));
}

TEST(AutoShardingConfigTest, ZeroInitialAssignmentTimeout) {
  const char* lb_config_json =
      "[{\n"
      "  \"autosharding_experimental\":{\n"
      "    \"channelFactoryKey\": \"sharding_service\",\n"
      "    \"autoshardingTarget\": \"my_autosharding_target\",\n"
      "    \"keyHeaderName\": \"x-slice-key\",\n"
      "    \"initialAssignmentTimeout\": \"0s\"\n"
      "  }\n"
      "}]\n";
  auto config = ParseConfig(lb_config_json);
  ASSERT_FALSE(config.ok());
  EXPECT_EQ(config.status(),
            absl::InvalidArgumentError(
                "errors validating autosharding LB policy config: "
                "[field:initialAssignmentTimeout error:must be greater than "
                "zero]"));
}

TEST(AutoShardingConfigTest, NegativeInitialAssignmentTimeout) {
  const char* lb_config_json =
      "[{\n"
      "  \"autosharding_experimental\":{\n"
      "    \"channelFactoryKey\": \"sharding_service\",\n"
      "    \"autoshardingTarget\": \"my_autosharding_target\",\n"
      "    \"keyHeaderName\": \"x-slice-key\",\n"
      "    \"initialAssignmentTimeout\": \"-1s\"\n"
      "  }\n"
      "}]\n";
  auto config = ParseConfig(lb_config_json);
  ASSERT_FALSE(config.ok());
  EXPECT_EQ(config.status(),
            absl::InvalidArgumentError(
                "errors validating autosharding LB policy config: "
                "[field:initialAssignmentTimeout error:seconds must be in the "
                "range [0, 315576000000]]"));
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
