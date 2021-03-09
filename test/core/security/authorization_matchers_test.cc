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

#include "src/core/lib/security/authorization/evaluate_args.h"
#include "src/core/lib/security/authorization/matchers.h"
#include "test/core/util/mock_eval_args_endpoint.h"

namespace grpc_core {

namespace {

void AddPairToMetadataBatch(grpc_metadata_batch* batch,
                            grpc_linked_mdelem* storage, const char* key,
                            const char* value) {
  ASSERT_EQ(grpc_metadata_batch_add_tail(
                batch, storage,
                grpc_mdelem_from_slices(
                    grpc_slice_intern(grpc_slice_from_static_string(key)),
                    grpc_slice_intern(grpc_slice_from_static_string(value)))),
            GRPC_ERROR_NONE);
}

}  // namespace

TEST(AuthorizationMatchersTest, AlwaysMatcher) {
  EvaluateArgs args(/*metadata=*/nullptr, /*auth_context=*/nullptr,
                    /*endpoint=*/nullptr);
  AlwaysMatcher matcher;
  EXPECT_TRUE(matcher.Matches(args));
}

TEST(AuthorizationMatchersTest, NotAlwaysMatcher) {
  EvaluateArgs args(/*metadata=*/nullptr, /*auth_context=*/nullptr,
                    /*endpoint=*/nullptr);
  AlwaysMatcher matcher(/*not_rule=*/true);
  EXPECT_FALSE(matcher.Matches(args));
}

TEST(AuthorizationMatchersTest, AndMatcherSuccessfulMatch) {
  grpc_init();
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_linked_mdelem storage;
  AddPairToMetadataBatch(&metadata, &storage, /*key=*/"foo",
                         /*value=*/"bar");
  MockEvalArgsEndpoint endpoint(/*local_uri=*/"ipv4:255.255.255.255:123",
                                /*peer_uri=*/"");
  EvaluateArgs args(&metadata, /*auth_context=*/nullptr, &endpoint);
  std::vector<std::unique_ptr<Rbac::Permission>> rules;
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::HEADER,
      HeaderMatcher::Create(/*name=*/"foo", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"bar")
          .value()));
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::DEST_PORT, /*port=*/123));
  AndMatcher matcher(rules);
  EXPECT_TRUE(matcher.Matches(args));
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(AuthorizationMatchersTest, AndMatcherFailsMatch) {
  grpc_init();
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_linked_mdelem storage;
  AddPairToMetadataBatch(&metadata, &storage, /*key=*/"foo",
                         /*value=*/"not_bar");
  MockEvalArgsEndpoint endpoint(/*local_uri=*/"ipv4:255.255.255.255:123",
                                /*peer_uri=*/"");
  EvaluateArgs args(&metadata, /*auth_context=*/nullptr, &endpoint);
  std::vector<std::unique_ptr<Rbac::Permission>> rules;
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::HEADER,
      HeaderMatcher::Create(/*name=*/"foo", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"bar")
          .value()));
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::DEST_PORT, /*port=*/123));
  AndMatcher matcher(rules);
  // Header rule fails. Expected value "bar", got "not_bar" for key "foo".
  EXPECT_FALSE(matcher.Matches(args));
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(AuthorizationMatchersTest, NotAndMatcher) {
  grpc_init();
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_linked_mdelem storage;
  AddPairToMetadataBatch(&metadata, &storage, /*key=*/":path",
                         /*value=*/"/expected/foo");
  EvaluateArgs args(&metadata, /*auth_context=*/nullptr, /*endpoint=*/nullptr);
  StringMatcher string_matcher =
      StringMatcher::Create(StringMatcher::Type::EXACT,
                            /*matcher=*/"/expected/foo",
                            /*case_sensitive=*/false)
          .value();
  std::vector<std::unique_ptr<Rbac::Permission>> ids;
  ids.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::PATH, string_matcher));
  AndMatcher matcher(ids, /*not_rule=*/true);
  EXPECT_FALSE(matcher.Matches(args));
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(AuthorizationMatchersTest, OrMatcherSuccessfulMatch) {
  grpc_init();
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_linked_mdelem storage;
  AddPairToMetadataBatch(&metadata, &storage, "foo", "bar");
  MockEvalArgsEndpoint endpoint("ipv4:255.255.255.255:123", "");
  EvaluateArgs args(&metadata, /*auth_context=*/nullptr, &endpoint);
  HeaderMatcher header_matcher =
      HeaderMatcher::Create(/*name=*/"foo", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"bar")
          .value();
  std::vector<std::unique_ptr<Rbac::Permission>> rules;
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::HEADER, header_matcher));
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::DEST_PORT, /*port=*/456));
  OrMatcher matcher(rules);
  EXPECT_TRUE(matcher.Matches(args));
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(AuthorizationMatchersTest, OrMatcherFailsMatch) {
  grpc_init();
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_linked_mdelem storage;
  AddPairToMetadataBatch(&metadata, &storage, /*key=*/"foo",
                         /*value=*/"not_bar");
  EvaluateArgs args(&metadata, /*auth_context=*/nullptr, /*endpoint=*/nullptr);
  std::vector<std::unique_ptr<Rbac::Permission>> rules;
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::HEADER,
      HeaderMatcher::Create(/*name=*/"foo", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"bar")
          .value()));
  OrMatcher matcher(rules);
  EXPECT_FALSE(matcher.Matches(args));
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(AuthorizationMatchersTest, NotOrMatcher) {
  grpc_init();
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_linked_mdelem storage;
  AddPairToMetadataBatch(&metadata, &storage, /*key=*/"foo",
                         /*value=*/"not_bar");
  EvaluateArgs args(&metadata, /*auth_context=*/nullptr, /*endpoint=*/nullptr);
  std::vector<std::unique_ptr<Rbac::Permission>> rules;
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::HEADER,
      HeaderMatcher::Create(/*name=*/"foo", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"bar")
          .value()));
  OrMatcher matcher(rules, /*not_rule=*/true);
  EXPECT_TRUE(matcher.Matches(args));
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(AuthorizationMatchersTest, PathMatcherSuccessfulMatch) {
  grpc_init();
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_linked_mdelem storage;
  AddPairToMetadataBatch(&metadata, &storage, /*key=*/":path",
                         /*value=*/"expected/path");
  EvaluateArgs args(&metadata, /*auth_context=*/nullptr, /*endpoint=*/nullptr);
  PathMatcher matcher(StringMatcher::Create(StringMatcher::Type::EXACT,
                                            /*matcher=*/"expected/path",
                                            /*case_sensitive=*/false)
                          .value());
  EXPECT_TRUE(matcher.Matches(args));
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(AuthorizationMatchersTest, PathMatcherFailsMatch) {
  grpc_init();
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_linked_mdelem storage;
  AddPairToMetadataBatch(&metadata, &storage, /*key=*/":path",
                         /*value=*/"different/path");
  EvaluateArgs args(&metadata, /*auth_context=*/nullptr, /*endpoint=*/nullptr);
  PathMatcher matcher(StringMatcher::Create(StringMatcher::Type::EXACT,
                                            /*matcher=*/"expected/path",
                                            /*case_sensitive=*/false)
                          .value());
  EXPECT_FALSE(matcher.Matches(args));
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(AuthorizationMatchersTest, NotPathMatcher) {
  grpc_init();
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_linked_mdelem storage;
  AddPairToMetadataBatch(&metadata, &storage, /*key=*/":path",
                         /*value=*/"expected/path");
  EvaluateArgs args(&metadata, /*auth_context=*/nullptr, /*endpoint=*/nullptr);
  PathMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::EXACT, "expected/path", false)
          .value(),
      /*not_rule=*/true);
  EXPECT_FALSE(matcher.Matches(args));
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(AuthorizationMatchersTest, PathMatcherFailsMatchMissingPath) {
  grpc_init();
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  EvaluateArgs args(&metadata, /*auth_context=*/nullptr, /*endpoint=*/nullptr);
  PathMatcher matcher(StringMatcher::Create(StringMatcher::Type::EXACT,
                                            /*matcher=*/"expected/path",
                                            /*case_sensitive=*/false)
                          .value());
  EXPECT_FALSE(matcher.Matches(args));
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(AuthorizationMatchersTest, HttpHeaderMatcherSuccessfulMatch) {
  grpc_init();
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_linked_mdelem storage;
  AddPairToMetadataBatch(&metadata, &storage, /*key=*/"key123",
                         /*value=*/"foo_xxx");
  EvaluateArgs args(&metadata, /*auth_context=*/nullptr, /*endpoint=*/nullptr);
  HttpHeaderMatcher matcher(HeaderMatcher::Create(/*name=*/"key123",
                                                  HeaderMatcher::Type::PREFIX,
                                                  /*matcher=*/"foo")
                                .value());
  EXPECT_TRUE(matcher.Matches(args));
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(AuthorizationMatchersTest, HttpHeaderMatcherFailsMatch) {
  grpc_init();
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_linked_mdelem storage;
  AddPairToMetadataBatch(&metadata, &storage, /*key=*/"key123",
                         /*value=*/"foo");
  EvaluateArgs args(&metadata, /*auth_context=*/nullptr, /*endpoint=*/nullptr);
  HttpHeaderMatcher matcher(HeaderMatcher::Create(/*name=*/"key123",
                                                  HeaderMatcher::Type::EXACT,
                                                  /*matcher=*/"bar")
                                .value());
  EXPECT_FALSE(matcher.Matches(args));
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(AuthorizationMatchersTest, HttpHeaderMatcherFailsMatchMultivaluedHeader) {
  grpc_init();
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_linked_mdelem storage1;
  AddPairToMetadataBatch(&metadata, &storage1, /*key=*/"key123",
                         /*value=*/"foo");
  grpc_linked_mdelem storage2;
  AddPairToMetadataBatch(&metadata, &storage2, /*key=*/"key123",
                         /*value=*/"bar");
  EvaluateArgs args(&metadata, /*auth_context=*/nullptr, /*endpoint=*/nullptr);
  HttpHeaderMatcher matcher(HeaderMatcher::Create(/*name=*/"key123",
                                                  HeaderMatcher::Type::EXACT,
                                                  /*matcher=*/"foo")
                                .value());
  EXPECT_FALSE(matcher.Matches(args));
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(AuthorizationMatchersTest, HttpHeaderMatcherFailsMatchMissingHeader) {
  grpc_init();
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  EvaluateArgs args(&metadata, /*auth_context=*/nullptr, /*endpoint=*/nullptr);
  HttpHeaderMatcher matcher(HeaderMatcher::Create(/*name=*/"key123",
                                                  HeaderMatcher::Type::SUFFIX,
                                                  /*matcher=*/"foo")
                                .value());
  EXPECT_FALSE(matcher.Matches(args));
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(AuthorizationMatchersTest, NotHttpHeaderMatcher) {
  grpc_init();
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_linked_mdelem storage;
  AddPairToMetadataBatch(&metadata, &storage, /*key=*/"key123",
                         /*value=*/"foo");
  EvaluateArgs args(&metadata, /*auth_context=*/nullptr, /*endpoint=*/nullptr);
  HttpHeaderMatcher matcher(
      HeaderMatcher::Create(/*name=*/"key123", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"bar")
          .value(),
      /*not_rule=*/true);
  EXPECT_TRUE(matcher.Matches(args));
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(AuthorizationMatchersTest, PortMatcherSuccessfulMatch) {
  MockEvalArgsEndpoint endpoint(/*local_uri=*/"ipv4:255.255.255.255:123",
                                /*peer_uri=*/"");
  EvaluateArgs args(/*metadata=*/nullptr, /*auth_context=*/nullptr, &endpoint);
  PortMatcher matcher(/*port=*/123);
  EXPECT_TRUE(matcher.Matches(args));
}

TEST(AuthorizationMatchersTest, PortMatcherFailsMatch) {
  MockEvalArgsEndpoint endpoint(/*local_uri=*/"ipv4:255.255.255.255:123",
                                /*peer_uri=*/"");
  EvaluateArgs args(/*metadata=*/nullptr, /*auth_context=*/nullptr, &endpoint);
  PortMatcher matcher(/*port=*/456);
  EXPECT_FALSE(matcher.Matches(args));
}

TEST(AuthorizationMatchersTest, NotPortMatcher) {
  MockEvalArgsEndpoint endpoint(/*local_uri=*/"ipv4:255.255.255.255:123",
                                /*peer_uri=*/"");
  EvaluateArgs args(/*metadata=*/nullptr, /*auth_context=*/nullptr, &endpoint);
  PortMatcher matcher(/*port=*/123, /*not_rule=*/true);
  EXPECT_FALSE(matcher.Matches(args));
}

TEST(AuthorizationMatchersTest, AuthenticatedMatcherUnAuthenticatedConnection) {
  grpc_auth_context auth_context(nullptr);
  EvaluateArgs args(/*metadata=*/nullptr, &auth_context, /*endpoint=*/nullptr);
  AuthenticatedMatcher matcher(StringMatcher::Create(StringMatcher::Type::EXACT,
                                                     /*matcher=*/"foo.com",
                                                     /*case_sensitive=*/false)
                                   .value());
  EXPECT_FALSE(matcher.Matches(args));
}

TEST(AuthorizationMatchersTest,
     AuthenticatedMatcherAuthenticatedConnectionMatcherUnset) {
  grpc_auth_context auth_context(nullptr);
  EvaluateArgs args(/*metadata=*/nullptr, &auth_context, /*endpoint=*/nullptr);
  auth_context.add_cstring_property(
      /*name=*/GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
      /*value=*/GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  AuthenticatedMatcher matcher(StringMatcher::Create(StringMatcher::Type::EXACT,
                                                     /*matcher=*/"",
                                                     /*case_sensitive=*/false)
                                   .value());
  EXPECT_TRUE(matcher.Matches(args));
}

TEST(AuthorizationMatchersTest, AuthenticatedMatcherSuccessfulSpiffeIdMatches) {
  grpc_auth_context auth_context(nullptr);
  EvaluateArgs args(/*metadata=*/nullptr, &auth_context, /*endpoint=*/nullptr);
  auth_context.add_cstring_property(
      /*name=*/GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
      /*value=*/GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  auth_context.add_cstring_property(/*name=*/GRPC_PEER_SPIFFE_ID_PROPERTY_NAME,
                                    /*value=*/"spiffe://foo.abc");
  AuthenticatedMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::EXACT,
                            /*matcher=*/"spiffe://foo.abc",
                            /*case_sensitive=*/false)
          .value());
  EXPECT_TRUE(matcher.Matches(args));
}

TEST(AuthorizationMatchersTest, AuthenticatedMatcherFailsSpiffeIdMatches) {
  grpc_auth_context auth_context(nullptr);
  EvaluateArgs args(/*metadata=*/nullptr, &auth_context, /*endpoint=*/nullptr);
  auth_context.add_cstring_property(
      /*name=*/GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
      /*value=*/GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  auth_context.add_cstring_property(/*name=*/GRPC_PEER_SPIFFE_ID_PROPERTY_NAME,
                                    /*value=*/"spiffe://bar.abc");
  AuthenticatedMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::EXACT,
                            /*matcher=*/"spiffe://foo.abc",
                            /*case_sensitive=*/false)
          .value());
  EXPECT_FALSE(matcher.Matches(args));
}

TEST(AuthorizationMatchersTest, AuthenticatedMatcherFailsNothingMatches) {
  grpc_auth_context auth_context(nullptr);
  EvaluateArgs args(/*metadata=*/nullptr, &auth_context, /*endpoint=*/nullptr);
  auth_context.add_cstring_property(
      /*name=*/GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
      /*value=*/GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  AuthenticatedMatcher matcher(StringMatcher::Create(StringMatcher::Type::EXACT,
                                                     /*matcher=*/"foo",
                                                     /*case_sensitive=*/false)
                                   .value());
  EXPECT_FALSE(matcher.Matches(args));
}

TEST(AuthorizationMatchersTest, NotAuthenticatedMatcher) {
  grpc_auth_context auth_context(nullptr);
  EvaluateArgs args(/*metadata=*/nullptr, &auth_context, /*endpoint=*/nullptr);
  auth_context.add_cstring_property(
      /*name=*/GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
      /*value=*/GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  AuthenticatedMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::EXACT, /*matcher=*/"foo",
                            /*case_sensitive=*/false)
          .value(),
      /*not_rule=*/true);
  EXPECT_TRUE(matcher.Matches(args));
}

TEST(AuthorizationMatchersTest, PolicyMatcherSuccessfulMatch) {
  grpc_init();
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_linked_mdelem storage;
  AddPairToMetadataBatch(&metadata, &storage, /*key=*/"key123",
                         /*value=*/"foo");
  EvaluateArgs args(&metadata, /*auth_context=*/nullptr, /*endpoint=*/nullptr);
  HeaderMatcher header_matcher =
      HeaderMatcher::Create(/*name=*/"key123", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"foo")
          .value();
  std::vector<std::unique_ptr<Rbac::Permission>> rules;
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::HEADER, header_matcher));
  PolicyMatcher matcher(Rbac::Policy(
      Rbac::Permission(Rbac::Permission::RuleType::OR, std::move(rules)),
      Rbac::Principal(Rbac::Principal::RuleType::ANY)));
  EXPECT_TRUE(matcher.Matches(args));
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(AuthorizationMatchersTest, PolicyMatcherFailsMatch) {
  grpc_init();
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_linked_mdelem storage;
  AddPairToMetadataBatch(&metadata, &storage, /*key=*/"key123",
                         /*value=*/"foo");
  EvaluateArgs args(&metadata, /*auth_context=*/nullptr, /*endpoint=*/nullptr);
  HeaderMatcher header_matcher =
      HeaderMatcher::Create(/*name=*/"key123", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"bar")
          .value();
  std::vector<std::unique_ptr<Rbac::Permission>> rules;
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::HEADER, header_matcher));
  PolicyMatcher matcher(Rbac::Policy(
      Rbac::Permission(Rbac::Permission::RuleType::OR, std::move(rules)),
      Rbac::Principal(Rbac::Principal::RuleType::ANY)));
  EXPECT_FALSE(matcher.Matches(args));
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
