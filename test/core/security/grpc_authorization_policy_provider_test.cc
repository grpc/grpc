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
#include <grpc/grpc_security.h>
#include <gtest/gtest.h>

#include "src/core/lib/security/authorization/grpc_authorization_engine.h"
#include "src/core/lib/security/authorization/grpc_authorization_policy_provider.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"

#define VALID_POLICY_PATH_1 \
  "src/core/lib/security/authorization/test_policies/valid_policy_1.json"
#define VALID_POLICY_PATH_2 \
  "src/core/lib/security/authorization/test_policies/valid_policy_2.json"
#define INVALID_POLICY_PATH \
  "src/core/lib/security/authorization/test_policies/invalid_policy.json"

namespace grpc_core {

TEST(AuthorizationPolicyProviderTest, StaticDataInitializationSuccessful) {
  auto provider = StaticDataAuthorizationPolicyProvider::Create(
      testing::GetFileContents(VALID_POLICY_PATH_1));
  ASSERT_TRUE(provider.ok());
  auto engines = (*provider)->engines();
  ASSERT_NE(engines.allow_engine, nullptr);
  EXPECT_EQ(dynamic_cast<GrpcAuthorizationEngine*>(engines.allow_engine.get())
                ->action(),
            Rbac::Action::kAllow);
  ASSERT_NE(engines.deny_engine, nullptr);
  EXPECT_EQ(dynamic_cast<GrpcAuthorizationEngine*>(engines.deny_engine.get())
                ->action(),
            Rbac::Action::kDeny);
}

TEST(AuthorizationPolicyProviderTest,
     StaticDataInitializationFailedInvalidPolicy) {
  auto provider = StaticDataAuthorizationPolicyProvider::Create(
      testing::GetFileContents(INVALID_POLICY_PATH));
  EXPECT_EQ(provider.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(provider.status().message(), "\"name\" field is not present.");
}

TEST(AuthorizationPolicyProviderTest,
     FileWatcherInitializationSuccessValidPolicy) {
  auto tmp_authz_policy = absl::make_unique<testing::TmpFile>(
      testing::GetFileContents(VALID_POLICY_PATH_1));
  auto provider = FileWatcherAuthorizationPolicyProvider::Create(
      tmp_authz_policy->name(), /*refresh_interval_sec=*/1);
  ASSERT_TRUE(provider.ok());
  AuthorizationEngines engines = (*provider)->engines();
  auto* allow_engine =
      dynamic_cast<GrpcAuthorizationEngine*>(engines.allow_engine.get());
  ASSERT_NE(allow_engine, nullptr);
  EXPECT_EQ(allow_engine->action(), Rbac::Action::kAllow);
  EXPECT_EQ(allow_engine->num_policies(), 1);
  auto* deny_engine =
      dynamic_cast<GrpcAuthorizationEngine*>(engines.deny_engine.get());
  ASSERT_NE(deny_engine, nullptr);
  EXPECT_EQ(deny_engine->action(), Rbac::Action::kDeny);
  EXPECT_EQ(deny_engine->num_policies(), 1);
}

TEST(AuthorizationPolicyProviderTest,
     FileWatcherInitializationFailedInvalidPolicy) {
  auto tmp_authz_policy = absl::make_unique<testing::TmpFile>(
      testing::GetFileContents(INVALID_POLICY_PATH));
  auto provider = FileWatcherAuthorizationPolicyProvider::Create(
      tmp_authz_policy->name(), /*refresh_interval_sec=*/1);
  EXPECT_EQ(provider.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(provider.status().message(), "\"name\" field is not present.");
}

TEST(AuthorizationPolicyProviderTest,
     FileWatcherInitializationSuccessValidPolicyRefresh) {
  auto tmp_authz_policy = absl::make_unique<testing::TmpFile>(
      testing::GetFileContents(VALID_POLICY_PATH_1));
  auto provider = FileWatcherAuthorizationPolicyProvider::Create(
      tmp_authz_policy->name(), /*refresh_interval_sec=*/1);
  ASSERT_TRUE(provider.ok());
  AuthorizationEngines engines = (*provider)->engines();
  auto* allow_engine =
      dynamic_cast<GrpcAuthorizationEngine*>(engines.allow_engine.get());
  ASSERT_NE(allow_engine, nullptr);
  EXPECT_EQ(allow_engine->action(), Rbac::Action::kAllow);
  EXPECT_EQ(allow_engine->num_policies(), 1);
  auto* deny_engine =
      dynamic_cast<GrpcAuthorizationEngine*>(engines.deny_engine.get());
  ASSERT_NE(deny_engine, nullptr);
  EXPECT_EQ(deny_engine->action(), Rbac::Action::kDeny);
  EXPECT_EQ(deny_engine->num_policies(), 1);

  tmp_authz_policy->RewriteFile(testing::GetFileContents(VALID_POLICY_PATH_2));
  // Wait 2 seconds for the provider's refresh thread to read the updated files.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(2, GPR_TIMESPAN)));
  engines = (*provider)->engines();
  allow_engine =
      dynamic_cast<GrpcAuthorizationEngine*>(engines.allow_engine.get());
  ASSERT_NE(allow_engine, nullptr);
  EXPECT_EQ(allow_engine->action(), Rbac::Action::kAllow);
  EXPECT_EQ(allow_engine->num_policies(), 2);
  deny_engine =
      dynamic_cast<GrpcAuthorizationEngine*>(engines.deny_engine.get());
  ASSERT_NE(deny_engine, nullptr);
  EXPECT_EQ(deny_engine->action(), Rbac::Action::kDeny);
  EXPECT_EQ(deny_engine->num_policies(), 0);
}

TEST(AuthorizationPolicyProviderTest,
     FileWatcherInitializationInvalidPolicyRefreshSkipReload) {
  auto tmp_authz_policy = absl::make_unique<testing::TmpFile>(
      testing::GetFileContents(VALID_POLICY_PATH_1));
  auto file_provider = FileWatcherAuthorizationPolicyProvider::Create(
      tmp_authz_policy->name(), /*refresh_interval_sec=*/1);
  ASSERT_TRUE(file_provider.ok());
  auto provider =
      static_cast<grpc_core::FileWatcherAuthorizationPolicyProvider*>(
          (*file_provider).get());
  provider->SetErrorStatusCallback(
      [](grpc_status_code status, const char* error_details) {
        EXPECT_EQ(status, GRPC_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(strcmp(error_details, "\"name\" field is not present."), 0);
      });
  AuthorizationEngines engines = provider->engines();
  auto* allow_engine =
      dynamic_cast<GrpcAuthorizationEngine*>(engines.allow_engine.get());
  ASSERT_NE(allow_engine, nullptr);
  EXPECT_EQ(allow_engine->action(), Rbac::Action::kAllow);
  EXPECT_EQ(allow_engine->num_policies(), 1);
  auto* deny_engine =
      dynamic_cast<GrpcAuthorizationEngine*>(engines.deny_engine.get());
  ASSERT_NE(deny_engine, nullptr);
  EXPECT_EQ(deny_engine->action(), Rbac::Action::kDeny);
  EXPECT_EQ(deny_engine->num_policies(), 1);

  // Skips the following policy update, and continues to use the valid policy.
  tmp_authz_policy->RewriteFile(testing::GetFileContents(INVALID_POLICY_PATH));
  // Wait 2 seconds for the provider's refresh thread to read the updated files.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(2, GPR_TIMESPAN)));
  engines = provider->engines();
  allow_engine =
      dynamic_cast<GrpcAuthorizationEngine*>(engines.allow_engine.get());
  ASSERT_NE(allow_engine, nullptr);
  EXPECT_EQ(allow_engine->action(), Rbac::Action::kAllow);
  EXPECT_EQ(allow_engine->num_policies(), 1);
  deny_engine =
      dynamic_cast<GrpcAuthorizationEngine*>(engines.deny_engine.get());
  ASSERT_NE(deny_engine, nullptr);
  EXPECT_EQ(deny_engine->action(), Rbac::Action::kDeny);
  EXPECT_EQ(deny_engine->num_policies(), 1);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
