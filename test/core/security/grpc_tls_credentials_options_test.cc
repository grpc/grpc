/*
 *
 * Copyright 2020 gRPC authors.
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

#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"

#include <gmock/gmock.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <gtest/gtest.h>

#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/credentials/tls/tls_credentials.h"
#include "src/core/lib/security/security_connector/tls/tls_security_connector.h"
#include "test/core/util/test_config.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"
#define CA_CERT_PATH_2 "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH_2 "src/core/tsi/test_creds/server0.pem"
#define SERVER_KEY_PATH_2 "src/core/tsi/test_creds/server0.key"
#define INVALID_PATH "invalid/path"

namespace grpc_core {

namespace testing {

class GrpcTlsCredentialsOptionsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    LoadCredentialData(CA_CERT_PATH, 1, &root_cert_);
    LoadCredentialData(CA_CERT_PATH, 0, &root_cert_no_terminator_);
    LoadCredentialData(CA_CERT_PATH_2, 1, &root_cert_2_);
    LoadCredentialData(CA_CERT_PATH_2, 0, &root_cert_no_terminator_2_);
    LoadCredentialData(SERVER_CERT_PATH, 1, &cert_chain_);
    LoadCredentialData(SERVER_CERT_PATH, 0, &cert_chain_no_terminator_);
    LoadCredentialData(SERVER_CERT_PATH_2, 1, &cert_chain_2_);
    LoadCredentialData(SERVER_CERT_PATH_2, 0, &cert_chain_no_terminator_2_);
    LoadCredentialData(SERVER_KEY_PATH, 1, &private_key_);
    LoadCredentialData(SERVER_KEY_PATH, 0, &private_key_no_terminator_);
    LoadCredentialData(SERVER_KEY_PATH_2, 1, &private_key_2_);
    LoadCredentialData(SERVER_KEY_PATH_2, 0, &private_key_no_terminator_2_);
  }

  static PemKeyCertPairList MakeCertKeyPairs(const char* private_key,
                                             const char* certs) {
    if (strcmp(private_key, "") == 0 && strcmp(certs, "") == 0) {
      return {};
    }
    grpc_ssl_pem_key_cert_pair* ssl_pair =
        static_cast<grpc_ssl_pem_key_cert_pair*>(
            gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
    ssl_pair->private_key = gpr_strdup(private_key);
    ssl_pair->cert_chain = gpr_strdup(certs);
    PemKeyCertPairList pem_key_cert_pairs;
    pem_key_cert_pairs.emplace_back(ssl_pair);
    return pem_key_cert_pairs;
  }

  static void LoadCredentialData(const char* path, int add_null_terminator,
                                 std::string* credential) {
    grpc_slice slice = grpc_empty_slice();
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(path, add_null_terminator, &slice)));
    *credential = std::string(StringViewFromSlice(slice));
    grpc_slice_unref(slice);
  }

  std::string root_cert_;
  std::string root_cert_no_terminator_;
  std::string root_cert_2_;
  std::string root_cert_no_terminator_2_;
  std::string private_key_;
  std::string private_key_no_terminator_;
  std::string private_key_2_;
  std::string private_key_no_terminator_2_;
  std::string cert_chain_;
  std::string cert_chain_no_terminator_;
  std::string cert_chain_2_;
  std::string cert_chain_no_terminator_2_;
};

TEST_F(GrpcTlsCredentialsOptionsTest, ErrorDetails) {
  grpc_tls_error_details error_details;
  EXPECT_STREQ(error_details.error_details().c_str(), "");
  error_details.set_error_details("test error details");
  EXPECT_STREQ(error_details.error_details().c_str(), "test error details");
}

// Tests for StaticDataCertificateProvider.
TEST_F(GrpcTlsCredentialsOptionsTest,
       ClientOptionsWithStaticDataProviderOnBothCerts) {
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto provider = MakeRefCounted<StaticDataCertificateProvider>(
      root_cert_, MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()));
  options->set_certificate_provider(std::move(provider));
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  options->set_server_verification_option(GRPC_TLS_SERVER_VERIFICATION);
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  grpc_channel_args* new_args = nullptr;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", nullptr, &new_args);
  grpc_channel_args_destroy(new_args);
  ASSERT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_TRUE(tls_connector->RootCertsForTesting().has_value());
  EXPECT_TRUE(tls_connector->KeyCertPairListForTesting().has_value());
}

TEST_F(GrpcTlsCredentialsOptionsTest,
       ClientOptionsWithStaticDataProviderOnRootCerts) {
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto provider = MakeRefCounted<StaticDataCertificateProvider>(
      root_cert_, PemKeyCertPairList());
  options->set_certificate_provider(std::move(provider));
  options->set_watch_root_cert(true);
  options->set_server_verification_option(GRPC_TLS_SERVER_VERIFICATION);
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  grpc_channel_args* new_args = nullptr;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", nullptr, &new_args);
  grpc_channel_args_destroy(new_args);
  ASSERT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_TRUE(tls_connector->RootCertsForTesting().has_value());
  EXPECT_FALSE(tls_connector->KeyCertPairListForTesting().has_value());
}

TEST_F(GrpcTlsCredentialsOptionsTest,
       ClientOptionsWithStaticDataProviderOnNotProvidedCerts) {
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto provider = MakeRefCounted<StaticDataCertificateProvider>(
      "", MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()));
  options->set_certificate_provider(std::move(provider));
  options->set_watch_root_cert(true);
  options->set_server_verification_option(GRPC_TLS_SERVER_VERIFICATION);
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  grpc_channel_args* new_args = nullptr;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", nullptr, &new_args);
  grpc_channel_args_destroy(new_args);
  ASSERT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_EQ(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
}

TEST_F(GrpcTlsCredentialsOptionsTest,
       ServerOptionsWithStaticDataProviderOnBothCerts) {
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto provider = MakeRefCounted<StaticDataCertificateProvider>(
      root_cert_, MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()));
  options->set_certificate_provider(std::move(provider));
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  options->set_cert_request_type(
      GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  auto credentials = MakeRefCounted<TlsServerCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  auto connector = credentials->create_security_connector();
  ASSERT_NE(connector, nullptr);
  TlsServerSecurityConnector* tls_connector =
      static_cast<TlsServerSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_TRUE(tls_connector->RootCertsForTesting().has_value());
  EXPECT_TRUE(tls_connector->KeyCertPairListForTesting().has_value());
}

TEST_F(GrpcTlsCredentialsOptionsTest,
       ServerOptionsWithStaticDataProviderOnIdentityCerts) {
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto provider = MakeRefCounted<StaticDataCertificateProvider>(
      "", MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()));
  options->set_certificate_provider(std::move(provider));
  options->set_watch_identity_pair(true);
  options->set_cert_request_type(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
  auto credentials = MakeRefCounted<TlsServerCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  auto connector = credentials->create_security_connector();
  ASSERT_NE(connector, nullptr);
  TlsServerSecurityConnector* tls_connector =
      static_cast<TlsServerSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_FALSE(tls_connector->RootCertsForTesting().has_value());
  EXPECT_TRUE(tls_connector->KeyCertPairListForTesting().has_value());
}

TEST_F(GrpcTlsCredentialsOptionsTest,
       ServerOptionsWithStaticDataProviderOnNotProvidedCerts) {
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto provider = MakeRefCounted<StaticDataCertificateProvider>(
      root_cert_, PemKeyCertPairList());
  options->set_certificate_provider(std::move(provider));
  options->set_watch_identity_pair(true);
  options->set_cert_request_type(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
  auto credentials = MakeRefCounted<TlsServerCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  auto connector = credentials->create_security_connector();
  ASSERT_NE(connector, nullptr);
  TlsServerSecurityConnector* tls_connector =
      static_cast<TlsServerSecurityConnector*>(connector.get());
  EXPECT_EQ(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
}

//// Tests for FileWatcherCertificateProvider.
TEST_F(GrpcTlsCredentialsOptionsTest,
       ClientOptionsWithCertWatcherProviderOnBothCerts) {
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto provider = MakeRefCounted<FileWatcherCertificateProvider>(
      SERVER_KEY_PATH, SERVER_CERT_PATH, CA_CERT_PATH, 1);
  options->set_certificate_provider(std::move(provider));
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  options->set_server_verification_option(GRPC_TLS_SERVER_VERIFICATION);
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  grpc_channel_args* new_args = nullptr;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", nullptr, &new_args);
  grpc_channel_args_destroy(new_args);
  ASSERT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_TRUE(tls_connector->RootCertsForTesting().has_value());
  EXPECT_TRUE(tls_connector->KeyCertPairListForTesting().has_value());
}

TEST_F(GrpcTlsCredentialsOptionsTest,
       ClientOptionsWithCertWatcherProviderOnRootCerts) {
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto provider =
      MakeRefCounted<FileWatcherCertificateProvider>("", "", CA_CERT_PATH, 1);
  options->set_certificate_provider(std::move(provider));
  options->set_watch_root_cert(true);
  options->set_server_verification_option(GRPC_TLS_SERVER_VERIFICATION);
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  grpc_channel_args* new_args = nullptr;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", nullptr, &new_args);
  grpc_channel_args_destroy(new_args);
  ASSERT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_TRUE(tls_connector->RootCertsForTesting().has_value());
  EXPECT_FALSE(tls_connector->KeyCertPairListForTesting().has_value());
}

TEST_F(GrpcTlsCredentialsOptionsTest,
       ClientOptionsWithCertWatcherProviderOnNotProvidedCerts) {
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto provider = MakeRefCounted<FileWatcherCertificateProvider>(
      SERVER_KEY_PATH, SERVER_CERT_PATH, "", 1);
  options->set_certificate_provider(std::move(provider));
  options->set_watch_root_cert(true);
  options->set_server_verification_option(GRPC_TLS_SERVER_VERIFICATION);
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  grpc_channel_args* new_args = nullptr;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", nullptr, &new_args);
  grpc_channel_args_destroy(new_args);
  ASSERT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_EQ(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
}

TEST_F(GrpcTlsCredentialsOptionsTest,
       ClientOptionsWithCertWatcherProviderOnBadTrustCerts) {
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto provider =
      MakeRefCounted<FileWatcherCertificateProvider>("", "", INVALID_PATH, 1);
  options->set_certificate_provider(std::move(provider));
  options->set_watch_root_cert(true);
  options->set_server_verification_option(GRPC_TLS_SERVER_VERIFICATION);
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  grpc_channel_args* new_args = nullptr;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", nullptr, &new_args);
  grpc_channel_args_destroy(new_args);
  ASSERT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_EQ(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
}

TEST_F(GrpcTlsCredentialsOptionsTest,
       ServerOptionsWithCertWatcherProviderOnBothCerts) {
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto provider = MakeRefCounted<FileWatcherCertificateProvider>(
      SERVER_KEY_PATH, SERVER_CERT_PATH, CA_CERT_PATH, 1);
  options->set_certificate_provider(std::move(provider));
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  options->set_cert_request_type(
      GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  auto credentials = MakeRefCounted<TlsServerCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  auto connector = credentials->create_security_connector();
  ASSERT_NE(connector, nullptr);
  TlsServerSecurityConnector* tls_connector =
      static_cast<TlsServerSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_TRUE(tls_connector->RootCertsForTesting().has_value());
  EXPECT_TRUE(tls_connector->KeyCertPairListForTesting().has_value());
}

TEST_F(GrpcTlsCredentialsOptionsTest,
       ServerOptionsWithCertWatcherProviderOnIdentityCerts) {
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto provider = MakeRefCounted<FileWatcherCertificateProvider>(
      SERVER_KEY_PATH, SERVER_CERT_PATH, "", 1);
  options->set_certificate_provider(std::move(provider));
  options->set_watch_identity_pair(true);
  options->set_cert_request_type(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
  auto credentials = MakeRefCounted<TlsServerCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  auto connector = credentials->create_security_connector();
  ASSERT_NE(connector, nullptr);
  TlsServerSecurityConnector* tls_connector =
      static_cast<TlsServerSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_FALSE(tls_connector->RootCertsForTesting().has_value());
  EXPECT_TRUE(tls_connector->KeyCertPairListForTesting().has_value());
}

TEST_F(GrpcTlsCredentialsOptionsTest,
       ServerOptionsWithCertWatcherProviderOnNotProvidedCerts) {
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto provider =
      MakeRefCounted<FileWatcherCertificateProvider>("", "", CA_CERT_PATH, 1);
  options->set_certificate_provider(std::move(provider));
  options->set_watch_identity_pair(true);
  options->set_cert_request_type(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
  auto credentials = MakeRefCounted<TlsServerCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  auto connector = credentials->create_security_connector();
  ASSERT_NE(connector, nullptr);
  TlsServerSecurityConnector* tls_connector =
      static_cast<TlsServerSecurityConnector*>(connector.get());
  EXPECT_EQ(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
}

TEST_F(GrpcTlsCredentialsOptionsTest,
       ServerOptionsWithCertWatcherProviderOnBadIdentityCerts) {
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto provider = MakeRefCounted<FileWatcherCertificateProvider>(
      INVALID_PATH, INVALID_PATH, "", 1);
  options->set_certificate_provider(std::move(provider));
  options->set_watch_identity_pair(true);
  options->set_cert_request_type(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
  auto credentials = MakeRefCounted<TlsServerCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  auto connector = credentials->create_security_connector();
  ASSERT_NE(connector, nullptr);
  TlsServerSecurityConnector* tls_connector =
      static_cast<TlsServerSecurityConnector*>(connector.get());
  EXPECT_EQ(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
}

TEST_F(GrpcTlsCredentialsOptionsTest,
       ClientOptionsWithCertWatcherProviderOnCertificateRefreshed) {
  // Create a temporary file and copy root cert data into it.
  FILE* root_cert_tmp = nullptr;
  char* root_cert_tmp_name = nullptr;
  root_cert_tmp = gpr_tmpfile("GrpcTlsCertificateProviderTest_root_cert",
                              &root_cert_tmp_name);
  GPR_ASSERT(root_cert_tmp_name != nullptr);
  GPR_ASSERT(root_cert_tmp != nullptr);
  GPR_ASSERT(fwrite(root_cert_no_terminator_.c_str(), 1,
                    root_cert_no_terminator_.size(),
                    root_cert_tmp) == root_cert_no_terminator_.size());
  fclose(root_cert_tmp);
  // Create a temporary file and copy identity key data into it.
  FILE* identity_key_tmp = nullptr;
  char* identity_key_tmp_name = nullptr;
  identity_key_tmp = gpr_tmpfile("GrpcTlsCertificateProviderTest_identity_key",
                                 &identity_key_tmp_name);
  GPR_ASSERT(identity_key_tmp_name != nullptr);
  GPR_ASSERT(identity_key_tmp != nullptr);
  GPR_ASSERT(fwrite(private_key_no_terminator_.c_str(), 1,
                    private_key_no_terminator_.size(),
                    identity_key_tmp) == private_key_no_terminator_.size());
  fclose(identity_key_tmp);
  // Create a temporary file and copy identity cert data into it.
  FILE* identity_cert_tmp = nullptr;
  char* identity_cert_tmp_name = nullptr;
  identity_cert_tmp = gpr_tmpfile(
      "GrpcTlsCertificateProviderTest_identity_cert", &identity_cert_tmp_name);
  GPR_ASSERT(identity_cert_tmp_name != nullptr);
  GPR_ASSERT(identity_cert_tmp != nullptr);
  GPR_ASSERT(fwrite(cert_chain_no_terminator_.c_str(), 1,
                    cert_chain_no_terminator_.size(),
                    identity_cert_tmp) == cert_chain_no_terminator_.size());
  fclose(identity_cert_tmp);
  // Create ClientOptions using FileWatcherCertificateProvider.
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto provider = MakeRefCounted<FileWatcherCertificateProvider>(
      identity_key_tmp_name, identity_cert_tmp_name, root_cert_tmp_name, 1);
  options->set_certificate_provider(std::move(provider));
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  options->set_server_verification_option(GRPC_TLS_SERVER_VERIFICATION);
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  grpc_channel_args* new_args = nullptr;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", nullptr, &new_args);
  grpc_channel_args_destroy(new_args);
  ASSERT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  // The initial data is all good, so we expect to have successful credential
  // updates.
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  ASSERT_TRUE(tls_connector->RootCertsForTesting().has_value());
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_);
  ASSERT_TRUE(tls_connector->KeyCertPairListForTesting().has_value());
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(),
            MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()));
  // Copy new data to files.
  root_cert_tmp = fopen(root_cert_tmp_name, "w");
  GPR_ASSERT(root_cert_tmp != nullptr);
  GPR_ASSERT(fwrite(root_cert_no_terminator_2_.c_str(), 1,
                    root_cert_no_terminator_2_.size(),
                    root_cert_tmp) == root_cert_no_terminator_2_.size());
  fclose(root_cert_tmp);
  identity_key_tmp = fopen(identity_key_tmp_name, "w");
  GPR_ASSERT(identity_key_tmp != nullptr);
  GPR_ASSERT(fwrite(private_key_no_terminator_2_.c_str(), 1,
                    private_key_no_terminator_2_.size(),
                    identity_key_tmp) == private_key_no_terminator_2_.size());
  fclose(identity_key_tmp);
  identity_cert_tmp = fopen(identity_cert_tmp_name, "w");
  GPR_ASSERT(identity_cert_tmp != nullptr);
  GPR_ASSERT(fwrite(cert_chain_no_terminator_2_.c_str(), 1,
                    cert_chain_no_terminator_2_.size(),
                    identity_cert_tmp) == cert_chain_no_terminator_2_.size());
  fclose(identity_cert_tmp);
  // Wait 2 seconds for the provider's refresh thread to read the updated files.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(2, GPR_TIMESPAN)));
  // Expect to see new credential data loaded by the security connector.
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  ASSERT_TRUE(tls_connector->RootCertsForTesting().has_value());
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_2_);
  ASSERT_TRUE(tls_connector->KeyCertPairListForTesting().has_value());
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(),
            MakeCertKeyPairs(private_key_2_.c_str(), cert_chain_2_.c_str()));
  // Clean up.
  gpr_free(identity_key_tmp_name);
  gpr_free(identity_cert_tmp_name);
  gpr_free(root_cert_tmp_name);
  remove(root_cert_tmp_name);
  remove(identity_key_tmp_name);
  remove(identity_cert_tmp_name);
}

TEST_F(GrpcTlsCredentialsOptionsTest,
       ClientOptionsWithCertWatcherProviderOnDeletedFiles) {
  // Create a temporary file and copy root cert data into it.
  FILE* root_cert_tmp = nullptr;
  char* root_cert_tmp_name = nullptr;
  root_cert_tmp = gpr_tmpfile("GrpcTlsCertificateProviderTest_root_cert",
                              &root_cert_tmp_name);
  GPR_ASSERT(root_cert_tmp_name != nullptr);
  GPR_ASSERT(root_cert_tmp != nullptr);
  GPR_ASSERT(fwrite(root_cert_no_terminator_.c_str(), 1,
                    root_cert_no_terminator_.size(),
                    root_cert_tmp) == root_cert_no_terminator_.size());
  fclose(root_cert_tmp);
  // Create a temporary file and copy identity key data into it.
  FILE* identity_key_tmp = nullptr;
  char* identity_key_tmp_name = nullptr;
  identity_key_tmp = gpr_tmpfile("GrpcTlsCertificateProviderTest_identity_key",
                                 &identity_key_tmp_name);
  GPR_ASSERT(identity_key_tmp_name != nullptr);
  GPR_ASSERT(identity_key_tmp != nullptr);
  GPR_ASSERT(fwrite(private_key_no_terminator_.c_str(), 1,
                    private_key_no_terminator_.size(),
                    identity_key_tmp) == private_key_no_terminator_.size());
  fclose(identity_key_tmp);
  // Create a temporary file and copy identity cert data into it.
  FILE* identity_cert_tmp = nullptr;
  char* identity_cert_tmp_name = nullptr;
  identity_cert_tmp = gpr_tmpfile(
      "GrpcTlsCertificateProviderTest_identity_cert", &identity_cert_tmp_name);
  GPR_ASSERT(identity_cert_tmp_name != nullptr);
  GPR_ASSERT(identity_cert_tmp != nullptr);
  GPR_ASSERT(fwrite(cert_chain_no_terminator_.c_str(), 1,
                    cert_chain_no_terminator_.size(),
                    identity_cert_tmp) == cert_chain_no_terminator_.size());
  fclose(identity_cert_tmp);
  // Create ClientOptions using FileWatcherCertificateProvider.
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto provider = MakeRefCounted<FileWatcherCertificateProvider>(
      identity_key_tmp_name, identity_cert_tmp_name, root_cert_tmp_name, 1);
  options->set_certificate_provider(std::move(provider));
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  options->set_server_verification_option(GRPC_TLS_SERVER_VERIFICATION);
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  grpc_channel_args* new_args = nullptr;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", nullptr, &new_args);
  grpc_channel_args_destroy(new_args);
  ASSERT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  // The initial data is all good, so we expect to have successful credential
  // updates.
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  ASSERT_TRUE(tls_connector->RootCertsForTesting().has_value());
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_);
  ASSERT_TRUE(tls_connector->KeyCertPairListForTesting().has_value());
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(),
            MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()));
  // Remove all the files.
  remove(root_cert_tmp_name);
  remove(identity_key_tmp_name);
  remove(identity_cert_tmp_name);
  // Wait 2 seconds for the provider's refresh thread to read the deleted files.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(2, GPR_TIMESPAN)));
  // It's a bit hard to test if errors are sent to the security connector,
  // because the security connector simply logs the error. We will see the err
  // messages if we open the log.
  // Clean up.
  gpr_free(identity_key_tmp_name);
  gpr_free(identity_cert_tmp_name);
  gpr_free(root_cert_tmp_name);
}

}  // namespace testing

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
