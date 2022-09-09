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

#include "src/cpp/ext/gcp/observability_config.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "test/core/util/test_config.h"

namespace grpc {
namespace internal {
namespace {

TEST(GcpObservabilityConfigJsonParsingTest, Basic) {
  const char* json_str = R"json({
      "cloud_logging": {
        "enabled": true
      },
      "cloud_monitoring": {
        "enabled": true
      },
      "cloud_trace": {
        "enabled": true
      },
      "project_id": "project"
    })json";
  auto json = grpc_core::Json::Parse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ErrorList errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status();
  EXPECT_TRUE(config.cloud_logging.enabled);
  EXPECT_TRUE(config.cloud_monitoring.enabled);
  EXPECT_TRUE(config.cloud_trace.enabled);
  EXPECT_EQ(config.project_id, "project");
}

TEST(GcpObservabilityConfigJsonParsingTest, Defaults) {
  const char* json_str = R"json({
    })json";
  auto json = grpc_core::Json::Parse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  grpc_core::ErrorList errors;
  auto config = grpc_core::LoadFromJson<GcpObservabilityConfig>(
      *json, grpc_core::JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.status();
  EXPECT_FALSE(config.cloud_logging.enabled);
  EXPECT_FALSE(config.cloud_monitoring.enabled);
  EXPECT_FALSE(config.cloud_trace.enabled);
  EXPECT_TRUE(config.project_id.empty());
}

}  // namespace
}  // namespace internal
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
