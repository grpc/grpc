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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace grpc_core {

namespace {

MATCHER_P2(EqualsPrincipalName, expected_matcher_type, expected_matcher_value,
           "") {
  return arg->type == Rbac::Principal::RuleType::kPrincipalName &&
         arg->string_matcher.type() == expected_matcher_type &&
         arg->string_matcher.string_matcher() == expected_matcher_value;
}

MATCHER_P2(EqualsPath, expected_matcher_type, expected_matcher_value, "") {
  return arg->type == Rbac::Permission::RuleType::kPath &&
         arg->string_matcher.type() == expected_matcher_type &&
         arg->string_matcher.string_matcher() == expected_matcher_value;
}

MATCHER_P3(EqualsHeader, expected_name, expected_matcher_type,
           expected_matcher_value, "") {
  return arg->type == Rbac::Permission::RuleType::kHeader &&
         arg->header_matcher.name() == expected_name &&
         arg->header_matcher.type() == expected_matcher_type &&
         arg->header_matcher.string_matcher() == expected_matcher_value;
}

}  // namespace

TEST(GenerateRbacPoliciesTest, InvalidPolicy) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz-policy\",,"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(rbac_policies.status().message()),
      ::testing::StartsWith("Failed to parse SDK authorization policy."));
}

TEST(GenerateRbacPoliciesTest, MissingAuthorizationPolicyName) {
  const char* authz_policy = "{}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(), "\"name\" field is not present.");
}

TEST(GenerateRbacPoliciesTest, IncorrectAuthorizationPolicyNameType) {
  const char* authz_policy =
      "{"
      "  \"name\": [\"authz_policy\"]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(), "\"name\" is not a string.");
}

TEST(GenerateRbacPoliciesTest, MissingAllowRules) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz_policy\""
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "\"allow_rules\" is not present.");
}

TEST(GenerateRbacPoliciesTest, MissingDenyRules) {
  const char* authz_policy =
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
  EXPECT_EQ(rbac_policies.value().deny_policy.action, Rbac::Action::kDeny);
  EXPECT_TRUE(rbac_policies.value().deny_policy.policies.empty());
}

TEST(GenerateRbacPoliciesTest, IncorrectAllowRulesType) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": {}"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "\"allow_rules\" is not an array.");
}

TEST(GenerateRbacPoliciesTest, IncorrectDenyRulesType) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"deny_rules\": 123"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "\"deny_rules\" is not an array.");
}

TEST(GenerateRbacPoliciesTest, IncorrectRuleType) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": [\"rule-a\"]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "allow_rules 0: is not an object.");
}

TEST(GenerateRbacPoliciesTest, MissingRuleNameField) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": [{}]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "allow_rules 0: \"name\" is not present.");
}

TEST(GenerateRbacPoliciesTest, IncorrectRuleNameType) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": 123"
      "    }"
      "  ]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "allow_rules 0: \"name\" is not a string.");
}

TEST(GenerateRbacPoliciesTest, MissingSourceAndRequest) {
  const char* authz_policy =
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
  EXPECT_EQ(rbac_policies.value().allow_policy.action, Rbac::Action::kAllow);
  EXPECT_THAT(
      rbac_policies.value().allow_policy.policies,
      ::testing::ElementsAre(::testing::Pair(
          "authz_allow_policy",
          ::testing::AllOf(
              ::testing::Field(
                  &Rbac::Policy::permissions,
                  ::testing::Field(&Rbac::Permission::type,
                                   Rbac::Permission::RuleType::kAny)),
              ::testing::Field(
                  &Rbac::Policy::principals,
                  ::testing::Field(&Rbac::Principal::type,
                                   Rbac::Principal::RuleType::kAny))))));
}

TEST(GenerateRbacPoliciesTest, EmptySourceAndRequest) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\","
      "      \"source\": {},"
      "      \"request\": {}"
      "    }"
      "  ]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  ASSERT_TRUE(rbac_policies.ok());
  EXPECT_EQ(rbac_policies.value().allow_policy.action, Rbac::Action::kAllow);
  EXPECT_THAT(
      rbac_policies.value().allow_policy.policies,
      ::testing::ElementsAre(::testing::Pair(
          "authz_allow_policy",
          ::testing::AllOf(
              ::testing::Field(
                  &Rbac::Policy::permissions,
                  ::testing::Field(&Rbac::Permission::type,
                                   Rbac::Permission::RuleType::kAny)),
              ::testing::Field(
                  &Rbac::Policy::principals,
                  ::testing::Field(&Rbac::Principal::type,
                                   Rbac::Principal::RuleType::kAny))))));
}

TEST(GenerateRbacPoliciesTest, IncorrectSourceType) {
  const char* authz_policy =
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
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "allow_rules 0: \"source\" is not an object.");
}

TEST(GenerateRbacPoliciesTest, IncorrectPrincipalsType) {
  const char* authz_policy =
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
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "allow_rules 0: \"principals\" 1: is not a string.");
}

TEST(GenerateRbacPoliciesTest, ParseSourceSuccess) {
  const char* authz_policy =
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
      "          \"spiffe://abc.*.com\""
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
  EXPECT_EQ(rbac_policies.value().allow_policy.action, Rbac::Action::kAllow);
  EXPECT_THAT(
      rbac_policies.value().allow_policy.policies,
      ::testing::ElementsAre(::testing::Pair(
          "authz_allow_policy",
          ::testing::AllOf(
              ::testing::Field(
                  &Rbac::Policy::permissions,
                  ::testing::Field(&Rbac::Permission::type,
                                   Rbac::Permission::RuleType::kAny)),
              ::testing::Field(
                  &Rbac::Policy::principals,
                  ::testing::AllOf(
                      ::testing::Field(&Rbac::Principal::type,
                                       Rbac::Principal::RuleType::kAnd),
                      ::testing::Field(
                          &Rbac::Principal::principals,
                          ::testing::ElementsAre(::testing::AllOf(
                              ::testing::Pointee(::testing::Field(
                                  &Rbac::Principal::type,
                                  Rbac::Principal::RuleType::kOr)),
                              ::testing::Pointee(::testing::Field(
                                  &Rbac::Principal::principals,
                                  ::testing::ElementsAre(
                                      EqualsPrincipalName(
                                          StringMatcher::Type::kExact,
                                          "spiffe://foo.abc"),
                                      EqualsPrincipalName(
                                          StringMatcher::Type::kPrefix,
                                          "spiffe://bar"),
                                      EqualsPrincipalName(
                                          StringMatcher::Type::kSuffix, "baz"),
                                      EqualsPrincipalName(
                                          StringMatcher::Type::kExact,
                                          "spiffe://abc.*.com")))))))))))));
  EXPECT_EQ(rbac_policies.value().deny_policy.action, Rbac::Action::kDeny);
  EXPECT_THAT(
      rbac_policies.value().deny_policy.policies,
      ::testing::ElementsAre(::testing::Pair(
          "authz_deny_policy",
          ::testing::AllOf(
              ::testing::Field(
                  &Rbac::Policy::permissions,
                  ::testing::Field(&Rbac::Permission::type,
                                   Rbac::Permission::RuleType::kAny)),
              ::testing::Field(
                  &Rbac::Policy::principals,
                  ::testing::AllOf(
                      ::testing::Field(&Rbac::Principal::type,
                                       Rbac::Principal::RuleType::kAnd),
                      ::testing::Field(
                          &Rbac::Principal::principals,
                          ::testing::ElementsAre(::testing::AllOf(
                              ::testing::Pointee(::testing::Field(
                                  &Rbac::Principal::type,
                                  Rbac::Principal::RuleType::kOr)),
                              ::testing::Pointee(::testing::Field(
                                  &Rbac::Principal::principals,
                                  ::testing::ElementsAre(EqualsPrincipalName(
                                      StringMatcher::Type::kPrefix,
                                      "")))))))))))));
}

TEST(GenerateRbacPoliciesTest, IncorrectRequestType) {
  const char* authz_policy =
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
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "deny_rules 0: \"request\" is not an object.");
}

TEST(GenerateRbacPoliciesTest, IncorrectPathType) {
  const char* authz_policy =
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
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "deny_rules 0: \"paths\" 1: is not a string.");
}

TEST(GenerateRbacPoliciesTest, ParseRequestPathsSuccess) {
  const char* authz_policy =
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
  EXPECT_EQ(rbac_policies.value().deny_policy.action, Rbac::Action::kDeny);
  EXPECT_THAT(
      rbac_policies.value().deny_policy.policies,
      ::testing::ElementsAre(::testing::Pair(
          "authz_deny_policy",
          ::testing::AllOf(
              ::testing::Field(
                  &Rbac::Policy::principals,
                  ::testing::Field(&Rbac::Principal::type,
                                   Rbac::Principal::RuleType::kAny)),
              ::testing::Field(
                  &Rbac::Policy::permissions,
                  ::testing::AllOf(
                      ::testing::Field(&Rbac::Permission::type,
                                       Rbac::Permission::RuleType::kAnd),
                      ::testing::Field(
                          &Rbac::Permission::permissions,
                          ::testing::ElementsAre(::testing::AllOf(
                              ::testing::Pointee(::testing::Field(
                                  &Rbac::Permission::type,
                                  Rbac::Permission::RuleType::kOr)),
                              ::testing::Pointee(::testing::Field(
                                  &Rbac::Permission::permissions,
                                  ::testing::ElementsAre(
                                      EqualsPath(StringMatcher::Type::kExact,
                                                 "path-foo"),
                                      EqualsPath(StringMatcher::Type::kPrefix,
                                                 "path-bar"),
                                      EqualsPath(StringMatcher::Type::kSuffix,
                                                 "baz")))))))))))));
  EXPECT_EQ(rbac_policies.value().allow_policy.action, Rbac::Action::kAllow);
  EXPECT_THAT(
      rbac_policies.value().allow_policy.policies,
      ::testing::ElementsAre(::testing::Pair(
          "authz_allow_policy",
          ::testing::AllOf(
              ::testing::Field(
                  &Rbac::Policy::principals,
                  ::testing::Field(&Rbac::Principal::type,
                                   Rbac::Principal::RuleType::kAny)),
              ::testing::Field(
                  &Rbac::Policy::permissions,
                  ::testing::AllOf(
                      ::testing::Field(&Rbac::Permission::type,
                                       Rbac::Permission::RuleType::kAnd),
                      ::testing::Field(
                          &Rbac::Permission::permissions,
                          ::testing::ElementsAre(::testing::AllOf(
                              ::testing::Pointee(::testing::Field(
                                  &Rbac::Permission::type,
                                  Rbac::Permission::RuleType::kOr)),
                              ::testing::Pointee(::testing::Field(
                                  &Rbac::Permission::permissions,
                                  ::testing::ElementsAre(
                                      EqualsPath(StringMatcher::Type::kPrefix,
                                                 "")))))))))))));
}

TEST(GenerateRbacPoliciesTest, IncorrectHeaderType) {
  const char* authz_policy =
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
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "deny_rules 0: \"headers\" 0: is not an object.");
}

TEST(GenerateRbacPoliciesTest, UnsupportedGrpcHeaders) {
  const char* authz_policy =
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
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "deny_rules 0: \"headers\" 0: Unsupported \"key\" grpc-xxx.");
}

TEST(GenerateRbacPoliciesTest, UnsupportedPseudoHeaders) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
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
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "allow_rules 0: \"headers\" 0: Unsupported \"key\" :method.");
}

TEST(GenerateRbacPoliciesTest, UnsupportedhostHeader) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"policy\","
      "      \"request\": {"
      "        \"headers\": ["
      "          {"
      "            \"key\": \"host\","
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
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "deny_rules 0: \"headers\" 0: Unsupported \"key\" host.");
}

TEST(GenerateRbacPoliciesTest, UnsupportedHostHeader) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"policy\","
      "      \"request\": {"
      "        \"headers\": ["
      "          {"
      "            \"key\": \"Host\","
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
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "allow_rules 0: \"headers\" 0: Unsupported \"key\" Host.");
}

TEST(GenerateRbacPoliciesTest, EmptyHeaderValuesList) {
  const char* authz_policy =
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
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "allow_rules 0: \"headers\" 0: \"values\" list is empty.");
}

TEST(GenerateRbacPoliciesTest, ParseRequestHeadersSuccess) {
  const char* authz_policy =
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
  EXPECT_EQ(rbac_policies.value().deny_policy.action, Rbac::Action::kDeny);
  EXPECT_TRUE(rbac_policies.value().deny_policy.policies.empty());
  EXPECT_EQ(rbac_policies.value().allow_policy.action, Rbac::Action::kAllow);
  EXPECT_THAT(
      rbac_policies.value().allow_policy.policies,
      ::testing::ElementsAre(::testing::Pair(
          "authz_allow_policy",
          ::testing::AllOf(
              ::testing::Field(
                  &Rbac::Policy::principals,
                  ::testing::Field(&Rbac::Principal::type,
                                   Rbac::Principal::RuleType::kAny)),
              ::testing::Field(
                  &Rbac::Policy::permissions,
                  ::testing::AllOf(
                      ::testing::Field(&Rbac::Permission::type,
                                       Rbac::Permission::RuleType::kAnd),
                      ::testing::Field(
                          &Rbac::Permission::permissions,
                          ::testing::ElementsAre(::testing::AllOf(
                              ::testing::Pointee(::testing::Field(
                                  &Rbac::Permission::type,
                                  Rbac::Permission::RuleType::kAnd)),
                              ::testing::Pointee(::testing::Field(
                                  &Rbac::Permission::permissions,
                                  ::testing::ElementsAre(
                                      ::testing::AllOf(
                                          ::testing::Pointee(::testing::Field(
                                              &Rbac::Permission::type,
                                              Rbac::Permission::RuleType::kOr)),
                                          ::testing::Pointee(::testing::Field(
                                              &Rbac::Permission::permissions,
                                              ::testing::ElementsAre(
                                                  EqualsHeader(
                                                      "key-1",
                                                      HeaderMatcher::Type::
                                                          kPrefix,
                                                      ""))))),
                                      ::testing::AllOf(
                                          ::testing::Pointee(::testing::Field(
                                              &Rbac::Permission::type,
                                              Rbac::Permission::RuleType::kOr)),
                                          ::testing::Pointee(::testing::Field(
                                              &Rbac::Permission::permissions,
                                              ::testing::ElementsAre(
                                                  EqualsHeader("key-2",
                                                               HeaderMatcher::
                                                                   Type::kExact,
                                                               "foo"),
                                                  EqualsHeader(
                                                      "key-2",
                                                      HeaderMatcher::Type::
                                                          kPrefix,
                                                      "bar"),
                                                  EqualsHeader(
                                                      "key-2",
                                                      HeaderMatcher::Type::
                                                          kSuffix,
                                                      "baz")))))))))))))))));
}

TEST(GenerateRbacPoliciesTest, ParseRulesArraySuccess) {
  const char* authz_policy =
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
      "          \"foo\""
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
  EXPECT_EQ(rbac_policies.value().deny_policy.action, Rbac::Action::kDeny);
  EXPECT_TRUE(rbac_policies.value().deny_policy.policies.empty());
  EXPECT_EQ(rbac_policies.value().allow_policy.action, Rbac::Action::kAllow);
  EXPECT_THAT(
      rbac_policies.value().allow_policy.policies,
      ::testing::ElementsAre(
          ::testing::Pair(
              "authz_allow_policy_1",
              ::testing::AllOf(
                  ::testing::Field(
                      &Rbac::Policy::permissions,
                      ::testing::AllOf(
                          ::testing::Field(&Rbac::Permission::type,
                                           Rbac::Permission::RuleType::kAnd),
                          ::testing::Field(
                              &Rbac::Permission::permissions,
                              ::testing::ElementsAre(::testing::AllOf(
                                  ::testing::Pointee(::testing::Field(
                                      &Rbac::Permission::type,
                                      Rbac::Permission::RuleType::kOr)),
                                  ::testing::Pointee(::testing::Field(
                                      &Rbac::Permission::permissions,
                                      ::testing::ElementsAre(EqualsPath(
                                          StringMatcher::Type::kExact,
                                          "foo"))))))))),
                  ::testing::Field(
                      &Rbac::Policy::principals,
                      ::testing::AllOf(
                          ::testing::Field(&Rbac::Principal::type,
                                           Rbac::Principal::RuleType::kAnd),
                          ::testing::Field(
                              &Rbac::Principal::principals,
                              ::testing::ElementsAre(::testing::AllOf(
                                  ::testing::Pointee(::testing::Field(
                                      &Rbac::Principal::type,
                                      Rbac::Principal::RuleType::kOr)),
                                  ::testing::Pointee(::testing::Field(
                                      &Rbac::Principal::principals,
                                      ::testing::ElementsAre(
                                          EqualsPrincipalName(
                                              StringMatcher::Type::kExact,
                                              "spiffe://foo.abc"))))))))))),
          ::testing::Pair(
              "authz_allow_policy_2",
              ::testing::AllOf(
                  ::testing::Field(
                      &Rbac::Policy::permissions,
                      ::testing::Field(&Rbac::Permission::type,
                                       Rbac::Permission::RuleType::kAny)),
                  ::testing::Field(
                      &Rbac::Policy::principals,
                      ::testing::Field(&Rbac::Principal::type,
                                       Rbac::Principal::RuleType::kAny))))));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
