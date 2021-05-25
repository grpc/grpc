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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/authorization/grpc_authorization_engine.h"
#include "src/core/lib/security/authorization/grpc_authorization_policy_provider.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"

#define VALID_POLICY_PATH_1 \
  "src/core/lib/security/authorization/test_policy/valid_policy_1.json"
#define VALID_POLICY_PATH_2 \
  "src/core/lib/security/authorization/test_policy/valid_policy_2.json"
#define INVALID_POLICY_PATH \
  "src/core/lib/security/authorization/test_policy/invalid_policy.json"
#define BAD_PATH "invalid/path"

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
  auto provider = StaticDataAuthorizationPolicyProvider::Create(authz_policy);
  ASSERT_TRUE(provider.ok());
  auto* allow_engine =
      dynamic_cast<GrpcAuthorizationEngine*>((*provider)->allow_engine().get());
  ASSERT_NE(allow_engine, nullptr);
  EXPECT_EQ(allow_engine->action(), Rbac::Action::kAllow);
  auto* deny_engine =
      dynamic_cast<GrpcAuthorizationEngine*>((*provider)->deny_engine().get());
  ASSERT_NE(deny_engine, nullptr);
  EXPECT_EQ(deny_engine->action(), Rbac::Action::kDeny);
}

TEST(AuthorizationPolicyProviderTest,
     StaticDataInitializationFailedInvalidPolicy) {
  const char* authz_policy = "{}";
  auto provider = StaticDataAuthorizationPolicyProvider::Create(authz_policy);
  EXPECT_EQ(provider.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(provider.status().message(), "\"name\" field is not present.");
}

TEST(AuthorizationPolicyProviderTest,
     FileWatcherInitializationFailedInvalidPolicy) {
  auto tmp_authz_policy = absl::make_unique<testing::TmpFile>(
      testing::GetFileContents(INVALID_POLICY_PATH));
  auto provider = FileWatcherAuthorizationPolicyProvider::Create(
      tmp_authz_policy->name(), 1);
  EXPECT_EQ(provider.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(provider.status().message(), "\"name\" field is not present.");
}

TEST(AuthorizationPolicyProviderTest,
     FileWatcherInitializationSuccessValidPolicy) {
  auto tmp_authz_policy = absl::make_unique<testing::TmpFile>(
      testing::GetFileContents(VALID_POLICY_PATH_1));
  auto provider = FileWatcherAuthorizationPolicyProvider::Create(
      tmp_authz_policy->name(), 1);
  ASSERT_TRUE(provider.ok());
  auto* allow_engine =
      dynamic_cast<GrpcAuthorizationEngine*>((*provider)->allow_engine().get());
  ASSERT_NE(allow_engine, nullptr);
  EXPECT_EQ(allow_engine->action(), Rbac::Action::kAllow);
  EXPECT_FALSE(allow_engine->IsEmpty());
  auto* deny_engine =
      dynamic_cast<GrpcAuthorizationEngine*>((*provider)->deny_engine().get());
  ASSERT_NE(deny_engine, nullptr);
  EXPECT_EQ(deny_engine->action(), Rbac::Action::kDeny);
  EXPECT_TRUE(deny_engine->IsEmpty());
}

TEST(AuthorizationPolicyProviderTest,
     FileWatcherInitializationSuccessValidPolicyRefresh) {
  auto tmp_authz_policy = absl::make_unique<testing::TmpFile>(
      testing::GetFileContents(VALID_POLICY_PATH_1));
  auto provider = FileWatcherAuthorizationPolicyProvider::Create(
      tmp_authz_policy->name(), 1);
  ASSERT_TRUE(provider.ok());
  auto* allow_engine =
      dynamic_cast<GrpcAuthorizationEngine*>((*provider)->allow_engine().get());
  ASSERT_NE(allow_engine, nullptr);
  EXPECT_EQ(allow_engine->action(), Rbac::Action::kAllow);
  EXPECT_FALSE(allow_engine->IsEmpty());
  auto* deny_engine =
      dynamic_cast<GrpcAuthorizationEngine*>((*provider)->deny_engine().get());
  ASSERT_NE(deny_engine, nullptr);
  EXPECT_EQ(deny_engine->action(), Rbac::Action::kDeny);
  EXPECT_TRUE(deny_engine->IsEmpty());

  tmp_authz_policy->RewriteFile(testing::GetFileContents(VALID_POLICY_PATH_2));
  // Wait 2 seconds for the provider's refresh thread to read the updated files.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(2, GPR_TIMESPAN)));
  auto* allow_engine1 =
      dynamic_cast<GrpcAuthorizationEngine*>((*provider)->allow_engine().get());
  ASSERT_NE(allow_engine1, nullptr);
  EXPECT_EQ(allow_engine1->action(), Rbac::Action::kAllow);
  EXPECT_FALSE(allow_engine1->IsEmpty());
  auto* deny_engine1 =
      dynamic_cast<GrpcAuthorizationEngine*>((*provider)->deny_engine().get());
  ASSERT_NE(deny_engine1, nullptr);
  EXPECT_EQ(deny_engine1->action(), Rbac::Action::kDeny);
  EXPECT_FALSE(deny_engine1->IsEmpty());
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
