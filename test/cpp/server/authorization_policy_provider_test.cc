// Copyright 2021 gRPC authors.
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

#include <grpcpp/security/authorization_policy_provider.h>
#include <gtest/gtest.h>

#include "test/core/util/test_config.h"

namespace grpc {

TEST(AuthorizationPolicyProviderTest, StaticDataCreateReturnsProvider) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ]"
      "}";
  grpc::Status status;
  auto provider = experimental::StaticDataAuthorizationPolicyProvider::Create(
      authz_policy, &status);
  ASSERT_NE(provider, nullptr);
  EXPECT_NE(provider->c_provider(), nullptr);
  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(status.error_message().empty());
}

TEST(AuthorizationPolicyProviderTest, StaticDataCreateReturnsErrorStatus) {
  const char* authz_policy = "{}";
  grpc::Status status;
  auto provider = experimental::StaticDataAuthorizationPolicyProvider::Create(
      authz_policy, &status);
  ASSERT_EQ(provider, nullptr);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  EXPECT_EQ(status.error_message(), "\"name\" field is not present.");
}

}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  return RUN_ALL_TESTS();
}
