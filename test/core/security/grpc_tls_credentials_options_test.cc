//
//
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
//
//

#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/credentials/tls/tls_credentials.h"
#include "src/core/lib/security/security_connector/tls/tls_security_connector.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"
#define CA_CERT_PATH_2 "src/core/tsi/test_creds/multi-domain.pem"
#define SERVER_CERT_PATH_2 "src/core/tsi/test_creds/server0.pem"
#define SERVER_KEY_PATH_2 "src/core/tsi/test_creds/server0.key"
#define INVALID_PATH "invalid/path"

namespace grpc_core {

namespace testing {

class GrpcTlsCredentialsOptionsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    root_cert_ = GetFileContents(CA_CERT_PATH);
    cert_chain_ = GetFileContents(SERVER_CERT_PATH);
    private_key_ = GetFileContents(SERVER_KEY_PATH);
    root_cert_2_ = GetFileContents(CA_CERT_PATH_2);
    cert_chain_2_ = GetFileContents(SERVER_CERT_PATH_2);
    private_key_2_ = GetFileContents(SERVER_KEY_PATH_2);
  }

  std::string root_cert_;
  std::string private_key_;
  std::string cert_chain_;
  std::string root_cert_2_;
  std::string private_key_2_;
  std::string cert_chain_2_;
  HostNameCertificateVerifier hostname_certificate_verifier_;
};

//
// Tests for Default Root Certs.
//

TEST_F(GrpcTlsCredentialsOptionsTest, ClientOptionsOnDefaultRootCerts) {
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  ChannelArgs new_args;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", &new_args);
  ASSERT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
}

//
// Tests for StaticDataCertificateProvider.
//

TEST_F(GrpcTlsCredentialsOptionsTest,
       ClientOptionsWithStaticDataProviderOnBothCerts) {
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto provider = MakeRefCounted<StaticDataCertificateProvider>(
      root_cert_, MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()));
  options->set_certificate_provider(std::move(provider));
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  ChannelArgs new_args;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", &new_args);
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
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  ChannelArgs new_args;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", &new_args);
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
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  ChannelArgs new_args;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", &new_args);
  ASSERT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_EQ(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
}

TEST_F(GrpcTlsCredentialsOptionsTest,
       ClientOptionsWithDefaultRootAndStaticDataProviderOnIdentityCerts) {
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto provider = MakeRefCounted<StaticDataCertificateProvider>(
      "", MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()));
  options->set_certificate_provider(std::move(provider));
  options->set_watch_identity_pair(true);
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  ChannelArgs new_args;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", &new_args);
  ASSERT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
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
  auto connector = credentials->create_security_connector(ChannelArgs());
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
  auto connector = credentials->create_security_connector(ChannelArgs());
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
  auto connector = credentials->create_security_connector(ChannelArgs());
  ASSERT_NE(connector, nullptr);
  TlsServerSecurityConnector* tls_connector =
      static_cast<TlsServerSecurityConnector*>(connector.get());
  EXPECT_EQ(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
}

//
// Tests for FileWatcherCertificateProvider.
//

TEST_F(GrpcTlsCredentialsOptionsTest,
       ClientOptionsWithCertWatcherProviderOnBothCerts) {
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto provider = MakeRefCounted<FileWatcherCertificateProvider>(
      SERVER_KEY_PATH, SERVER_CERT_PATH, CA_CERT_PATH, 1);
  options->set_certificate_provider(std::move(provider));
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  ChannelArgs new_args;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", &new_args);
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
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  ChannelArgs new_args;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", &new_args);
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
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  ChannelArgs new_args;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", &new_args);
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
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  ChannelArgs new_args;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", &new_args);
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
  auto connector = credentials->create_security_connector(ChannelArgs());
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
  auto connector = credentials->create_security_connector(ChannelArgs());
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
  auto connector = credentials->create_security_connector(ChannelArgs());
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
  auto connector = credentials->create_security_connector(ChannelArgs());
  ASSERT_NE(connector, nullptr);
  TlsServerSecurityConnector* tls_connector =
      static_cast<TlsServerSecurityConnector*>(connector.get());
  EXPECT_EQ(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
}

//
// Tests writing credential data to temporary files to test the
// transition behavior of the provider.
//

TEST_F(GrpcTlsCredentialsOptionsTest,
       ClientOptionsWithCertWatcherProviderOnCertificateRefreshed) {
  // Create temporary files and copy cert data into them.
  TmpFile tmp_root_cert(root_cert_);
  TmpFile tmp_identity_key(private_key_);
  TmpFile tmp_identity_cert(cert_chain_);
  // Create ClientOptions using FileWatcherCertificateProvider.
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto provider = MakeRefCounted<FileWatcherCertificateProvider>(
      tmp_identity_key.name(), tmp_identity_cert.name(), tmp_root_cert.name(),
      1);
  options->set_certificate_provider(std::move(provider));
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  ChannelArgs new_args;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", &new_args);
  ASSERT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  // Expect to see the credential data.
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  ASSERT_TRUE(tls_connector->RootCertsForTesting().has_value());
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_);
  ASSERT_TRUE(tls_connector->KeyCertPairListForTesting().has_value());
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(),
            MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()));
  // Copy new data to files.
  // TODO(ZhenLian): right now it is not completely atomic. Use the real atomic
  // update when the directory renaming is added in gpr.
  tmp_root_cert.RewriteFile(root_cert_2_);
  tmp_identity_key.RewriteFile(private_key_2_);
  tmp_identity_cert.RewriteFile(cert_chain_2_);
  // Wait 10 seconds for the provider's refresh thread to read the updated
  // files.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(10, GPR_TIMESPAN)));
  // Expect to see new credential data loaded by the security connector.
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  ASSERT_TRUE(tls_connector->RootCertsForTesting().has_value());
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_2_);
  ASSERT_TRUE(tls_connector->KeyCertPairListForTesting().has_value());
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(),
            MakeCertKeyPairs(private_key_2_.c_str(), cert_chain_2_.c_str()));
}

TEST_F(GrpcTlsCredentialsOptionsTest,
       ClientOptionsWithCertWatcherProviderOnDeletedFiles) {
  // Create temporary files and copy cert data into it.
  auto tmp_root_cert = std::make_unique<TmpFile>(root_cert_);
  auto tmp_identity_key = std::make_unique<TmpFile>(private_key_);
  auto tmp_identity_cert = std::make_unique<TmpFile>(cert_chain_);
  // Create ClientOptions using FileWatcherCertificateProvider.
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  auto provider = MakeRefCounted<FileWatcherCertificateProvider>(
      tmp_identity_key->name(), tmp_identity_cert->name(),
      tmp_root_cert->name(), 1);
  options->set_certificate_provider(std::move(provider));
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  ChannelArgs new_args;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", &new_args);
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
  // Delete TmpFile objects, which will remove the corresponding files.
  tmp_root_cert.reset();
  tmp_identity_key.reset();
  tmp_identity_cert.reset();
  // Wait 10 seconds for the provider's refresh thread to read the deleted
  // files.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(10, GPR_TIMESPAN)));
  // It's a bit hard to test if errors are sent to the security connector,
  // because the security connector simply logs the error. We will see the err
  // messages if we open the log.
  // The old certs should still being used.
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  ASSERT_TRUE(tls_connector->RootCertsForTesting().has_value());
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_);
  ASSERT_TRUE(tls_connector->KeyCertPairListForTesting().has_value());
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(),
            MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()));
}

//
// Tests for ExternalCertificateVerifier.
// It will only test the creation of security connector, so the actual verify
// logic is not invoked.
//

TEST_F(GrpcTlsCredentialsOptionsTest, ClientOptionsWithExternalVerifier) {
  auto* sync_verifier_ = new SyncExternalVerifier(true);
  ExternalCertificateVerifier core_external_verifier(sync_verifier_->base());
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  options->set_verify_server_cert(true);
  options->set_certificate_verifier(core_external_verifier.Ref());
  options->set_check_call_host(false);
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  ChannelArgs new_args;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", &new_args);
  ASSERT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector, nullptr);
}

TEST_F(GrpcTlsCredentialsOptionsTest, ServerOptionsWithExternalVerifier) {
  auto* sync_verifier_ = new SyncExternalVerifier(true);
  ExternalCertificateVerifier core_external_verifier(sync_verifier_->base());
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  options->set_cert_request_type(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
  options->set_certificate_verifier(core_external_verifier.Ref());
  // On server side we have to set the provider providing identity certs.
  auto provider = MakeRefCounted<StaticDataCertificateProvider>(
      root_cert_, PemKeyCertPairList());
  options->set_certificate_provider(std::move(provider));
  options->set_watch_identity_pair(true);
  auto credentials = MakeRefCounted<TlsServerCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  auto connector = credentials->create_security_connector(ChannelArgs());
  ASSERT_NE(connector, nullptr);
  TlsServerSecurityConnector* tls_connector =
      static_cast<TlsServerSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector, nullptr);
}

//
// Tests for HostnameCertificateVerifier.
//

TEST_F(GrpcTlsCredentialsOptionsTest,
       ClientOptionsWithHostnameCertificateVerifier) {
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  options->set_verify_server_cert(true);
  options->set_certificate_verifier(hostname_certificate_verifier_.Ref());
  auto credentials = MakeRefCounted<TlsCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  ChannelArgs new_args;
  auto connector = credentials->create_security_connector(
      nullptr, "random targets", &new_args);
  ASSERT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
}

TEST_F(GrpcTlsCredentialsOptionsTest,
       ServerOptionsWithHostnameCertificateVerifier) {
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  options->set_cert_request_type(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
  options->set_certificate_verifier(hostname_certificate_verifier_.Ref());
  // On server side we have to set the provider providing identity certs.
  auto provider = MakeRefCounted<StaticDataCertificateProvider>(
      root_cert_, PemKeyCertPairList());
  options->set_certificate_provider(std::move(provider));
  options->set_watch_identity_pair(true);
  auto credentials = MakeRefCounted<TlsServerCredentials>(options);
  ASSERT_NE(credentials, nullptr);
  auto connector = credentials->create_security_connector(ChannelArgs());
  ASSERT_NE(connector, nullptr);
  TlsServerSecurityConnector* tls_connector =
      static_cast<TlsServerSecurityConnector*>(connector.get());
  EXPECT_EQ(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
}

}  // namespace testing

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_core::ConfigVars::Overrides overrides;
  overrides.default_ssl_roots_file_path = CA_CERT_PATH;
  grpc_core::ConfigVars::SetOverrides(overrides);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
