// Copyright 2020 gRPC authors.
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

#include "src/core/lib/security/authorization/authorization_engine.h"

#include <gtest/gtest.h>

class AuthorizationEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    deny_policy = envoy_config_rbac_v2_RBAC_new(arena.ptr());
    envoy_config_rbac_v2_RBAC_set_action(deny_policy, 1);
    allow_policy = envoy_config_rbac_v2_RBAC_new(arena.ptr());
    envoy_config_rbac_v2_RBAC_set_action(allow_policy, 0);
  }

  upb::Arena arena;
  envoy_config_rbac_v2_RBAC* deny_policy;
  envoy_config_rbac_v2_RBAC* allow_policy;
};

TEST_F(AuthorizationEngineTest, CreateEngineSuccessOnePolicy) {
  std::vector<envoy_config_rbac_v2_RBAC*> policies{allow_policy};
  std::unique_ptr<AuthorizationEngine> engine =
      AuthorizationEngine::CreateAuthorizationEngine(policies);
  EXPECT_NE(engine, nullptr)
      << "Error: Failed to create an AuthorizationEngine with one policy.";
}

TEST_F(AuthorizationEngineTest, CreateEngineSuccessTwoPolicies) {
  std::vector<envoy_config_rbac_v2_RBAC*> policies{deny_policy, allow_policy};
  std::unique_ptr<AuthorizationEngine> engine =
      AuthorizationEngine::CreateAuthorizationEngine(policies);
  EXPECT_NE(engine, nullptr)
      << "Error: Failed to create an AuthorizationEngine with two policies.";
}

TEST_F(AuthorizationEngineTest, CreateEngineFailNoPolicies) {
  std::vector<envoy_config_rbac_v2_RBAC*> policies{};
  std::unique_ptr<AuthorizationEngine> engine =
      AuthorizationEngine::CreateAuthorizationEngine(policies);
  EXPECT_EQ(engine, nullptr)
      << "Error: Created an AuthorizationEngine without policies.";
}

TEST_F(AuthorizationEngineTest, CreateEngineFailTooManyPolicies) {
  std::vector<envoy_config_rbac_v2_RBAC*> policies{deny_policy, allow_policy,
                                                   deny_policy};
  std::unique_ptr<AuthorizationEngine> engine =
      AuthorizationEngine::CreateAuthorizationEngine(policies);
  EXPECT_EQ(engine, nullptr)
      << "Error: Created an AuthorizationEngine with more than two policies.";
}

TEST_F(AuthorizationEngineTest, CreateEngineFailWrongPolicyOrder) {
  std::vector<envoy_config_rbac_v2_RBAC*> policies{allow_policy, deny_policy};
  std::unique_ptr<AuthorizationEngine> engine =
      AuthorizationEngine::CreateAuthorizationEngine(policies);
  EXPECT_EQ(engine, nullptr) << "Error: Created an AuthorizationEngine with "
                                "policies in the wrong order.";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}