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
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/cpp/common/tls_credentials_options.cc"

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

typedef class ::grpc_impl::experimental::TlsKeyMaterialsConfig
    TlsKeyMaterialsConfig;

TEST_F(CredentialsTest, TlsKeyMaterialsConfigCppToC) {
  TlsKeyMaterialsConfig config;
  struct TlsKeyMaterialsConfig::PemKeyCertPair pair = {"private_key",
                                                       "cert_chain"};
  ::std::vector<TlsKeyMaterialsConfig::PemKeyCertPair> pair_list = {pair};
  config.set_key_materials("pem_root_certs", pair_list);
  grpc_tls_key_materials_config* c_config = config.c_key_materials();
  EXPECT_STREQ("pem_root_certs", c_config->pem_root_certs());
  EXPECT_EQ(1, static_cast<int>(c_config->pem_key_cert_pair_list().size()));
  EXPECT_STREQ(pair.private_key.c_str(),
               c_config->pem_key_cert_pair_list()[0].private_key());
  EXPECT_STREQ(pair.cert_chain.c_str(),
               c_config->pem_key_cert_pair_list()[0].cert_chain());
}

typedef class ::grpc_impl::experimental::TlsCredentialReloadArg
    TlsCredentialReloadArg;
typedef class ::grpc_impl::experimental::TlsCredentialReloadConfig
    TlsCredentialReloadConfig;

TEST_F(CredentialsTest, TlsCredentialReloadArgCppToC) {
  TlsCredentialReloadArg arg;
  // Only sync credential reload supported currently,
  // so we use a nullptr call back function.
  arg.set_cb(nullptr);
  arg.set_cb_user_data(nullptr);
  arg.set_key_materials_config(nullptr);
  arg.set_status(GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW);
  arg.set_error_details("error_details");
  grpc_tls_credential_reload_arg* c_arg = arg.c_credential_reload_arg();
  EXPECT_NE(c_arg, nullptr);
  EXPECT_EQ(c_arg->cb, nullptr);
  EXPECT_EQ(c_arg->cb_user_data, nullptr);
  EXPECT_EQ(c_arg->key_materials_config, nullptr);
  EXPECT_EQ(c_arg->status, GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW);
  EXPECT_STREQ(c_arg->error_details, "error_details");
}

TEST_F(CredentialsTest, TlsCredentialReloadConfigCppToC) {
  TlsCredentialReloadConfig config =
      TlsCredentialReloadConfig(nullptr, nullptr, nullptr, nullptr);
  grpc_tls_credential_reload_config* c_config = config.c_credential_reload();
  EXPECT_NE(c_config, nullptr);
  // TODO: add tests to compare schedule, cancel, destruct fields.
}

typedef class ::grpc_impl::experimental::TlsServerAuthorizationCheckArg
    TlsServerAuthorizationCheckArg;
typedef class ::grpc_impl::experimental::TlsServerAuthorizationCheckConfig
    TlsServerAuthorizationCheckConfig;

TEST_F(CredentialsTest, TlsServerAuthorizationCheckArgCppToC) {
  TlsServerAuthorizationCheckArg arg;
  // Only sync server authorization check supported currently,
  // so we use a nullptr call back function.
  arg.set_cb(nullptr);
  arg.set_cb_user_data(nullptr);
  arg.set_success(1);
  arg.set_peer_cert("peer_cert");
  arg.set_status(GRPC_STATUS_OK);
  arg.set_error_details("error_details");
  grpc_tls_server_authorization_check_arg* c_arg =
      arg.c_server_authorization_check_arg();
  EXPECT_NE(c_arg, nullptr);
  EXPECT_EQ(c_arg->cb, nullptr);
  EXPECT_EQ(c_arg->cb_user_data, nullptr);
  EXPECT_EQ(c_arg->success, 1);
  EXPECT_STREQ(c_arg->peer_cert, "peer_cert");
  EXPECT_EQ(c_arg->status, GRPC_STATUS_OK);
  EXPECT_STREQ(c_arg->error_details, "error_details");
}

TEST_F(CredentialsTest, TlsServerAuthorizationCheckCppToC) {
  TlsServerAuthorizationCheckConfig config =
      TlsServerAuthorizationCheckConfig(nullptr, nullptr, nullptr, nullptr);
  grpc_tls_server_authorization_check_config* c_config =
      config.c_server_authorization_check();
  EXPECT_NE(c_config, nullptr);
  // TODO: add tests to compare schedule, cancel, destruct fields.
}

typedef class ::grpc_impl::experimental::TlsCredentialsOptions
    TlsCredentialsOptions;

TEST_F(CredentialsTest, TlsCredentialsOptionsCppToC) {
  TlsCredentialsOptions options;
  options.set_cert_request_type(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY);
  ::std::shared_ptr<TlsKeyMaterialsConfig> key_materials_config(
      new TlsKeyMaterialsConfig());
  struct TlsKeyMaterialsConfig::PemKeyCertPair pair = {"private_key",
                                                       "cert_chain"};
  ::std::vector<TlsKeyMaterialsConfig::PemKeyCertPair> pair_list = {pair};
  key_materials_config->set_key_materials("pem_root_certs", pair_list);
  options.set_key_materials_config(key_materials_config);
  // TODO: add instances of credential reload and server authorization check to
  // options.
  grpc_tls_credentials_options* c_options = options.c_credentials_options();
  EXPECT_EQ(c_options->cert_request_type(),
            GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY);
  EXPECT_EQ(c_options->key_materials_config(),
            key_materials_config->c_key_materials());
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
