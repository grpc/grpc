/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpcpp/security/credentials.h>

#include <memory>

#include <gmock/gmock.h>
#include <grpc/grpc.h>
#include <gtest/gtest.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/cpp/client/secure_credentials.h"

namespace grpc {
namespace testing {

class CredentialsTest : public ::testing::Test {
 protected:
};

TEST_F(CredentialsTest, InvalidGoogleRefreshToken) {
  std::shared_ptr<CallCredentials> bad1 = GoogleRefreshTokenCredentials("");
  EXPECT_EQ(static_cast<CallCredentials*>(nullptr), bad1.get());
}

TEST_F(CredentialsTest, DefaultCredentials) {
  auto creds = GoogleDefaultCredentials();
}

TEST_F(CredentialsTest, StsCredentialsOptionsCppToCore) {
  grpc::experimental::StsCredentialsOptions options;
  options.token_exchange_service_uri = "https://foo.com/exchange";
  options.resource = "resource";
  options.audience = "audience";
  options.scope = "scope";
  // options.requested_token_type explicitly not set.
  options.subject_token_path = "/foo/bar";
  options.subject_token_type = "nice_token_type";
  options.actor_token_path = "/foo/baz";
  options.actor_token_type = "even_nicer_token_type";
  grpc_sts_credentials_options core_opts =
      grpc_impl::experimental::StsCredentialsCppToCoreOptions(options);
  EXPECT_EQ(options.token_exchange_service_uri,
            core_opts.token_exchange_service_uri);
  EXPECT_EQ(options.resource, core_opts.resource);
  EXPECT_EQ(options.audience, core_opts.audience);
  EXPECT_EQ(options.scope, core_opts.scope);
  EXPECT_EQ(options.requested_token_type, core_opts.requested_token_type);
  EXPECT_EQ(options.subject_token_path, core_opts.subject_token_path);
  EXPECT_EQ(options.subject_token_type, core_opts.subject_token_type);
  EXPECT_EQ(options.actor_token_path, core_opts.actor_token_path);
  EXPECT_EQ(options.actor_token_type, core_opts.actor_token_type);
}

TEST_F(CredentialsTest, StsCredentialsOptionsJson) {
  const char valid_json[] = R"(
  {
    "token_exchange_service_uri": "https://foo/exchange",
    "resource": "resource",
    "audience": "audience",
    "scope": "scope",
    "requested_token_type": "requested_token_type",
    "subject_token_path": "subject_token_path",
    "subject_token_type": "subject_token_type",
    "actor_token_path": "actor_token_path",
    "actor_token_type": "actor_token_type"
  })";
  grpc::experimental::StsCredentialsOptions options;
  EXPECT_TRUE(
      grpc::experimental::StsCredentialsOptionsFromJson(valid_json, &options)
          .ok());
  EXPECT_EQ(options.token_exchange_service_uri, "https://foo/exchange");
  EXPECT_EQ(options.resource, "resource");
  EXPECT_EQ(options.audience, "audience");
  EXPECT_EQ(options.scope, "scope");
  EXPECT_EQ(options.requested_token_type, "requested_token_type");
  EXPECT_EQ(options.subject_token_path, "subject_token_path");
  EXPECT_EQ(options.subject_token_type, "subject_token_type");
  EXPECT_EQ(options.actor_token_path, "actor_token_path");
  EXPECT_EQ(options.actor_token_type, "actor_token_type");

  const char minimum_valid_json[] = R"(
  {
    "token_exchange_service_uri": "https://foo/exchange",
    "subject_token_path": "subject_token_path",
    "subject_token_type": "subject_token_type"
  })";
  EXPECT_TRUE(grpc::experimental::StsCredentialsOptionsFromJson(
                  minimum_valid_json, &options)
                  .ok());
  EXPECT_EQ(options.token_exchange_service_uri, "https://foo/exchange");
  EXPECT_EQ(options.resource, "");
  EXPECT_EQ(options.audience, "");
  EXPECT_EQ(options.scope, "");
  EXPECT_EQ(options.requested_token_type, "");
  EXPECT_EQ(options.subject_token_path, "subject_token_path");
  EXPECT_EQ(options.subject_token_type, "subject_token_type");
  EXPECT_EQ(options.actor_token_path, "");
  EXPECT_EQ(options.actor_token_type, "");

  const char invalid_json[] = R"(
  I'm not a valid JSON.
  )";
  EXPECT_EQ(
      grpc::StatusCode::INVALID_ARGUMENT,
      grpc::experimental::StsCredentialsOptionsFromJson(invalid_json, &options)
          .error_code());

  const char invalid_json_missing_subject_token_type[] = R"(
  {
    "token_exchange_service_uri": "https://foo/exchange",
    "subject_token_path": "subject_token_path"
  })";
  auto status = grpc::experimental::StsCredentialsOptionsFromJson(
      invalid_json_missing_subject_token_type, &options);
  EXPECT_EQ(grpc::StatusCode::INVALID_ARGUMENT, status.error_code());
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("subject_token_type"));

  const char invalid_json_missing_subject_token_path[] = R"(
  {
    "token_exchange_service_uri": "https://foo/exchange",
    "subject_token_type": "subject_token_type"
  })";
  status = grpc::experimental::StsCredentialsOptionsFromJson(
      invalid_json_missing_subject_token_path, &options);
  EXPECT_EQ(grpc::StatusCode::INVALID_ARGUMENT, status.error_code());
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("subject_token_path"));

  const char invalid_json_missing_token_exchange_uri[] = R"(
  {
    "subject_token_path": "subject_token_path",
    "subject_token_type": "subject_token_type"
  })";
  status = grpc::experimental::StsCredentialsOptionsFromJson(
      invalid_json_missing_token_exchange_uri, &options);
  EXPECT_EQ(grpc::StatusCode::INVALID_ARGUMENT, status.error_code());
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("token_exchange_service_uri"));
}

TEST_F(CredentialsTest, StsCredentialsOptionsFromEnv) {
  // Unset env and check expected failure.
  gpr_unsetenv("STS_CREDENTIALS");
  grpc::experimental::StsCredentialsOptions options;
  auto status = grpc::experimental::StsCredentialsOptionsFromEnv(&options);
  EXPECT_EQ(grpc::StatusCode::NOT_FOUND, status.error_code());

  // Set env and check for success.
  const char valid_json[] = R"(
  {
    "token_exchange_service_uri": "https://foo/exchange",
    "subject_token_path": "subject_token_path",
    "subject_token_type": "subject_token_type"
  })";
  char* creds_file_name;
  FILE* creds_file = gpr_tmpfile("sts_creds_options", &creds_file_name);
  ASSERT_NE(creds_file_name, nullptr);
  ASSERT_NE(creds_file, nullptr);
  ASSERT_EQ(sizeof(valid_json),
            fwrite(valid_json, 1, sizeof(valid_json), creds_file));
  fclose(creds_file);
  gpr_setenv("STS_CREDENTIALS", creds_file_name);
  gpr_free(creds_file_name);
  status = grpc::experimental::StsCredentialsOptionsFromEnv(&options);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(options.token_exchange_service_uri, "https://foo/exchange");
  EXPECT_EQ(options.resource, "");
  EXPECT_EQ(options.audience, "");
  EXPECT_EQ(options.scope, "");
  EXPECT_EQ(options.requested_token_type, "");
  EXPECT_EQ(options.subject_token_path, "subject_token_path");
  EXPECT_EQ(options.subject_token_type, "subject_token_type");
  EXPECT_EQ(options.actor_token_path, "");
  EXPECT_EQ(options.actor_token_type, "");

  // Cleanup.
  gpr_unsetenv("STS_CREDENTIALS");
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
