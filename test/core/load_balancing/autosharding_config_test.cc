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

#include "src/core/lib/channel/channel_args.h"
#include "src/core/service_config/service_config_impl.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace grpc_core {
namespace testing {
namespace {

TEST(AutoShardingConfigTest, ValidConfig) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"autosharding_experimental\":{\n"
      "      \"channelFactoryKey\": \"sharding_service\",\n"
      "      \"autoshardingTarget\": \"my_autosharding_target\",\n"
      "      \"keyHeaderName\": \"x-slice-key\"\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  EXPECT_NE(*service_config, nullptr);
}

TEST(AutoShardingConfigTest, ValidConfigWithOptionalFields) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"autosharding_experimental\":{\n"
      "      \"channelFactoryKey\": \"sharding_service\",\n"
      "      \"autoshardingTarget\": \"my_autosharding_target\",\n"
      "      \"keyHeaderName\": \"x-slice-key\",\n"
      "      \"enableFallback\": true,\n"
      "      \"initialAssignmentTimeout\": \"10s\"\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  EXPECT_NE(*service_config, nullptr);
}

TEST(AutoShardingConfigTest, InvalidTypes) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"autosharding_experimental\":{\n"
      "      \"channelFactoryKey\": 5,\n"
      "      \"autoshardingTarget\": true,\n"
      "      \"keyHeaderName\": [],\n"
      "      \"enableFallback\": \"true\",\n"
      "      \"initialAssignmentTimeout\": {}\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  ASSERT_FALSE(service_config.ok());
  EXPECT_EQ(service_config.status(),
            absl::InvalidArgumentError(
                "errors validating service config: [field:loadBalancingConfig "
                "error:errors validating autosharding LB policy config: "
                "[field:autoshardingTarget error:is not a string; "
                "field:channelFactoryKey error:is not a string; "
                "field:enableFallback error:is not a boolean; "
                "field:initialAssignmentTimeout error:is not a string; "
                "field:keyHeaderName error:is not a string]]"));
}

TEST(AutoShardingConfigTest, FieldsNotPresent) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"autosharding_experimental\":{\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  ASSERT_FALSE(service_config.ok());
  EXPECT_EQ(service_config.status(),
            absl::InvalidArgumentError(
                "errors validating service config: [field:loadBalancingConfig "
                "error:errors validating autosharding LB policy config: "
                "[field:autoshardingTarget error:field not present; "
                "field:channelFactoryKey error:field not present; "
                "field:keyHeaderName error:field not present]]"));
}

TEST(AutoShardingConfigTest, EmptyFields) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"autosharding_experimental\":{\n"
      "      \"channelFactoryKey\": \"\",\n"
      "      \"autoshardingTarget\": \"\",\n"
      "      \"keyHeaderName\": \"\"\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  ASSERT_FALSE(service_config.ok());
  EXPECT_EQ(service_config.status(),
            absl::InvalidArgumentError(
                "errors validating service config: [field:loadBalancingConfig "
                "error:errors validating autosharding LB policy config: "
                "[field:autoshardingTarget error:must be non-empty; "
                "field:channelFactoryKey error:must be non-empty; "
                "field:keyHeaderName error:must be non-empty]]"));
}

TEST(AutoShardingConfigTest, ZeroInitialAssignmentTimeout) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"autosharding_experimental\":{\n"
      "      \"channelFactoryKey\": \"sharding_service\",\n"
      "      \"autoshardingTarget\": \"my_autosharding_target\",\n"
      "      \"keyHeaderName\": \"x-slice-key\",\n"
      "      \"initialAssignmentTimeout\": \"0s\"\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  ASSERT_FALSE(service_config.ok());
  EXPECT_EQ(service_config.status(),
            absl::InvalidArgumentError(
                "errors validating service config: [field:loadBalancingConfig "
                "error:errors validating autosharding LB policy config: "
                "[field:initialAssignmentTimeout error:must be greater than "
                "zero]]"));
}

TEST(AutoShardingConfigTest, NegativeInitialAssignmentTimeout) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"autosharding_experimental\":{\n"
      "      \"channelFactoryKey\": \"sharding_service\",\n"
      "      \"autoshardingTarget\": \"my_autosharding_target\",\n"
      "      \"keyHeaderName\": \"x-slice-key\",\n"
      "      \"initialAssignmentTimeout\": \"-1s\"\n"
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
          "error:errors validating autosharding LB policy config: "
          "[field:initialAssignmentTimeout error:seconds must be in the range "
          "[0, 315576000000]]]"));
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
