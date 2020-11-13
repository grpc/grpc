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

#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/credentials/tls/tls_credentials.h"
#include "src/core/lib/security/security_connector/tls/tls_security_connector.h"
#include "test/core/util/test_config.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"
#define INVALID_PATH "invalid/path"

namespace grpc_core {

namespace testing {

class GrpcTlsCredentialsOptionsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grpc_slice ca_slice, cert_slice, key_slice;
    GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                                 grpc_load_file(CA_CERT_PATH, 1, &ca_slice)));
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(SERVER_CERT_PATH, 1, &cert_slice)));
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(SERVER_KEY_PATH, 1, &key_slice)));
    root_cert_ = std::string(StringViewFromSlice(ca_slice));
    std::string identity_key = std::string(StringViewFromSlice(key_slice));
    std::string identity_cert = std::string(StringViewFromSlice(cert_slice));
    grpc_ssl_pem_key_cert_pair* ssl_pair =
        static_cast<grpc_ssl_pem_key_cert_pair*>(
            gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
    ssl_pair->private_key = gpr_strdup(identity_key.c_str());
    ssl_pair->cert_chain = gpr_strdup(identity_cert.c_str());
    identity_pairs_.emplace_back(ssl_pair);
    grpc_slice_unref(ca_slice);
    grpc_slice_unref(cert_slice);
    grpc_slice_unref(key_slice);
  }

  std::string root_cert_;
  PemKeyCertPairList identity_pairs_;
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
      root_cert_, identity_pairs_);
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
  auto provider =
      MakeRefCounted<StaticDataCertificateProvider>("", identity_pairs_);
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
      root_cert_, identity_pairs_);
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
  auto provider =
      MakeRefCounted<StaticDataCertificateProvider>("", identity_pairs_);
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
