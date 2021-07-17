/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/core/lib/security/security_connector/tls/tls_security_connector.h"

#include <gmock/gmock.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <gtest/gtest.h>
#include <stdlib.h>
#include <string.h>

#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"
#include "src/core/lib/security/credentials/tls/tls_credentials.h"
#include "src/core/tsi/transport_security.h"
#include "test/core/util/test_config.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define CLIENT_CERT_PATH "src/core/tsi/test_creds/multi-domain.pem"
#define SERVER_CERT_PATH_0 "src/core/tsi/test_creds/server0.pem"
#define SERVER_KEY_PATH_0 "src/core/tsi/test_creds/server0.key"
#define SERVER_CERT_PATH_1 "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH_1 "src/core/tsi/test_creds/server1.key"

namespace grpc {
namespace testing {

constexpr const char* kRootCertName = "root_cert_name";
constexpr const char* kIdentityCertName = "identity_cert_name";
constexpr const char* kErrorMessage = "error_message";
constexpr const char* kTargetName = "some_target";

class TlsSecurityConnectorTest : public ::testing::Test {
 protected:
  TlsSecurityConnectorTest() {}
  void SetUp() override {
    grpc_slice ca_slice_1, ca_slice_0, cert_slice_1, key_slice_1, cert_slice_0,
        key_slice_0;
    GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                                 grpc_load_file(CA_CERT_PATH, 1, &ca_slice_1)));
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(CLIENT_CERT_PATH, 1, &ca_slice_0)));
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(SERVER_CERT_PATH_1, 1, &cert_slice_1)));
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(SERVER_KEY_PATH_1, 1, &key_slice_1)));
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(SERVER_CERT_PATH_0, 1, &cert_slice_0)));
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(SERVER_KEY_PATH_0, 1, &key_slice_0)));
    root_cert_1_ = std::string(grpc_core::StringViewFromSlice(ca_slice_1));
    root_cert_0_ = std::string(grpc_core::StringViewFromSlice(ca_slice_0));
    std::string identity_key_1 =
        std::string(grpc_core::StringViewFromSlice(key_slice_1));
    std::string identity_key_0 =
        std::string(grpc_core::StringViewFromSlice(key_slice_0));
    std::string identity_cert_1 =
        std::string(grpc_core::StringViewFromSlice(cert_slice_1));
    std::string identity_cert_0 =
        std::string(grpc_core::StringViewFromSlice(cert_slice_0));
    identity_pairs_1_.emplace_back(identity_key_1, identity_cert_1);
    identity_pairs_0_.emplace_back(identity_key_0, identity_cert_0);
    grpc_slice_unref(ca_slice_1);
    grpc_slice_unref(ca_slice_0);
    grpc_slice_unref(cert_slice_1);
    grpc_slice_unref(key_slice_1);
    grpc_slice_unref(cert_slice_0);
    grpc_slice_unref(key_slice_0);
  }

  void TearDown() override {}

  std::string root_cert_1_;
  std::string root_cert_0_;
  grpc_core::PemKeyCertPairList identity_pairs_1_;
  grpc_core::PemKeyCertPairList identity_pairs_0_;
};

class TlsTestCertificateProvider : public ::grpc_tls_certificate_provider {
 public:
  explicit TlsTestCertificateProvider(
      grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor)
      : distributor_(std::move(distributor)) {}
  ~TlsTestCertificateProvider() override {}
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor()
      const override {
    return distributor_;
  }

 private:
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor_;
};

// Tests for ChannelSecurityConnector.
TEST_F(TlsSecurityConnectorTest,
       RootAndIdentityCertsObtainedWhenCreateChannelSecurityConnector) {
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      grpc_core::MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, absl::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_0_);
  grpc_core::RefCountedPtr<::grpc_tls_certificate_provider> provider =
      grpc_core::MakeRefCounted<TlsTestCertificateProvider>(distributor);
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  options->set_certificate_provider(provider);
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  grpc_core::RefCountedPtr<TlsCredentials> credential =
      grpc_core::MakeRefCounted<TlsCredentials>(options);
  grpc_channel_args* new_args = nullptr;
  grpc_core::RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, nullptr,
                                            &new_args);
  EXPECT_NE(connector, nullptr);
  grpc_core::TlsChannelSecurityConnector* tls_connector =
      static_cast<grpc_core::TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  distributor->SetKeyMaterials(kRootCertName, root_cert_1_, absl::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_1_);
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_1_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_1_);
  grpc_channel_args_destroy(new_args);
}

TEST_F(TlsSecurityConnectorTest,
       SystemRootsWhenCreateChannelSecurityConnector) {
  // Create options watching for no certificates.
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> root_options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  grpc_core::RefCountedPtr<TlsCredentials> root_credential =
      grpc_core::MakeRefCounted<TlsCredentials>(root_options);
  grpc_channel_args* root_new_args = nullptr;
  grpc_core::RefCountedPtr<grpc_channel_security_connector> root_connector =
      root_credential->create_security_connector(nullptr, "some_target",
                                                 nullptr, &root_new_args);
  EXPECT_NE(root_connector, nullptr);
  grpc_core::TlsChannelSecurityConnector* tls_root_connector =
      static_cast<grpc_core::TlsChannelSecurityConnector*>(
          root_connector.get());
  EXPECT_NE(tls_root_connector->ClientHandshakerFactoryForTesting(), nullptr);
  grpc_channel_args_destroy(root_new_args);
}

TEST_F(TlsSecurityConnectorTest,
       SystemRootsAndIdentityCertsObtainedWhenCreateChannelSecurityConnector) {
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      grpc_core::MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_0_);
  grpc_core::RefCountedPtr<::grpc_tls_certificate_provider> provider =
      grpc_core::MakeRefCounted<TlsTestCertificateProvider>(distributor);
  // Create options only watching for identity certificates.
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> root_options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  root_options->set_certificate_provider(provider);
  root_options->set_watch_identity_pair(true);
  root_options->set_identity_cert_name(kIdentityCertName);
  grpc_core::RefCountedPtr<TlsCredentials> root_credential =
      grpc_core::MakeRefCounted<TlsCredentials>(root_options);
  grpc_channel_args* root_new_args = nullptr;
  grpc_core::RefCountedPtr<grpc_channel_security_connector> root_connector =
      root_credential->create_security_connector(nullptr, "some_target",
                                                 nullptr, &root_new_args);
  EXPECT_NE(root_connector, nullptr);
  grpc_core::TlsChannelSecurityConnector* tls_root_connector =
      static_cast<grpc_core::TlsChannelSecurityConnector*>(
          root_connector.get());
  EXPECT_NE(tls_root_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_root_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  // If we have a root update, we shouldn't receive them in security connector,
  // since we claimed to use default system roots.
  distributor->SetKeyMaterials(kRootCertName, root_cert_1_, absl::nullopt);
  EXPECT_NE(tls_root_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_NE(tls_root_connector->RootCertsForTesting(), root_cert_1_);
  grpc_channel_args_destroy(root_new_args);
}

TEST_F(TlsSecurityConnectorTest,
       RootCertsObtainedWhenCreateChannelSecurityConnector) {
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      grpc_core::MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, absl::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_0_);
  grpc_core::RefCountedPtr<::grpc_tls_certificate_provider> provider =
      grpc_core::MakeRefCounted<TlsTestCertificateProvider>(distributor);
  // Create options only watching for root certificates.
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> root_options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  root_options->set_certificate_provider(provider);
  root_options->set_watch_root_cert(true);
  root_options->set_root_cert_name(kRootCertName);
  grpc_core::RefCountedPtr<TlsCredentials> root_credential =
      grpc_core::MakeRefCounted<TlsCredentials>(root_options);
  grpc_channel_args* root_new_args = nullptr;
  grpc_core::RefCountedPtr<grpc_channel_security_connector> root_connector =
      root_credential->create_security_connector(nullptr, "some_target",
                                                 nullptr, &root_new_args);
  EXPECT_NE(root_connector, nullptr);
  grpc_core::TlsChannelSecurityConnector* tls_root_connector =
      static_cast<grpc_core::TlsChannelSecurityConnector*>(
          root_connector.get());
  EXPECT_NE(tls_root_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_root_connector->RootCertsForTesting(), root_cert_0_);
  distributor->SetKeyMaterials(kRootCertName, root_cert_1_, absl::nullopt);
  EXPECT_NE(tls_root_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_root_connector->RootCertsForTesting(), root_cert_1_);
  grpc_channel_args_destroy(root_new_args);
}

TEST_F(TlsSecurityConnectorTest,
       CertPartiallyObtainedWhenCreateChannelSecurityConnector) {
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      grpc_core::MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, absl::nullopt);
  grpc_core::RefCountedPtr<::grpc_tls_certificate_provider> provider =
      grpc_core::MakeRefCounted<TlsTestCertificateProvider>(distributor);
  // Registered the options watching both certs, but only root certs are
  // available at distributor right now.
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  options->set_certificate_provider(provider);
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  grpc_core::RefCountedPtr<TlsCredentials> credential =
      grpc_core::MakeRefCounted<TlsCredentials>(options);
  grpc_channel_args* new_args = nullptr;
  grpc_core::RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, nullptr,
                                            &new_args);
  EXPECT_NE(connector, nullptr);
  grpc_core::TlsChannelSecurityConnector* tls_connector =
      static_cast<grpc_core::TlsChannelSecurityConnector*>(connector.get());
  // The client_handshaker_factory_ shouldn't be updated.
  EXPECT_EQ(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_0_);
  // After updating the root certs, the client_handshaker_factory_ should be
  // updated.
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_0_);
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  grpc_channel_args_destroy(new_args);
}

TEST_F(TlsSecurityConnectorTest,
       DistributorHasErrorForChannelSecurityConnector) {
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      grpc_core::MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, absl::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_0_);
  grpc_core::RefCountedPtr<::grpc_tls_certificate_provider> provider =
      grpc_core::MakeRefCounted<TlsTestCertificateProvider>(distributor);
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  options->set_certificate_provider(provider);
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  grpc_core::RefCountedPtr<TlsCredentials> credential =
      grpc_core::MakeRefCounted<TlsCredentials>(options);
  grpc_channel_args* new_args = nullptr;
  grpc_core::RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, nullptr,
                                            &new_args);
  EXPECT_NE(connector, nullptr);
  grpc_core::TlsChannelSecurityConnector* tls_connector =
      static_cast<grpc_core::TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  // Calling SetErrorForCert on distributor shouldn't invalidate the previous
  // valid credentials.
  distributor->SetErrorForCert(
      kRootCertName, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kErrorMessage),
      absl::nullopt);
  distributor->SetErrorForCert(
      kIdentityCertName, absl::nullopt,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(kErrorMessage));
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  grpc_channel_args_destroy(new_args);
}

TEST_F(TlsSecurityConnectorTest,
       CreateChannelSecurityConnectorFailNoTargetName) {
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  grpc_core::RefCountedPtr<TlsCredentials> credential =
      grpc_core::MakeRefCounted<TlsCredentials>(options);
  grpc_channel_args* new_args = nullptr;
  grpc_core::RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, nullptr, nullptr,
                                            &new_args);
  EXPECT_EQ(connector, nullptr);
}

TEST_F(TlsSecurityConnectorTest,
       CreateChannelSecurityConnectorFailNoCredentials) {
  auto connector =
      grpc_core::TlsChannelSecurityConnector::CreateTlsChannelSecurityConnector(
          nullptr, grpc_core::MakeRefCounted<grpc_tls_credentials_options>(),
          nullptr, kTargetName, nullptr, nullptr);
  EXPECT_EQ(connector, nullptr);
}

TEST_F(TlsSecurityConnectorTest, CreateChannelSecurityConnectorFailNoOptions) {
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  grpc_core::RefCountedPtr<TlsCredentials> credential =
      grpc_core::MakeRefCounted<TlsCredentials>(options);
  auto connector =
      grpc_core::TlsChannelSecurityConnector::CreateTlsChannelSecurityConnector(
          credential, nullptr, nullptr, kTargetName, nullptr, nullptr);
  EXPECT_EQ(connector, nullptr);
}

TEST_F(TlsSecurityConnectorTest, TlsCheckHostNameSuccess) {
  const char* target_name = "foo.test.google.fr";
  tsi_peer peer;
  GPR_ASSERT(tsi_construct_peer(1, &peer) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY, target_name,
                 &peer.properties[0]) == TSI_OK);
  grpc_error_handle error =
      grpc_core::internal::TlsCheckHostName(target_name, &peer);
  tsi_peer_destruct(&peer);
  EXPECT_EQ(error, GRPC_ERROR_NONE);
  GRPC_ERROR_UNREF(error);
}

TEST_F(TlsSecurityConnectorTest, TlsCheckHostNameFail) {
  const char* target_name = "foo.test.google.fr";
  const char* another_name = "bar.test.google.fr";
  tsi_peer peer;
  GPR_ASSERT(tsi_construct_peer(1, &peer) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY, another_name,
                 &peer.properties[0]) == TSI_OK);
  grpc_error_handle error =
      grpc_core::internal::TlsCheckHostName(target_name, &peer);
  tsi_peer_destruct(&peer);
  EXPECT_NE(error, GRPC_ERROR_NONE);
  GRPC_ERROR_UNREF(error);
}

// Tests for ServerSecurityConnector.
TEST_F(TlsSecurityConnectorTest,
       RootAndIdentityCertsObtainedWhenCreateServerSecurityConnector) {
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      grpc_core::MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, absl::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_0_);
  grpc_core::RefCountedPtr<::grpc_tls_certificate_provider> provider =
      grpc_core::MakeRefCounted<TlsTestCertificateProvider>(distributor);
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  options->set_certificate_provider(provider);
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  grpc_core::RefCountedPtr<TlsServerCredentials> credential =
      grpc_core::MakeRefCounted<TlsServerCredentials>(options);
  grpc_core::RefCountedPtr<grpc_server_security_connector> connector =
      credential->create_security_connector(nullptr);
  EXPECT_NE(connector, nullptr);
  grpc_core::TlsServerSecurityConnector* tls_connector =
      static_cast<grpc_core::TlsServerSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  distributor->SetKeyMaterials(kRootCertName, root_cert_1_, absl::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_1_);
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_1_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_1_);
}

// Note that on server side, we don't have tests watching root certs only,
// because in TLS, the identity certs should always be presented. If we don't
// provide, it will try to load certs from some default system locations, and
// will hence fail on some systems.
TEST_F(TlsSecurityConnectorTest,
       IdentityCertsObtainedWhenCreateServerSecurityConnector) {
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      grpc_core::MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, absl::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_0_);
  grpc_core::RefCountedPtr<::grpc_tls_certificate_provider> provider =
      grpc_core::MakeRefCounted<TlsTestCertificateProvider>(distributor);
  // Create options only watching for identity certificates.
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> identity_options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  identity_options->set_certificate_provider(provider);
  identity_options->set_watch_identity_pair(true);
  identity_options->set_identity_cert_name(kIdentityCertName);
  grpc_core::RefCountedPtr<TlsServerCredentials> identity_credential =
      grpc_core::MakeRefCounted<TlsServerCredentials>(identity_options);
  grpc_core::RefCountedPtr<grpc_server_security_connector> identity_connector =
      identity_credential->create_security_connector(nullptr);
  EXPECT_NE(identity_connector, nullptr);
  grpc_core::TlsServerSecurityConnector* tls_identity_connector =
      static_cast<grpc_core::TlsServerSecurityConnector*>(
          identity_connector.get());
  EXPECT_NE(tls_identity_connector->ServerHandshakerFactoryForTesting(),
            nullptr);
  EXPECT_EQ(tls_identity_connector->KeyCertPairListForTesting(),
            identity_pairs_0_);
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_1_);
  EXPECT_NE(tls_identity_connector->ServerHandshakerFactoryForTesting(),
            nullptr);
  EXPECT_EQ(tls_identity_connector->KeyCertPairListForTesting(),
            identity_pairs_1_);
}

TEST_F(TlsSecurityConnectorTest,
       CertPartiallyObtainedWhenCreateServerSecurityConnector) {
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      grpc_core::MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_0_);
  grpc_core::RefCountedPtr<::grpc_tls_certificate_provider> provider =
      grpc_core::MakeRefCounted<TlsTestCertificateProvider>(distributor);
  // Registered the options watching both certs, but only root certs are
  // available at distributor right now.
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  options->set_certificate_provider(provider);
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  grpc_core::RefCountedPtr<TlsServerCredentials> credential =
      grpc_core::MakeRefCounted<TlsServerCredentials>(options);
  grpc_core::RefCountedPtr<grpc_server_security_connector> connector =
      credential->create_security_connector(nullptr);
  EXPECT_NE(connector, nullptr);
  grpc_core::TlsServerSecurityConnector* tls_connector =
      static_cast<grpc_core::TlsServerSecurityConnector*>(connector.get());
  // The server_handshaker_factory_ shouldn't be updated.
  EXPECT_EQ(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  // After updating the root certs, the server_handshaker_factory_ should be
  // updated.
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, absl::nullopt);
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
}

TEST_F(TlsSecurityConnectorTest,
       DistributorHasErrorForServerSecurityConnector) {
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      grpc_core::MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, absl::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, absl::nullopt,
                               identity_pairs_0_);
  grpc_core::RefCountedPtr<::grpc_tls_certificate_provider> provider =
      grpc_core::MakeRefCounted<TlsTestCertificateProvider>(distributor);
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  options->set_certificate_provider(provider);
  options->set_watch_root_cert(true);
  options->set_watch_identity_pair(true);
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  grpc_core::RefCountedPtr<TlsServerCredentials> credential =
      grpc_core::MakeRefCounted<TlsServerCredentials>(options);
  grpc_core::RefCountedPtr<grpc_server_security_connector> connector =
      credential->create_security_connector(nullptr);
  EXPECT_NE(connector, nullptr);
  grpc_core::TlsServerSecurityConnector* tls_connector =
      static_cast<grpc_core::TlsServerSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  // Calling SetErrorForCert on distributor shouldn't invalidate the previous
  // valid credentials.
  distributor->SetErrorForCert(
      kRootCertName, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kErrorMessage),
      absl::nullopt);
  distributor->SetErrorForCert(
      kIdentityCertName, absl::nullopt,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(kErrorMessage));
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertsForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
}

TEST_F(TlsSecurityConnectorTest,
       CreateServerSecurityConnectorFailNoCredentials) {
  auto connector =
      grpc_core::TlsServerSecurityConnector::CreateTlsServerSecurityConnector(
          nullptr, grpc_core::MakeRefCounted<grpc_tls_credentials_options>());
  EXPECT_EQ(connector, nullptr);
}

TEST_F(TlsSecurityConnectorTest, CreateServerSecurityConnectorFailNoOptions) {
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options =
      grpc_core::MakeRefCounted<grpc_tls_credentials_options>();
  grpc_core::RefCountedPtr<TlsServerCredentials> credential =
      grpc_core::MakeRefCounted<TlsServerCredentials>(options);
  auto connector =
      grpc_core::TlsServerSecurityConnector::CreateTlsServerSecurityConnector(
          credential, nullptr);
  EXPECT_EQ(connector, nullptr);
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  GPR_GLOBAL_CONFIG_SET(grpc_default_ssl_roots_file_path, CA_CERT_PATH);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
