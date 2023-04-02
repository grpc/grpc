//
//
// Copyright 2015 gRPC authors.
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
//
//

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <grpcpp/server_builder.h>

#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/env.h"
#include "src/cpp/client/secure_credentials.h"
#include "test/cpp/util/tls_test_utils.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"
#define CRL_DIR_PATH "test/core/tsi/test_creds/crl_data/crls"

namespace {

constexpr const char* kRootCertName = "root_cert_name";
constexpr const char* kRootCertContents = "root_cert_contents";
constexpr const char* kIdentityCertName = "identity_cert_name";
constexpr const char* kIdentityCertPrivateKey = "identity_private_key";
constexpr const char* kIdentityCertContents = "identity_cert_contents";

using ::grpc::experimental::ExternalCertificateVerifier;
using ::grpc::experimental::FileWatcherCertificateProvider;
using ::grpc::experimental::HostNameCertificateVerifier;
using ::grpc::experimental::StaticDataCertificateProvider;

}  // namespace

namespace grpc {
namespace testing {
namespace {

TEST(CredentialsTest, InvalidGoogleRefreshToken) {
  std::shared_ptr<CallCredentials> bad1 = GoogleRefreshTokenCredentials("");
  EXPECT_EQ(static_cast<CallCredentials*>(nullptr), bad1.get());
}

TEST(CredentialsTest, DefaultCredentials) {
  auto creds = GoogleDefaultCredentials();
}

TEST(CredentialsTest, ExternalAccountCredentials) {
  // url credentials
  std::string url_options_string(
      "{\"type\":\"external_account\",\"audience\":\"audience\",\"subject_"
      "token_type\":\"subject_token_type\",\"service_account_impersonation_"
      "url\":\"service_account_impersonation_url\",\"token_url\":\"https://"
      "foo.com:5555/token\",\"token_info_url\":\"https://foo.com:5555/"
      "token_info\",\"credential_source\":{\"url\":\"https://foo.com:5555/"
      "generate_subject_token_format_json\",\"headers\":{\"Metadata-Flavor\":"
      "\"Google\"},\"format\":{\"type\":\"json\",\"subject_token_field_name\":"
      "\"access_token\"}},\"quota_project_id\":\"quota_"
      "project_id\",\"client_id\":\"client_id\",\"client_secret\":\"client_"
      "secret\"}");
  auto url_creds = grpc::ExternalAccountCredentials(url_options_string,
                                                    {"scope1", "scope2"});
  EXPECT_TRUE(url_creds != nullptr);
  // file credentials
  std::string file_options_string(
      "{\"type\":\"external_account\",\"audience\":\"audience\",\"subject_"
      "token_type\":\"subject_token_type\",\"service_account_impersonation_"
      "url\":\"service_account_impersonation_url\",\"token_url\":\"https://"
      "foo.com:5555/token\",\"token_info_url\":\"https://foo.com:5555/"
      "token_info\",\"credential_source\":{\"file\":\"credentials_file_path\"},"
      "\"quota_project_id\":\"quota_"
      "project_id\",\"client_id\":\"client_id\",\"client_secret\":\"client_"
      "secret\"}");
  auto file_creds = grpc::ExternalAccountCredentials(file_options_string,
                                                     {"scope1", "scope2"});
  EXPECT_TRUE(file_creds != nullptr);
  // aws credentials
  std::string aws_options_string(
      "{\"type\":\"external_account\",\"audience\":\"audience\",\"subject_"
      "token_type\":\"subject_token_type\",\"service_account_impersonation_"
      "url\":\"service_account_impersonation_url\",\"token_url\":\"https://"
      "foo.com:5555/token\",\"token_info_url\":\"https://foo.com:5555/"
      "token_info\",\"credential_source\":{\"environment_id\":\"aws1\","
      "\"region_url\":\"https://169.254.169.254:5555/"
      "region_url\",\"url\":\"https://"
      "169.254.169.254:5555/url\",\"regional_cred_verification_url\":\"https://"
      "foo.com:5555/regional_cred_verification_url_{region}\"},"
      "\"quota_project_id\":\"quota_"
      "project_id\",\"client_id\":\"client_id\",\"client_secret\":\"client_"
      "secret\"}");
  auto aws_creds = grpc::ExternalAccountCredentials(aws_options_string,
                                                    {"scope1", "scope2"});
  EXPECT_TRUE(aws_creds != nullptr);
}

TEST(CredentialsTest, StsCredentialsOptionsCppToCore) {
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
      grpc::experimental::StsCredentialsCppToCoreOptions(options);
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

TEST(CredentialsTest, StsCredentialsOptionsJson) {
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

TEST(CredentialsTest, StsCredentialsOptionsFromEnv) {
  // Unset env and check expected failure.
  grpc_core::UnsetEnv("STS_CREDENTIALS");
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
  grpc_core::SetEnv("STS_CREDENTIALS", creds_file_name);
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
  grpc_core::UnsetEnv("STS_CREDENTIALS");
}

TEST(CredentialsTest, TlsChannelCredentialsWithDefaultRootsAndDefaultVerifier) {
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_verify_server_certs(true);
  auto channel_credentials = grpc::experimental::TlsCredentials(options);
  GPR_ASSERT(channel_credentials.get() != nullptr);
}

TEST(
    CredentialsTest,
    TlsChannelCredentialsWithStaticDataCertificateProviderLoadingRootAndIdentity) {
  experimental::IdentityKeyCertPair key_cert_pair;
  key_cert_pair.private_key = kIdentityCertPrivateKey;
  key_cert_pair.certificate_chain = kIdentityCertContents;
  std::vector<experimental::IdentityKeyCertPair> identity_key_cert_pairs;
  identity_key_cert_pairs.emplace_back(key_cert_pair);
  auto certificate_provider = std::make_shared<StaticDataCertificateProvider>(
      kRootCertContents, identity_key_cert_pairs);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name(kRootCertName);
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name(kIdentityCertName);
  auto channel_credentials = grpc::experimental::TlsCredentials(options);
  GPR_ASSERT(channel_credentials.get() != nullptr);
}

TEST(CredentialsTest,
     TlsChannelCredentialsWithStaticDataCertificateProviderLoadingRootOnly) {
  auto certificate_provider =
      std::make_shared<StaticDataCertificateProvider>(kRootCertContents);
  GPR_ASSERT(certificate_provider != nullptr);
  GPR_ASSERT(certificate_provider->c_provider() != nullptr);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name(kRootCertName);
  auto channel_credentials = grpc::experimental::TlsCredentials(options);
  GPR_ASSERT(channel_credentials.get() != nullptr);
}

TEST(
    CredentialsTest,
    TlsChannelCredentialsWithDefaultRootsAndStaticDataCertificateProviderLoadingIdentityOnly) {
  experimental::IdentityKeyCertPair key_cert_pair;
  key_cert_pair.private_key = kIdentityCertPrivateKey;
  key_cert_pair.certificate_chain = kIdentityCertContents;
  std::vector<experimental::IdentityKeyCertPair> identity_key_cert_pairs;
  identity_key_cert_pairs.emplace_back(key_cert_pair);
  auto certificate_provider =
      std::make_shared<StaticDataCertificateProvider>(identity_key_cert_pairs);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(certificate_provider);
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name(kIdentityCertName);
  auto channel_credentials = grpc::experimental::TlsCredentials(options);
  GPR_ASSERT(channel_credentials.get() != nullptr);
}

TEST(
    CredentialsTest,
    TlsChannelCredentialsWithFileWatcherCertificateProviderLoadingRootAndIdentity) {
  auto certificate_provider = std::make_shared<FileWatcherCertificateProvider>(
      SERVER_KEY_PATH, SERVER_CERT_PATH, CA_CERT_PATH, 1);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name(kRootCertName);
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name(kIdentityCertName);
  auto channel_credentials = grpc::experimental::TlsCredentials(options);
  GPR_ASSERT(channel_credentials.get() != nullptr);
}

TEST(CredentialsTest,
     TlsChannelCredentialsWithFileWatcherCertificateProviderLoadingRootOnly) {
  auto certificate_provider =
      std::make_shared<FileWatcherCertificateProvider>(CA_CERT_PATH, 1);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name(kRootCertName);
  auto channel_credentials = grpc::experimental::TlsCredentials(options);
  GPR_ASSERT(channel_credentials.get() != nullptr);
}

TEST(CredentialsTest, TlsChannelCredentialsWithHostNameVerifier) {
  auto verifier = std::make_shared<HostNameCertificateVerifier>();
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_verify_server_certs(true);
  options.set_certificate_verifier(verifier);
  auto channel_credentials = grpc::experimental::TlsCredentials(options);
  GPR_ASSERT(channel_credentials.get() != nullptr);
}

TEST(CredentialsTest, TlsChannelCredentialsWithSyncExternalVerifier) {
  auto verifier =
      ExternalCertificateVerifier::Create<SyncCertificateVerifier>(true);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_verify_server_certs(true);
  options.set_certificate_verifier(verifier);
  options.set_check_call_host(false);
  auto channel_credentials = grpc::experimental::TlsCredentials(options);
  GPR_ASSERT(channel_credentials.get() != nullptr);
}

TEST(CredentialsTest, TlsChannelCredentialsWithAsyncExternalVerifier) {
  auto verifier =
      ExternalCertificateVerifier::Create<AsyncCertificateVerifier>(true);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_verify_server_certs(true);
  options.set_certificate_verifier(verifier);
  options.set_check_call_host(false);
  auto channel_credentials = grpc::experimental::TlsCredentials(options);
  GPR_ASSERT(channel_credentials.get() != nullptr);
}

TEST(CredentialsTest, TlsChannelCredentialsWithCrlDirectory) {
  auto certificate_provider = std::make_shared<FileWatcherCertificateProvider>(
      SERVER_KEY_PATH, SERVER_CERT_PATH, CA_CERT_PATH, 1);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name(kRootCertName);
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name(kIdentityCertName);
  options.set_crl_directory(CRL_DIR_PATH);
  auto channel_credentials = grpc::experimental::TlsCredentials(options);
  GPR_ASSERT(channel_credentials.get() != nullptr);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
