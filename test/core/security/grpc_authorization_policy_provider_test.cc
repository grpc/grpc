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

#include "src/core/lib/security/authorization/grpc_authorization_policy_provider.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/grpc_security.h>

#include "src/core/lib/security/authorization/grpc_authorization_engine.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"

#define VALID_POLICY_PATH_1 \
  "test/core/security/authorization/test_policies/valid_policy_1.json"
#define VALID_POLICY_PATH_2 \
  "test/core/security/authorization/test_policies/valid_policy_2.json"
#define INVALID_POLICY_PATH \
  "test/core/security/authorization/test_policies/invalid_policy.json"

namespace grpc_core {

TEST(AuthorizationPolicyProviderTest, StaticDataInitializationSuccessful) {
  auto provider = StaticDataAuthorizationPolicyProvider::Create(
      testing::GetFileContents(VALID_POLICY_PATH_1));
  ASSERT_TRUE(provider.ok());
  auto engines = (*provider)->engines();
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
     StaticDataInitializationFailedInvalidPolicy) {
  auto provider = StaticDataAuthorizationPolicyProvider::Create(
      testing::GetFileContents(INVALID_POLICY_PATH));
  EXPECT_EQ(provider.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(provider.status().message(), "\"name\" field is not present.");
}

TEST(AuthorizationPolicyProviderTest,
     FileWatcherInitializationSuccessValidPolicy) {
  auto tmp_authz_policy = std::make_unique<testing::TmpFile>(
      testing::GetFileContents(VALID_POLICY_PATH_1));
  auto provider = FileWatcherAuthorizationPolicyProvider::Create(
      tmp_authz_policy->name(), /*refresh_interval_sec=*/1);
  ASSERT_TRUE(provider.ok());
  auto engines = (*provider)->engines();
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
  auto tmp_authz_policy = std::make_unique<testing::TmpFile>(
      testing::GetFileContents(INVALID_POLICY_PATH));
  auto provider = FileWatcherAuthorizationPolicyProvider::Create(
      tmp_authz_policy->name(), /*refresh_interval_sec=*/1);
  EXPECT_EQ(provider.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(provider.status().message(), "\"name\" field is not present.");
}

TEST(AuthorizationPolicyProviderTest, FileWatcherSuccessValidPolicyRefresh) {
  auto tmp_authz_policy = std::make_unique<testing::TmpFile>(
      testing::GetFileContents(VALID_POLICY_PATH_1));
  auto provider = FileWatcherAuthorizationPolicyProvider::Create(
      tmp_authz_policy->name(), /*refresh_interval_sec=*/1);
  ASSERT_TRUE(provider.ok());
  auto engines = (*provider)->engines();
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
  gpr_event on_reload_done;
  gpr_event_init(&on_reload_done);
  std::function<void(bool contents_changed, absl::Status status)> callback =
      [&on_reload_done](bool contents_changed, absl::Status status) {
        if (contents_changed) {
          EXPECT_TRUE(status.ok());
          gpr_event_set(&on_reload_done, reinterpret_cast<void*>(1));
        }
      };
  dynamic_cast<FileWatcherAuthorizationPolicyProvider*>(provider->get())
      ->SetCallbackForTesting(std::move(callback));
  // Rewrite the file with a different valid authorization policy.
  tmp_authz_policy->RewriteFile(testing::GetFileContents(VALID_POLICY_PATH_2));
  // Wait for the provider's refresh thread to read the updated files.
  ASSERT_EQ(
      gpr_event_wait(&on_reload_done, gpr_inf_future(GPR_CLOCK_MONOTONIC)),
      reinterpret_cast<void*>(1));
  engines = (*provider)->engines();
  allow_engine =
      dynamic_cast<GrpcAuthorizationEngine*>(engines.allow_engine.get());
  ASSERT_NE(allow_engine, nullptr);
  EXPECT_EQ(allow_engine->action(), Rbac::Action::kAllow);
  EXPECT_EQ(allow_engine->num_policies(), 2);
  deny_engine =
      dynamic_cast<GrpcAuthorizationEngine*>(engines.deny_engine.get());
  EXPECT_EQ(deny_engine, nullptr);
}

TEST(AuthorizationPolicyProviderTest,
     FileWatcherInvalidPolicyRefreshSkipReload) {
  auto tmp_authz_policy = std::make_unique<testing::TmpFile>(
      testing::GetFileContents(VALID_POLICY_PATH_1));
  auto provider = FileWatcherAuthorizationPolicyProvider::Create(
      tmp_authz_policy->name(), /*refresh_interval_sec=*/1);
  ASSERT_TRUE(provider.ok());
  auto engines = (*provider)->engines();
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
  gpr_event on_reload_done;
  gpr_event_init(&on_reload_done);
  std::function<void(bool contents_changed, absl::Status status)> callback =
      [&on_reload_done](bool contents_changed, absl::Status status) {
        if (contents_changed) {
          EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
          EXPECT_EQ(status.message(), "\"name\" field is not present.");
          gpr_event_set(&on_reload_done, reinterpret_cast<void*>(1));
        }
      };
  dynamic_cast<FileWatcherAuthorizationPolicyProvider*>(provider->get())
      ->SetCallbackForTesting(std::move(callback));
  // Skips the following policy update, and continues to use the valid policy.
  tmp_authz_policy->RewriteFile(testing::GetFileContents(INVALID_POLICY_PATH));
  // Wait for the provider's refresh thread to read the updated files.
  ASSERT_EQ(
      gpr_event_wait(&on_reload_done, gpr_inf_future(GPR_CLOCK_MONOTONIC)),
      reinterpret_cast<void*>(1));
  engines = (*provider)->engines();
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
  dynamic_cast<FileWatcherAuthorizationPolicyProvider*>(provider->get())
      ->SetCallbackForTesting(nullptr);
}

TEST(AuthorizationPolicyProviderTest, FileWatcherRecoversFromFailure) {
  auto tmp_authz_policy = std::make_unique<testing::TmpFile>(
      testing::GetFileContents(VALID_POLICY_PATH_1));
  auto provider = FileWatcherAuthorizationPolicyProvider::Create(
      tmp_authz_policy->name(), /*refresh_interval_sec=*/1);
  ASSERT_TRUE(provider.ok());
  auto engines = (*provider)->engines();
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
  gpr_event on_first_reload_done;
  gpr_event_init(&on_first_reload_done);
  std::function<void(bool contents_changed, absl::Status status)> callback1 =
      [&on_first_reload_done](bool contents_changed, absl::Status status) {
        if (contents_changed) {
          EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
          EXPECT_EQ(status.message(), "\"name\" field is not present.");
          gpr_event_set(&on_first_reload_done, reinterpret_cast<void*>(1));
        }
      };
  dynamic_cast<FileWatcherAuthorizationPolicyProvider*>(provider->get())
      ->SetCallbackForTesting(std::move(callback1));
  // Skips the following policy update, and continues to use the valid policy.
  tmp_authz_policy->RewriteFile(testing::GetFileContents(INVALID_POLICY_PATH));
  // Wait for the provider's refresh thread to read the updated files.
  ASSERT_EQ(gpr_event_wait(&on_first_reload_done,
                           gpr_inf_future(GPR_CLOCK_MONOTONIC)),
            reinterpret_cast<void*>(1));
  engines = (*provider)->engines();
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
  gpr_event on_second_reload_done;
  gpr_event_init(&on_second_reload_done);
  std::function<void(bool contents_changed, absl::Status status)> callback2 =
      [&on_second_reload_done](bool contents_changed, absl::Status status) {
        if (contents_changed) {
          EXPECT_TRUE(status.ok());
          gpr_event_set(&on_second_reload_done, reinterpret_cast<void*>(1));
        }
      };
  dynamic_cast<FileWatcherAuthorizationPolicyProvider*>(provider->get())
      ->SetCallbackForTesting(std::move(callback2));
  // Rewrite the file with a valid authorization policy.
  tmp_authz_policy->RewriteFile(testing::GetFileContents(VALID_POLICY_PATH_2));
  // Wait for the provider's refresh thread to read the updated files.
  ASSERT_EQ(gpr_event_wait(&on_second_reload_done,
                           gpr_inf_future(GPR_CLOCK_MONOTONIC)),
            reinterpret_cast<void*>(1));
  engines = (*provider)->engines();
  allow_engine =
      dynamic_cast<GrpcAuthorizationEngine*>(engines.allow_engine.get());
  ASSERT_NE(allow_engine, nullptr);
  EXPECT_EQ(allow_engine->action(), Rbac::Action::kAllow);
  EXPECT_EQ(allow_engine->num_policies(), 2);
  deny_engine =
      dynamic_cast<GrpcAuthorizationEngine*>(engines.deny_engine.get());
  EXPECT_EQ(deny_engine, nullptr);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
