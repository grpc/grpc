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

#include "src/core/lib/security/authorization/rbac_translator.h"

#include <gtest/gtest.h>

#include "absl/strings/strip.h"

namespace grpc_core {

TEST(GenerateRbacPoliciesTest, InvalidPolicy) {
  std::string authz_policy =
      "{"
      "  \"name\": \"authz-policy\",,"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_FALSE(rbac_policies.ok());
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(absl::StartsWith(rbac_policies.status().message(),
                               "Failed to parse SDK authorization policy."));
}

TEST(GenerateRbacPoliciesTest, MissingAuthorizationPolicyName) {
  std::string authz_policy = "{}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_FALSE(rbac_policies.ok());
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(), "\"name\" field is not present.");
}

TEST(GenerateRbacPoliciesTest, IncorrectAuthorizationPolicyNameType) {
  std::string authz_policy =
      "{"
      "  \"name\": [\"authz_policy\"]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_FALSE(rbac_policies.ok());
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(), "\"name\" is not a string.");
}

TEST(GenerateRbacPoliciesTest, MissingAllowRules) {
  std::string authz_policy =
      "{"
      "  \"name\": \"authz_policy\""
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_FALSE(rbac_policies.ok());
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "\"allow_rules\" field is not present.");
}

TEST(GenerateRbacPoliciesTest, IncorrectAllowRulesType) {
  const std::string authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": {}"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_FALSE(rbac_policies.ok());
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "\"allow_rules\" is not an array.");
}

TEST(GenerateRbacPoliciesTest, IncorrectDenyRulesType) {
  const std::string authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"deny_rules\": 123"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_FALSE(rbac_policies.ok());
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "\"deny_rules\" is not an array.");
}

TEST(GenerateRbacPoliciesTest, IncorrectRuleType) {
  const std::string authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": [\"rule-a\"]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_FALSE(rbac_policies.ok());
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(), "rules 0 is not an object.");
}

TEST(GenerateRbacPoliciesTest, MissingRuleNameField) {
  const std::string authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": [{}]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_FALSE(rbac_policies.ok());
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "policy \"name\" field is not present.");
}

TEST(GenerateRbacPoliciesTest, IncorrectRuleNameType) {
  const std::string authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": 123"
      "    }"
      "  ]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_FALSE(rbac_policies.ok());
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "policy \"name\" is not a string.");
}

TEST(GenerateRbacPoliciesTest, EmptySourceAndRequest) {
  const std::string authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  ASSERT_TRUE(rbac_policies.ok());
  EXPECT_EQ(rbac_policies.value()->allow_policy.action, Rbac::Action::ALLOW);
  EXPECT_EQ(rbac_policies.value()
                ->allow_policy.policies["authz_allow_policy"]
                .permissions.type,
            Rbac::Permission::RuleType::ANY);
  EXPECT_EQ(rbac_policies.value()
                ->allow_policy.policies["authz_allow_policy"]
                .principals.type,
            Rbac::Principal::RuleType::ANY);
}

TEST(GenerateRbacPoliciesTest, IncorrectSourceType) {
  const std::string authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\","
      "      \"source\": 111"
      "    }"
      "  ]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_FALSE(rbac_policies.ok());
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(), "\"source\" is not an object.");
}

TEST(GenerateRbacPoliciesTest, IncorrectPrincipalsType) {
  const std::string authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\","
      "      \"source\": {"
      "        \"principals\": ["
      "          \"*\","
      "          123"
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_FALSE(rbac_policies.ok());
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "\"principals\" 1 is not a string.");
}

TEST(GenerateRbacPoliciesTest, ParseSourceSuccess) {
  const std::string authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\","
      "      \"source\": {"
      "        \"principals\": ["
      "          \"spiffe://foo.abc\","
      "          \"spiffe://bar*\","
      "          \"*baz\","
      "          \"spiffe://*.com\""
      "        ]"
      "      }"
      "    }"
      "  ],"
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"deny_policy\","
      "      \"source\": {"
      "        \"principals\": ["
      "          \"*\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";

  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  ASSERT_TRUE(rbac_policies.ok());
  {
    auto allow_policy = std::move(rbac_policies.value()->allow_policy);
    EXPECT_EQ(allow_policy.action, Rbac::Action::ALLOW);
    EXPECT_EQ(allow_policy.policies.size(), 1);
    auto principals =
        std::move(allow_policy.policies["authz_allow_policy"].principals);
    EXPECT_EQ(principals.type, Rbac::Principal::RuleType::AND);
    EXPECT_EQ(principals.principals.size(), 1);
    auto principal_names = principals.principals[0].get();
    EXPECT_EQ(principal_names->type, Rbac::Principal::RuleType::OR);
    EXPECT_EQ(principal_names->principals.size(), 4);
    EXPECT_EQ(principal_names->principals[0]->type,
              Rbac::Principal::RuleType::PRINCIPAL_NAME);
    EXPECT_EQ(principal_names->principals[0]->string_matcher.type(),
              StringMatcher::Type::EXACT);
    EXPECT_EQ(principal_names->principals[0]->string_matcher.string_matcher(),
              "spiffe://foo.abc");
    EXPECT_EQ(principal_names->principals[1]->type,
              Rbac::Principal::RuleType::PRINCIPAL_NAME);
    EXPECT_EQ(principal_names->principals[1]->string_matcher.type(),
              StringMatcher::Type::PREFIX);
    EXPECT_EQ(principal_names->principals[1]->string_matcher.string_matcher(),
              "spiffe://bar");
    EXPECT_EQ(principal_names->principals[2]->type,
              Rbac::Principal::RuleType::PRINCIPAL_NAME);
    EXPECT_EQ(principal_names->principals[2]->string_matcher.type(),
              StringMatcher::Type::SUFFIX);
    EXPECT_EQ(principal_names->principals[2]->string_matcher.string_matcher(),
              "baz");
    EXPECT_EQ(principal_names->principals[3]->type,
              Rbac::Principal::RuleType::PRINCIPAL_NAME);
    EXPECT_EQ(principal_names->principals[3]->string_matcher.type(),
              StringMatcher::Type::EXACT);
    EXPECT_EQ(principal_names->principals[3]->string_matcher.string_matcher(),
              "spiffe://*.com");
  }
  {
    auto deny_policy = std::move(rbac_policies.value()->deny_policy);
    EXPECT_EQ(deny_policy.action, Rbac::Action::DENY);
    EXPECT_EQ(deny_policy.policies.size(), 1);
    auto principals =
        std::move(deny_policy.policies["authz_deny_policy"].principals);
    EXPECT_EQ(principals.type, Rbac::Principal::RuleType::AND);
    EXPECT_EQ(principals.principals.size(), 1);
    auto principal_names = principals.principals[0].get();
    EXPECT_EQ(principal_names->type, Rbac::Principal::RuleType::OR);
    EXPECT_EQ(principal_names->principals.size(), 1);
    EXPECT_EQ(principal_names->principals[0]->type,
              Rbac::Principal::RuleType::PRINCIPAL_NAME);
    EXPECT_EQ(principal_names->principals[0]->string_matcher.type(),
              StringMatcher::Type::SAFE_REGEX);
    EXPECT_EQ(principal_names->principals[0]
                  ->string_matcher.regex_matcher()
                  ->pattern(),
              ".*");
  }
}

TEST(GenerateRbacPoliciesTest, IncorrectRequestType) {
  const std::string authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"deny_policy\","
      "      \"request\": 111"
      "    }"
      "  ]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_FALSE(rbac_policies.ok());
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(), "\"request\" is not an object.");
}

TEST(GenerateRbacPoliciesTest, IncorrectPathType) {
  const std::string authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"path-a\","
      "          123"
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_FALSE(rbac_policies.ok());
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(), "\"paths\" 1 is not a string.");
}

TEST(GenerateRbacPoliciesTest, ParseRequestPathsSuccess) {
  const std::string authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*\""
      "        ]"
      "      }"
      "    }"
      "  ],"
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"deny_policy\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"path-foo\","
      "          \"path-bar*\","
      "          \"*baz\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";

  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  ASSERT_TRUE(rbac_policies.ok());
  {
    auto allow_policy = std::move(rbac_policies.value()->allow_policy);
    EXPECT_EQ(allow_policy.action, Rbac::Action::ALLOW);
    EXPECT_EQ(allow_policy.policies.size(), 1);
    auto permissions =
        std::move(allow_policy.policies["authz_allow_policy"].permissions);
    EXPECT_EQ(permissions.type, Rbac::Permission::RuleType::AND);
    EXPECT_EQ(permissions.permissions.size(), 1);
    auto paths = permissions.permissions[0].get();
    EXPECT_EQ(paths->type, Rbac::Permission::RuleType::OR);
    EXPECT_EQ(paths->permissions.size(), 1);
    EXPECT_EQ(paths->permissions[0]->type, Rbac::Permission::RuleType::PATH);
    EXPECT_EQ(paths->permissions[0]->string_matcher.type(),
              StringMatcher::Type::SAFE_REGEX);
    EXPECT_EQ(paths->permissions[0]->string_matcher.regex_matcher()->pattern(),
              ".*");
  }
  {
    auto deny_policy = std::move(rbac_policies.value()->deny_policy);
    EXPECT_EQ(deny_policy.action, Rbac::Action::DENY);
    EXPECT_EQ(deny_policy.policies.size(), 1);
    auto permissions =
        std::move(deny_policy.policies["authz_deny_policy"].permissions);
    EXPECT_EQ(permissions.type, Rbac::Permission::RuleType::AND);
    EXPECT_EQ(permissions.permissions.size(), 1);
    auto paths = permissions.permissions[0].get();
    EXPECT_EQ(paths->type, Rbac::Permission::RuleType::OR);
    EXPECT_EQ(paths->permissions.size(), 3);
    EXPECT_EQ(paths->permissions[0]->type, Rbac::Permission::RuleType::PATH);
    EXPECT_EQ(paths->permissions[0]->string_matcher.type(),
              StringMatcher::Type::EXACT);
    EXPECT_EQ(paths->permissions[0]->string_matcher.string_matcher(),
              "path-foo");
    EXPECT_EQ(paths->permissions[1]->type, Rbac::Permission::RuleType::PATH);
    EXPECT_EQ(paths->permissions[1]->string_matcher.type(),
              StringMatcher::Type::PREFIX);
    EXPECT_EQ(paths->permissions[1]->string_matcher.string_matcher(),
              "path-bar");
    EXPECT_EQ(paths->permissions[2]->type, Rbac::Permission::RuleType::PATH);
    EXPECT_EQ(paths->permissions[2]->string_matcher.type(),
              StringMatcher::Type::SUFFIX);
    EXPECT_EQ(paths->permissions[2]->string_matcher.string_matcher(), "baz");
  }
}

TEST(GenerateRbacPoliciesTest, IncorrectHeaderType) {
  const std::string authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\","
      "      \"request\": {"
      "        \"headers\": ["
      "          \"header-a\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_FALSE(rbac_policies.ok());
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "\"headers\" 0 is not an object.");
}

TEST(GenerateRbacPoliciesTest, UnsupportedGrpcHeaders) {
  const std::string authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"policy\","
      "      \"request\": {"
      "        \"headers\": ["
      "          {"
      "            \"key\": \"grpc-xxx\","
      "            \"values\": ["
      "              \"*\""
      "            ]"
      "          }"
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";

  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_FALSE(rbac_policies.ok());
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "Unsupported header \"key\" grpc-xxx.");
}

TEST(GenerateRbacPoliciesTest, UnsupportedPseudoHeaders) {
  const std::string authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"policy\","
      "      \"request\": {"
      "        \"headers\": ["
      "          {"
      "            \"key\": \":method\","
      "            \"values\": ["
      "              \"*\""
      "            ]"
      "          }"
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";

  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_FALSE(rbac_policies.ok());
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "Unsupported header \"key\" :method.");
}

TEST(GenerateRbacPoliciesTest, EmptyHeaderValuesList) {
  const std::string authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy_1\","
      "      \"request\": {"
      "        \"headers\": ["
      "          {"
      "            \"key\": \"key-a\","
      "            \"values\": ["
      "            ]"
      "          }"
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_FALSE(rbac_policies.ok());
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "header \"values\" list is empty.");
}

TEST(GenerateRbacPoliciesTest, ParseRequestHeadersSuccess) {
  const std::string authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\","
      "      \"request\": {"
      "        \"headers\": ["
      "          {"
      "            \"key\": \"key-1\","
      "            \"values\": ["
      "              \"*\""
      "            ]"
      "          },"
      "          {"
      "            \"key\": \"key-2\","
      "            \"values\": ["
      "              \"foo\","
      "              \"bar*\","
      "              \"*baz\""
      "            ]"
      "          }"
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";

  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  ASSERT_TRUE(rbac_policies.ok());

  auto allow_policy = std::move(rbac_policies.value()->allow_policy);
  EXPECT_EQ(allow_policy.action, Rbac::Action::ALLOW);
  EXPECT_EQ(allow_policy.policies.size(), 1);

  auto permissions =
      std::move(allow_policy.policies["authz_allow_policy"].permissions);
  EXPECT_EQ(permissions.type, Rbac::Permission::RuleType::AND);
  EXPECT_EQ(permissions.permissions.size(), 1);

  auto headers = permissions.permissions[0].get();
  EXPECT_EQ(headers->type, Rbac::Permission::RuleType::AND);
  EXPECT_EQ(headers->permissions.size(), 2);

  auto header1 = headers->permissions[0].get();
  EXPECT_EQ(header1->type, Rbac::Permission::RuleType::OR);
  EXPECT_EQ(header1->permissions.size(), 1);
  EXPECT_EQ(header1->permissions[0]->type, Rbac::Permission::RuleType::HEADER);
  EXPECT_EQ(header1->permissions[0]->header_matcher.type(),
            HeaderMatcher::Type::SAFE_REGEX);
  EXPECT_EQ(header1->permissions[0]->header_matcher.name(), "key-1");
  EXPECT_EQ(header1->permissions[0]->header_matcher.regex_matcher()->pattern(),
            ".*");

  auto header2 = headers->permissions[1].get();
  EXPECT_EQ(header2->type, Rbac::Permission::RuleType::OR);
  EXPECT_EQ(header2->permissions.size(), 3);
  EXPECT_EQ(header2->permissions[0]->type, Rbac::Permission::RuleType::HEADER);
  EXPECT_EQ(header2->permissions[0]->header_matcher.type(),
            HeaderMatcher::Type::EXACT);
  EXPECT_EQ(header2->permissions[0]->header_matcher.name(), "key-2");
  EXPECT_EQ(header2->permissions[0]->header_matcher.string_matcher(), "foo");
  EXPECT_EQ(header2->permissions[1]->type, Rbac::Permission::RuleType::HEADER);
  EXPECT_EQ(header2->permissions[1]->header_matcher.type(),
            HeaderMatcher::Type::PREFIX);
  EXPECT_EQ(header2->permissions[1]->header_matcher.name(), "key-2");
  EXPECT_EQ(header2->permissions[1]->header_matcher.string_matcher(), "bar");
  EXPECT_EQ(header2->permissions[2]->type, Rbac::Permission::RuleType::HEADER);
  EXPECT_EQ(header2->permissions[2]->header_matcher.type(),
            HeaderMatcher::Type::SUFFIX);
  EXPECT_EQ(header2->permissions[2]->header_matcher.name(), "key-2");
  EXPECT_EQ(header2->permissions[2]->header_matcher.string_matcher(), "baz");
}

TEST(GenerateRbacPoliciesTest, ParseRulesArraySuccess) {
  const std::string authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy_1\","
      "      \"source\": {"
      "        \"principals\": ["
      "          \"spiffe://foo.abc\""
      "        ]"
      "      },"
      "      \"request\": {"
      "        \"paths\": ["
      "          \"path-abc\""
      "        ]"
      "      }"
      "    },"
      "    {"
      "      \"name\": \"allow_policy_2\""
      "    }"
      "  ]"
      "}";

  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  ASSERT_TRUE(rbac_policies.ok());

  auto allow_policy = std::move(rbac_policies.value()->allow_policy);
  EXPECT_EQ(allow_policy.action, Rbac::Action::ALLOW);

  auto permissions1 =
      std::move(allow_policy.policies["authz_allow_policy_1"].permissions);
  EXPECT_EQ(permissions1.type, Rbac::Permission::RuleType::AND);
  EXPECT_EQ(permissions1.permissions.size(), 1);
  auto paths = permissions1.permissions[0].get();
  EXPECT_EQ(paths->type, Rbac::Permission::RuleType::OR);
  EXPECT_EQ(paths->permissions.size(), 1);
  EXPECT_EQ(paths->permissions[0]->type, Rbac::Permission::RuleType::PATH);
  EXPECT_EQ(paths->permissions[0]->string_matcher.type(),
            StringMatcher::Type::EXACT);
  EXPECT_EQ(paths->permissions[0]->string_matcher.string_matcher(), "path-abc");

  auto principals1 =
      std::move(allow_policy.policies["authz_allow_policy_1"].principals);
  EXPECT_EQ(principals1.type, Rbac::Principal::RuleType::AND);
  EXPECT_EQ(principals1.principals.size(), 1);
  auto principal_names = principals1.principals[0].get();
  EXPECT_EQ(principal_names->type, Rbac::Principal::RuleType::OR);
  EXPECT_EQ(principal_names->principals.size(), 1);
  EXPECT_EQ(principal_names->principals[0]->type,
            Rbac::Principal::RuleType::PRINCIPAL_NAME);
  EXPECT_EQ(principal_names->principals[0]->string_matcher.type(),
            StringMatcher::Type::EXACT);
  EXPECT_EQ(principal_names->principals[0]->string_matcher.string_matcher(),
            "spiffe://foo.abc");

  auto permissions2 =
      std::move(allow_policy.policies["authz_allow_policy_2"].permissions);
  EXPECT_EQ(permissions2.type, Rbac::Permission::RuleType::ANY);

  auto principals2 =
      std::move(allow_policy.policies["authz_allow_policy_2"].principals);
  EXPECT_EQ(principals2.type, Rbac::Principal::RuleType::ANY);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
