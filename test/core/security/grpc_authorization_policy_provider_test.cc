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

#include <grpc/support/port_platform.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/core/lib/security/authorization/grpc_authorization_policy_provider.h"
#include "test/core/util/test_config.h"

namespace grpc_core {

TEST(AuthorizationPolicyProviderTest, StaticDataInitializationSuccessful) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ]"
      "}";
  auto provider_or =
      StaticDataAuthorizationPolicyProvider::Create(authz_policy);
  ASSERT_TRUE(provider_or.ok());
  auto provider = provider_or.value();
  EXPECT_EQ(
      reinterpret_cast<GrpcAuthorizationEngine*>(provider->allow_engine().get())
          ->GetType(),
      Rbac::Action::kAllow);
  EXPECT_EQ(
      reinterpret_cast<GrpcAuthorizationEngine*>(provider->deny_engine().get())
          ->GetType(),
      Rbac::Action::kDeny);
}

TEST(AuthorizationPolicyProviderTest,
     StaticDataInitializationFailedInvalidPolicy) {
  const char* authz_policy = "{}";
  auto provider_or =
      StaticDataAuthorizationPolicyProvider::Create(authz_policy);
  EXPECT_EQ(provider_or.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(provider_or.status().message(), "\"name\" field is not present.");
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
