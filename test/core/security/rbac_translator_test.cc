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

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc_audit_logging.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_writer.h"
#include "src/core/lib/security/authorization/audit_logging.h"
#include "test/core/util/test_config.h"

namespace grpc_core {

namespace {

constexpr absl::string_view kLoggerName = "test_logger";

using experimental::AuditLogger;
using experimental::AuditLoggerFactory;
using experimental::AuditLoggerRegistry;
using experimental::RegisterAuditLoggerFactory;

MATCHER_P3(EqualsPrincipalName, expected_matcher_type, expected_matcher_value,
           is_regex, "") {
  return arg->type == Rbac::Principal::RuleType::kPrincipalName &&
                 arg->string_matcher.value().type() == expected_matcher_type &&
                 is_regex
             ? arg->string_matcher.value().regex_matcher()->pattern() ==
                   expected_matcher_value
             : arg->string_matcher.value().string_matcher() ==
                   expected_matcher_value;
}

MATCHER_P3(EqualsPath, expected_matcher_type, expected_matcher_value, is_regex,
           "") {
  return arg->type == Rbac::Permission::RuleType::kPath &&
                 arg->string_matcher.type() == expected_matcher_type && is_regex
             ? arg->string_matcher.regex_matcher()->pattern() ==
                   expected_matcher_value
             : arg->string_matcher.string_matcher() == expected_matcher_value;
}

MATCHER_P4(EqualsHeader, expected_name, expected_matcher_type,
           expected_matcher_value, is_regex, "") {
  return arg->type == Rbac::Permission::RuleType::kHeader &&
                 arg->header_matcher.name() == expected_name &&
                 arg->header_matcher.type() == expected_matcher_type && is_regex
             ? arg->header_matcher.regex_matcher()->pattern() ==
                   expected_matcher_value
             : arg->header_matcher.string_matcher() == expected_matcher_value;
}

class TestAuditLoggerFactory : public AuditLoggerFactory {
 public:
  class TestAuditLoggerConfig : public AuditLoggerFactory::Config {
   public:
    explicit TestAuditLoggerConfig(std::string config_dump)
        : config_dump_(std::move(config_dump)) {}
    absl::string_view name() const override { return kLoggerName; }
    std::string ToString() const override { return config_dump_; }

   private:
    std::string config_dump_;
  };
  absl::string_view name() const override { return kLoggerName; }
  absl::StatusOr<std::unique_ptr<AuditLoggerFactory::Config>>
  ParseAuditLoggerConfig(const Json& json) override {
    // Config with a field "bad" will be considered invalid.
    if (json.object().find("bad") != json.object().end()) {
      return absl::InvalidArgumentError("bad logger config.");
    }
    return std::make_unique<TestAuditLoggerConfig>(JsonDump(json));
  }
  std::unique_ptr<AuditLogger> CreateAuditLogger(
      std::unique_ptr<AuditLoggerFactory::Config>) override {
    // This test target should never need to create a logger.
    Crash("unreachable");
    return nullptr;
  }
};

class GenerateRbacPoliciesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    RegisterAuditLoggerFactory(std::make_unique<TestAuditLoggerFactory>());
  }

  void TearDown() override { AuditLoggerRegistry::TestOnlyResetRegistry(); }
};

}  // namespace

TEST_F(GenerateRbacPoliciesTest, InvalidPolicy) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz-policy\",,"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(rbac_policies.status().message()),
      ::testing::StartsWith("Failed to parse gRPC authorization policy."));
}

TEST_F(GenerateRbacPoliciesTest, MissingAuthorizationPolicyName) {
  const char* authz_policy = "{}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(), "\"name\" field is not present.");
}

TEST_F(GenerateRbacPoliciesTest, IncorrectAuthorizationPolicyNameType) {
  const char* authz_policy =
      "{"
      "  \"name\": [\"authz_policy\"]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(), "\"name\" is not a string.");
}

TEST_F(GenerateRbacPoliciesTest, MissingAllowRules) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz_policy\""
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "\"allow_rules\" is not present.");
}

TEST_F(GenerateRbacPoliciesTest, MissingDenyRules) {
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
  EXPECT_EQ(rbac_policies->allow_policy.name, "authz");
  EXPECT_FALSE(rbac_policies->deny_policy.has_value());
}

TEST_F(GenerateRbacPoliciesTest, IncorrectAllowRulesType) {
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

TEST_F(GenerateRbacPoliciesTest, IncorrectDenyRulesType) {
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

TEST_F(GenerateRbacPoliciesTest, IncorrectRuleType) {
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

TEST_F(GenerateRbacPoliciesTest, EmptyRuleArray) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": []"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(), "allow_rules is empty.");
}

TEST_F(GenerateRbacPoliciesTest, MissingRuleNameField) {
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

TEST_F(GenerateRbacPoliciesTest, IncorrectRuleNameType) {
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

TEST_F(GenerateRbacPoliciesTest, MissingSourceAndRequest) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ]"
      "}";
  auto rbacs = GenerateRbacPolicies(authz_policy);
  ASSERT_TRUE(rbacs.ok());
  EXPECT_EQ(rbacs->allow_policy.name, "authz");
  EXPECT_EQ(rbacs->allow_policy.action, Rbac::Action::kAllow);
  EXPECT_THAT(
      rbacs->allow_policy.policies,
      ::testing::ElementsAre(::testing::Pair(
          "allow_policy",
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

TEST_F(GenerateRbacPoliciesTest, EmptySourceAndRequest) {
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
  auto rbacs = GenerateRbacPolicies(authz_policy);
  ASSERT_TRUE(rbacs.ok());
  EXPECT_EQ(rbacs->allow_policy.name, "authz");
  EXPECT_EQ(rbacs->allow_policy.action, Rbac::Action::kAllow);
  EXPECT_THAT(
      rbacs->allow_policy.policies,
      ::testing::ElementsAre(::testing::Pair(
          "allow_policy",
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

TEST_F(GenerateRbacPoliciesTest, IncorrectSourceType) {
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

TEST_F(GenerateRbacPoliciesTest, IncorrectPrincipalsType) {
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

TEST_F(GenerateRbacPoliciesTest, ParseSourceSuccess) {
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
  auto rbacs = GenerateRbacPolicies(authz_policy);
  ASSERT_TRUE(rbacs.ok());
  EXPECT_EQ(rbacs->allow_policy.name, "authz");
  EXPECT_EQ(rbacs->deny_policy->name, "authz");
  EXPECT_EQ(rbacs->allow_policy.action, Rbac::Action::kAllow);
  EXPECT_THAT(rbacs->allow_policy.policies,
              ::testing::ElementsAre(::testing::Pair(
                  "allow_policy",
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
                                                  "spiffe://foo.abc", false),
                                              EqualsPrincipalName(
                                                  StringMatcher::Type::kPrefix,
                                                  "spiffe://bar", false),
                                              EqualsPrincipalName(
                                                  StringMatcher::Type::kSuffix,
                                                  "baz", false),
                                              EqualsPrincipalName(
                                                  StringMatcher::Type::kExact,
                                                  "spiffe://abc.*.com",
                                                  false)))))))))))));
  ASSERT_TRUE(rbacs->deny_policy.has_value());
  EXPECT_EQ(rbacs->deny_policy->action, Rbac::Action::kDeny);
  EXPECT_THAT(
      rbacs->deny_policy->policies,
      ::testing::ElementsAre(::testing::Pair(
          "deny_policy",
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
                                      StringMatcher::Type::kSafeRegex, ".+",
                                      true)))))))))))));
}

TEST_F(GenerateRbacPoliciesTest, IncorrectRequestType) {
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

TEST_F(GenerateRbacPoliciesTest, IncorrectPathType) {
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

TEST_F(GenerateRbacPoliciesTest, ParseRequestPathsSuccess) {
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
  auto rbacs = GenerateRbacPolicies(authz_policy);
  ASSERT_TRUE(rbacs.ok());
  EXPECT_EQ(rbacs->allow_policy.name, "authz");
  EXPECT_EQ(rbacs->deny_policy->name, "authz");
  ASSERT_TRUE(rbacs->deny_policy.has_value());
  EXPECT_EQ(rbacs->deny_policy->action, Rbac::Action::kDeny);
  EXPECT_THAT(
      rbacs->deny_policy->policies,
      ::testing::ElementsAre(::testing::Pair(
          "deny_policy",
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
                                                 "path-foo", false),
                                      EqualsPath(StringMatcher::Type::kPrefix,
                                                 "path-bar", false),
                                      EqualsPath(StringMatcher::Type::kSuffix,
                                                 "baz", false)))))))))))));
  EXPECT_EQ(rbacs->allow_policy.action, Rbac::Action::kAllow);
  EXPECT_THAT(
      rbacs->allow_policy.policies,
      ::testing::ElementsAre(::testing::Pair(
          "allow_policy",
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
                                  ::testing::ElementsAre(EqualsPath(
                                      StringMatcher::Type::kSafeRegex, ".+",
                                      true)))))))))))));
}

TEST_F(GenerateRbacPoliciesTest, IncorrectHeaderType) {
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

TEST_F(GenerateRbacPoliciesTest, MissingHeaderKey) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"policy\","
      "      \"request\": {"
      "        \"headers\": ["
      "          {}"
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "allow_rules 0: \"headers\" 0: \"key\" is not present.");
}

TEST_F(GenerateRbacPoliciesTest, MissingHeaderValues) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"policy\","
      "      \"request\": {"
      "        \"headers\": ["
      "          {"
      "            \"key\": \"key-abc\""
      "          }"
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "allow_rules 0: \"headers\" 0: \"values\" is not present.");
}

TEST_F(GenerateRbacPoliciesTest, IncorrectHeaderKeyType) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"policy\","
      "      \"request\": {"
      "        \"headers\": ["
      "          {"
      "            \"key\": 123,"
      "            \"values\": [\"value-a\"]"
      "          }"
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "allow_rules 0: \"headers\" 0: \"key\" is not a string.");
}

TEST_F(GenerateRbacPoliciesTest, IncorrectHeaderValuesType) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"policy\","
      "      \"request\": {"
      "        \"headers\": ["
      "          {"
      "            \"key\": \"key-abc\","
      "            \"values\": {}"
      "          }"
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(rbac_policies.status().message(),
            "allow_rules 0: \"headers\" 0: \"values\" is not an array.");
}

TEST_F(GenerateRbacPoliciesTest, UnsupportedGrpcHeaders) {
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

TEST_F(GenerateRbacPoliciesTest, UnsupportedPseudoHeaders) {
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

TEST_F(GenerateRbacPoliciesTest, UnsupportedHostHeader) {
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

TEST_F(GenerateRbacPoliciesTest, EmptyHeaderValuesList) {
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

TEST_F(GenerateRbacPoliciesTest, ParseRequestHeadersSuccess) {
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
  auto rbacs = GenerateRbacPolicies(authz_policy);
  ASSERT_TRUE(rbacs.ok());
  EXPECT_EQ(rbacs->allow_policy.name, "authz");
  EXPECT_EQ(rbacs->allow_policy.action, Rbac::Action::kAllow);
  EXPECT_THAT(
      rbacs->allow_policy.policies,
      ::testing::ElementsAre(::testing::Pair(
          "allow_policy",
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
                                                          kSafeRegex,
                                                      ".+", true))))),
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
                                                               "foo", false),
                                                  EqualsHeader(
                                                      "key-2",
                                                      HeaderMatcher::Type::
                                                          kPrefix,
                                                      "bar", false),
                                                  EqualsHeader(
                                                      "key-2",
                                                      HeaderMatcher::Type::
                                                          kSuffix,
                                                      "baz",
                                                      false)))))))))))))))));
}

TEST_F(GenerateRbacPoliciesTest, ParseRulesArraySuccess) {
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
  auto rbacs = GenerateRbacPolicies(authz_policy);
  ASSERT_TRUE(rbacs.ok());
  EXPECT_EQ(rbacs->allow_policy.name, "authz");
  EXPECT_EQ(rbacs->allow_policy.action, Rbac::Action::kAllow);
  EXPECT_THAT(
      rbacs->allow_policy.policies,
      ::testing::ElementsAre(
          ::testing::Pair(
              "allow_policy_1",
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
                                          StringMatcher::Type::kExact, "foo",
                                          false))))))))),
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
                                              "spiffe://foo.abc",
                                              false))))))))))),
          ::testing::Pair(
              "allow_policy_2",
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

TEST_F(GenerateRbacPoliciesTest, UnknownFieldInTopLayer) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ],"
      "  \"foo\": \"123\""
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(rbac_policies.status().message(),
              "policy contains unknown field \"foo\".");
}

TEST_F(GenerateRbacPoliciesTest, UnknownFieldInRule) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\","
      "      \"foo\": {}"
      "    }"
      "  ]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      rbac_policies.status().message(),
      "allow_rules 0: policy contains unknown field \"foo\" in \"rule\".");
}

TEST_F(GenerateRbacPoliciesTest, UnknownFieldInSource) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\","
      "      \"source\": "
      "      {"
      "        \"principals\": [\"spiffe://foo.abc\"],"
      "        \"foo\": {} "
      "      }"
      "    }"
      "  ]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      rbac_policies.status().message(),
      "allow_rules 0: policy contains unknown field \"foo\" in \"source\".");
}

TEST_F(GenerateRbacPoliciesTest, UnknownFieldInRequest) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\","
      "      \"request\": { \"foo\": {}}"
      "    }"
      "  ]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      rbac_policies.status().message(),
      "allow_rules 0: policy contains unknown field \"foo\" in \"request\".");
}

TEST_F(GenerateRbacPoliciesTest, UnknownFieldInHeaders) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\","
      "      \"request\": {"
      "        \"headers\": [{ \"foo\": {}}]"
      "      }"
      "    }"
      "  ]"
      "}";
  auto rbac_policies = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbac_policies.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(rbac_policies.status().message(),
              "allow_rules 0: \"headers\" 0: policy contains unknown field "
              "\"foo\".");
}

TEST_F(GenerateRbacPoliciesTest, EmptyAuditLoggingOptions) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ],"
      "  \"audit_logging_options\": {}"
      "}";
  auto rbacs = GenerateRbacPolicies(authz_policy);
  ASSERT_TRUE(rbacs.ok());
  EXPECT_EQ(rbacs->allow_policy.name, "authz");
}

TEST_F(GenerateRbacPoliciesTest, AuditConditionNone) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"audit_condition\": \"NONE\""
      "  }"
      "}";
  auto rbacs = GenerateRbacPolicies(authz_policy);
  ASSERT_TRUE(rbacs.ok());
  EXPECT_EQ(rbacs->allow_policy.name, "authz");
  EXPECT_EQ(rbacs->allow_policy.audit_condition, Rbac::AuditCondition::kNone);
  EXPECT_TRUE(
      absl::StartsWith(rbacs->allow_policy.ToString(),
                       "Rbac name=authz action=Allow audit_condition=None"));
}

TEST_F(GenerateRbacPoliciesTest, AuditConditionOnDeny) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ],"
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"deny_policy\""
      "    }"
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"audit_condition\": \"ON_DENY\""
      "  }"
      "}";
  auto rbacs = GenerateRbacPolicies(authz_policy);
  ASSERT_TRUE(rbacs.ok());
  EXPECT_EQ(rbacs->allow_policy.name, "authz");
  EXPECT_EQ(rbacs->deny_policy->name, "authz");
  EXPECT_EQ(rbacs->allow_policy.audit_condition, Rbac::AuditCondition::kOnDeny);
  EXPECT_EQ(rbacs->deny_policy->audit_condition, Rbac::AuditCondition::kOnDeny);
}

TEST_F(GenerateRbacPoliciesTest, AuditConditionOnAllowWithAuditLoggers) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ],"
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"deny_policy\""
      "    }"
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"audit_condition\": \"ON_ALLOW\","
      "    \"audit_loggers\": ["
      "      {"
      "        \"name\": \"test_logger\","
      "        \"config\": {"
      "          \"foo\": true"
      "        }"
      "      },"
      "      {"
      "        \"name\": \"test_logger\","
      "        \"config\": {"
      "          \"bar\": true"
      "        }"
      "      }"
      "    ]"
      "  }"
      "}";
  auto rbacs = GenerateRbacPolicies(authz_policy);
  ASSERT_TRUE(rbacs.ok());
  EXPECT_EQ(rbacs->allow_policy.name, "authz");
  EXPECT_EQ(rbacs->deny_policy->name, "authz");
  EXPECT_EQ(rbacs->allow_policy.audit_condition,
            Rbac::AuditCondition::kOnAllow);
  EXPECT_EQ(rbacs->deny_policy->audit_condition, Rbac::AuditCondition::kNone);
  ASSERT_EQ(rbacs->allow_policy.logger_configs.size(), 2);
  EXPECT_EQ(rbacs->deny_policy->logger_configs.size(), 0);
  EXPECT_EQ(rbacs->allow_policy.logger_configs.at(0)->name(), kLoggerName);
  EXPECT_EQ(rbacs->allow_policy.logger_configs.at(1)->name(), kLoggerName);
  EXPECT_EQ(rbacs->allow_policy.logger_configs.at(0)->ToString(),
            "{\"foo\":true}");
  EXPECT_EQ(rbacs->allow_policy.logger_configs.at(1)->ToString(),
            "{\"bar\":true}");
  EXPECT_EQ(rbacs->allow_policy.ToString(),
            "Rbac name=authz action=Allow audit_condition=OnAllow{\n{\n  "
            "policy_name=allow_policy\n  Policy  {\n    Permissions{any}\n    "
            "Principals{any}\n  }\n}\n{\n  "
            "audit_logger=test_logger\n{\"foo\":true}\n}\n{\n  "
            "audit_logger=test_logger\n{\"bar\":true}\n}\n}");
}

TEST_F(GenerateRbacPoliciesTest,
       AuditConditionOnDenyAndAllowWithUnsupportedButOptionalLogger) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ],"
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"deny_policy\""
      "    }"
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"audit_condition\": \"ON_DENY_AND_ALLOW\","
      "    \"audit_loggers\": ["
      "      {"
      "        \"name\": \"unknown_logger\","
      "        \"is_optional\": true,"
      "        \"config\": {}"
      "      }"
      "    ]"
      "  }"
      "}";
  auto rbacs = GenerateRbacPolicies(authz_policy);
  ASSERT_TRUE(rbacs.ok());
  EXPECT_EQ(rbacs->allow_policy.name, "authz");
  EXPECT_EQ(rbacs->deny_policy->name, "authz");
  EXPECT_EQ(rbacs->allow_policy.audit_condition,
            Rbac::AuditCondition::kOnDenyAndAllow);
  EXPECT_EQ(rbacs->deny_policy->audit_condition, Rbac::AuditCondition::kOnDeny);
  EXPECT_EQ(rbacs->allow_policy.logger_configs.size(), 0);
  EXPECT_EQ(rbacs->deny_policy->logger_configs.size(), 0);
}

TEST_F(GenerateRbacPoliciesTest, UnknownFieldInAuditLoggingOptions) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"foo\": 123"
      "  }"
      "}";
  auto rbacs = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbacs.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      rbacs.status().message(),
      "policy contains unknown field \"foo\" in \"audit_logging_options\".");
}

TEST_F(GenerateRbacPoliciesTest, AuditConditionIsNotString) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"audit_condition\": 123"
      "  }"
      "}";
  auto rbacs = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbacs.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(rbacs.status().message(), "\"audit_condition\" is not a string.");
}

TEST_F(GenerateRbacPoliciesTest, IncorrectAuditConditionValue) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"audit_condition\": \"UNKNOWN\""
      "  }"
      "}";
  auto rbacs = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbacs.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(rbacs.status().message(),
              "Unsupported \"audit_condition\" value UNKNOWN.");
}

TEST_F(GenerateRbacPoliciesTest, IncorrectAuditLoggersType) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"audit_loggers\": 123"
      "  }"
      "}";
  auto rbacs = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbacs.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(rbacs.status().message(), "\"audit_loggers\" is not an array.");
}

TEST_F(GenerateRbacPoliciesTest, IncorrectAuditLoggerType) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"audit_loggers\": [123]"
      "  }"
      "}";
  auto rbacs = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbacs.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(rbacs.status().message(),
              "\"audit_loggers[0]\" is not an object.");
}

TEST_F(GenerateRbacPoliciesTest, UnknownFieldInAuditLoggers) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"audit_loggers\": ["
      "      {"
      "        \"foo\": 123"
      "      }"
      "    ]"
      "  }"
      "}";
  auto rbacs = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbacs.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(rbacs.status().message(),
              "policy contains unknown field \"foo\" in "
              "\"audit_logging_options.audit_loggers[0]\".");
}

TEST_F(GenerateRbacPoliciesTest, IncorrectAuditLoggerConfigType) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"audit_loggers\": ["
      "      {"
      "        \"name\": \"unknown_logger\","
      "        \"config\": 123"
      "      }"
      "    ]"
      "  }"
      "}";
  auto rbacs = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbacs.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(rbacs.status().message(),
              "\"audit_loggers[0].config\" is not an object.");
}

TEST_F(GenerateRbacPoliciesTest, BadAuditLoggerConfig) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"audit_loggers\": ["
      "      {"
      "        \"name\": \"test_logger\","
      "        \"config\": {\"bad\": true}"
      "      }"
      "    ]"
      "  }"
      "}";
  auto rbacs = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbacs.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(rbacs.status().message(),
              "\"audit_loggers[0]\" bad logger config.");
}

TEST_F(GenerateRbacPoliciesTest, IncorrectAuditLoggerNameType) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"audit_loggers\": ["
      "      {"
      "        \"name\": 123,"
      "        \"config\": {}"
      "      }"
      "    ]"
      "  }"
      "}";
  auto rbacs = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbacs.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(rbacs.status().message(),
              "\"audit_loggers[0].name\" is not a string.");
}

TEST_F(GenerateRbacPoliciesTest, IncorrectAuditLoggerIsOptionalType) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"audit_loggers\": ["
      "      {"
      "        \"name\": \"test_logger\","
      "        \"is_optional\": 123,"
      "        \"config\": {}"
      "      }"
      "    ]"
      "  }"
      "}";
  auto rbacs = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbacs.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(rbacs.status().message(),
              "\"audit_loggers[0].is_optional\" is not a boolean.");
}

TEST_F(GenerateRbacPoliciesTest, MissingAuditLoggerName) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"audit_loggers\": ["
      "      {"
      "        \"config\": {}"
      "      }"
      "    ]"
      "  }"
      "}";
  auto rbacs = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbacs.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(rbacs.status().message(),
              "\"audit_loggers[0].name\" is required.");
}

TEST_F(GenerateRbacPoliciesTest, MissingAuditLoggerConfig) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"audit_condition\": \"ON_DENY\","
      "    \"audit_loggers\": ["
      "      {"
      "        \"name\": \"test_logger\""
      "      }"
      "    ]"
      "  }"
      "}";
  auto rbacs = GenerateRbacPolicies(authz_policy);
  ASSERT_TRUE(rbacs.ok());
  EXPECT_EQ(rbacs->allow_policy.name, "authz");
  EXPECT_EQ(rbacs->allow_policy.audit_condition, Rbac::AuditCondition::kOnDeny);
  EXPECT_EQ(rbacs->allow_policy.logger_configs.size(), 1);
  EXPECT_EQ(rbacs->allow_policy.logger_configs.at(0)->name(), kLoggerName);
  EXPECT_EQ(rbacs->allow_policy.logger_configs.at(0)->ToString(), "{}");
}

TEST_F(GenerateRbacPoliciesTest, UnsupportedAuditLogger) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_policy\""
      "    }"
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"audit_loggers\": ["
      "      {"
      "        \"name\": \"unknown_logger\","
      "        \"config\": {}"
      "      }"
      "    ]"
      "  }"
      "}";
  auto rbacs = GenerateRbacPolicies(authz_policy);
  EXPECT_EQ(rbacs.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(rbacs.status().message(),
              "\"audit_loggers[0].name\" unknown_logger is not supported "
              "natively or registered.");
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
