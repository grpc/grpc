//
//
// Copyright 2018 gRPC authors.
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

#include "src/core/credentials/transport/tls/tls_security_connector.h"

#include <grpc/credentials.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <openssl/x509.h>
#include <stdlib.h>
#include <string.h>

#include "src/core/config/config_vars.h"
#include "src/core/credentials/transport/tls/grpc_tls_certificate_provider.h"
#include "src/core/credentials/transport/tls/grpc_tls_credentials_options.h"
#include "src/core/credentials/transport/tls/tls_credentials.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/transport/auth_context.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security.h"
#include "src/core/util/crash.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/unique_type_name.h"
#include "test/core/test_util/test_call_creds.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define CLIENT_CERT_PATH "src/core/tsi/test_creds/multi-domain.pem"
#define SERVER_CERT_PATH_0 "src/core/tsi/test_creds/server0.pem"
#define SERVER_KEY_PATH_0 "src/core/tsi/test_creds/server0.key"
#define SERVER_CERT_PATH_1 "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH_1 "src/core/tsi/test_creds/server1.key"

namespace grpc_core {
namespace testing {
namespace {

constexpr const char* kRootCertName = "root_cert_name";
constexpr const char* kIdentityCertName = "identity_cert_name";
constexpr const char* kErrorMessage = "error_message";
constexpr const char* kTargetName = "foo.bar.com:443";
constexpr absl::string_view kSpiffeBundlePath0 =
    "test/core/credentials/transport/tls/test_data/spiffe/"
    "client_spiffebundle.json";
constexpr absl::string_view kSpiffeBundlePath1 =
    "test/core/credentials/transport/tls/test_data/spiffe/test_bundles/"
    "spiffebundle2.json";
// This test only checks the non-empty crl_directory path, not filesystem CRL
// loading.
constexpr const char* kFakeCrlDirectory = "/nonexistent/crl/dir";

long GetVerificationFlags(X509_STORE* store) {
#if OPENSSL_VERSION_NUMBER >= 0x10100000
  return X509_VERIFY_PARAM_get_flags(X509_STORE_get0_param(store));
#else
  return X509_VERIFY_PARAM_get_flags(store->param);
#endif
}

}  // namespace

class TlsSecurityConnectorTest : public ::testing::Test {
 protected:
  TlsSecurityConnectorTest() {}

  void SetUp() override {
    root_cert_1_ = std::make_shared<tsi::RootCertInfo>(
        testing::GetFileContents(CA_CERT_PATH));
    root_cert_0_ = std::make_shared<tsi::RootCertInfo>(
        testing::GetFileContents(CLIENT_CERT_PATH));
    identity_pairs_1_.emplace_back(
        testing::GetFileContents(SERVER_KEY_PATH_1),
        testing::GetFileContents(SERVER_CERT_PATH_1));
    identity_pairs_0_.emplace_back(
        testing::GetFileContents(SERVER_KEY_PATH_0),
        testing::GetFileContents(SERVER_CERT_PATH_0));
    auto map0 = SpiffeBundleMap::FromFile(kSpiffeBundlePath0);
    GRPC_CHECK(map0.ok());
    spiffe_bundle_map_0_ =
        std::make_shared<tsi::RootCertInfo>(std::move(*map0));
    auto map1 = SpiffeBundleMap::FromFile(kSpiffeBundlePath1);
    GRPC_CHECK(map1.ok());
    spiffe_bundle_map_1_ =
        std::make_shared<tsi::RootCertInfo>(std::move(*map1));
  }

  static void VerifyExpectedErrorCallback(void* arg, grpc_error_handle error) {
    const char* expected_error_msg = static_cast<const char*>(arg);
    if (expected_error_msg == nullptr) {
      EXPECT_EQ(error, absl::OkStatus());
    } else {
      EXPECT_EQ(error.message(), expected_error_msg);
    }
  }

  std::shared_ptr<tsi::RootCertInfo> root_cert_1_;
  std::shared_ptr<tsi::RootCertInfo> root_cert_0_;
  PemKeyCertPairList identity_pairs_1_;
  PemKeyCertPairList identity_pairs_0_;
  std::shared_ptr<tsi::RootCertInfo> spiffe_bundle_map_0_;
  std::shared_ptr<tsi::RootCertInfo> spiffe_bundle_map_1_;
  HostNameCertificateVerifier hostname_certificate_verifier_;
};

class TlsTestCertificateProvider : public grpc_tls_certificate_provider {
 public:
  explicit TlsTestCertificateProvider(
      RefCountedPtr<grpc_tls_certificate_distributor> distributor)
      : distributor_(std::move(distributor)) {}
  ~TlsTestCertificateProvider() override {}
  RefCountedPtr<grpc_tls_certificate_distributor> distributor() const override {
    return distributor_;
  }

  UniqueTypeName type() const override {
    static UniqueTypeName::Factory kFactory("tls_test");
    return kFactory.Create();
  }

 private:
  int CompareImpl(const grpc_tls_certificate_provider* other) const override {
    // TODO(yashykt): Maybe do something better here.
    return QsortCompare(static_cast<const grpc_tls_certificate_provider*>(this),
                        other);
  }

  RefCountedPtr<grpc_tls_certificate_distributor> distributor_;
};

//
// Tests for Certificate Providers in ChannelSecurityConnector.
//

TEST_F(TlsSecurityConnectorTest,
       RootAndIdentityCertsObtainedWhenCreateChannelSecurityConnector) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, std::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, nullptr, identity_pairs_0_);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_root_certificate_provider(provider);
  options->set_identity_certificate_provider(std::move(provider));
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  ChannelArgs new_args;
  RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, &new_args);
  EXPECT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertInfoForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  distributor->SetKeyMaterials(kRootCertName, root_cert_1_, std::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, nullptr, identity_pairs_1_);
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertInfoForTesting(), root_cert_1_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_1_);
}

TEST_F(TlsSecurityConnectorTest,
       SystemRootsWhenCreateChannelSecurityConnector) {
  // Create options watching for no certificates.
  RefCountedPtr<grpc_tls_credentials_options> root_options =
      MakeRefCounted<grpc_tls_credentials_options>();
  RefCountedPtr<TlsCredentials> root_credential =
      MakeRefCounted<TlsCredentials>(root_options);
  ChannelArgs root_new_args;
  RefCountedPtr<grpc_channel_security_connector> root_connector =
      root_credential->create_security_connector(nullptr, "some_target",
                                                 &root_new_args);
  EXPECT_NE(root_connector, nullptr);
  TlsChannelSecurityConnector* tls_root_connector =
      static_cast<TlsChannelSecurityConnector*>(root_connector.get());
  EXPECT_NE(tls_root_connector->ClientHandshakerFactoryForTesting(), nullptr);
}

TEST_F(TlsSecurityConnectorTest,
       SystemRootsAndIdentityCertsObtainedWhenCreateChannelSecurityConnector) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kIdentityCertName, nullptr, identity_pairs_0_);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  // Create options only watching for identity certificates.
  RefCountedPtr<grpc_tls_credentials_options> root_options =
      MakeRefCounted<grpc_tls_credentials_options>();
  root_options->set_identity_certificate_provider(std::move(provider));
  root_options->set_identity_cert_name(kIdentityCertName);
  RefCountedPtr<TlsCredentials> root_credential =
      MakeRefCounted<TlsCredentials>(root_options);
  ChannelArgs root_new_args;
  RefCountedPtr<grpc_channel_security_connector> root_connector =
      root_credential->create_security_connector(nullptr, "some_target",
                                                 &root_new_args);
  EXPECT_NE(root_connector, nullptr);
  TlsChannelSecurityConnector* tls_root_connector =
      static_cast<TlsChannelSecurityConnector*>(root_connector.get());
  EXPECT_NE(tls_root_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_root_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  // If we have a root update, we shouldn't receive them in security connector,
  // since we claimed to use default system roots.
  distributor->SetKeyMaterials(kRootCertName, root_cert_1_, std::nullopt);
  EXPECT_NE(tls_root_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_NE(tls_root_connector->RootCertInfoForTesting(), root_cert_1_);
}

TEST_F(TlsSecurityConnectorTest,
       RootCertsObtainedWhenCreateChannelSecurityConnector) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, std::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, nullptr, identity_pairs_0_);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  // Create options only watching for root certificates.
  RefCountedPtr<grpc_tls_credentials_options> root_options =
      MakeRefCounted<grpc_tls_credentials_options>();
  root_options->set_root_certificate_provider(std::move(provider));
  root_options->set_root_cert_name(kRootCertName);
  RefCountedPtr<TlsCredentials> root_credential =
      MakeRefCounted<TlsCredentials>(root_options);
  ChannelArgs root_new_args;
  RefCountedPtr<grpc_channel_security_connector> root_connector =
      root_credential->create_security_connector(nullptr, "some_target",
                                                 &root_new_args);
  EXPECT_NE(root_connector, nullptr);
  TlsChannelSecurityConnector* tls_root_connector =
      static_cast<TlsChannelSecurityConnector*>(root_connector.get());
  EXPECT_NE(tls_root_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_root_connector->RootCertInfoForTesting(), root_cert_0_);
  distributor->SetKeyMaterials(kRootCertName, root_cert_1_, std::nullopt);
  EXPECT_NE(tls_root_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_root_connector->RootCertInfoForTesting(), root_cert_1_);
}

TEST_F(TlsSecurityConnectorTest,
       CertPartiallyObtainedWhenCreateChannelSecurityConnector) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, std::nullopt);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  // Registered the options watching both certs, but only root certs are
  // available at distributor right now.
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_root_certificate_provider(provider);
  options->set_identity_certificate_provider(std::move(provider));
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  ChannelArgs new_args;
  RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, &new_args);
  EXPECT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  // The client_handshaker_factory_ shouldn't be updated.
  EXPECT_EQ(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertInfoForTesting(), root_cert_0_);
  // After updating the root certs, the client_handshaker_factory_ should be
  // updated.
  distributor->SetKeyMaterials(kIdentityCertName, nullptr, identity_pairs_0_);
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertInfoForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
}

TEST_F(TlsSecurityConnectorTest,
       DistributorHasErrorForChannelSecurityConnector) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, std::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, nullptr, identity_pairs_0_);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_root_certificate_provider(provider);
  options->set_identity_certificate_provider(std::move(provider));
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  ChannelArgs new_args;
  RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, &new_args);
  EXPECT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertInfoForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  // Calling SetErrorForCert on distributor shouldn't invalidate the previous
  // valid credentials.
  distributor->SetErrorForCert(kRootCertName, GRPC_ERROR_CREATE(kErrorMessage),
                               std::nullopt);
  distributor->SetErrorForCert(kIdentityCertName, std::nullopt,
                               GRPC_ERROR_CREATE(kErrorMessage));
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertInfoForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
}

TEST_F(TlsSecurityConnectorTest,
       CreateChannelSecurityConnectorFailNoTargetName) {
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  ChannelArgs new_args;
  RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, nullptr, &new_args);
  EXPECT_EQ(connector, nullptr);
}

TEST_F(TlsSecurityConnectorTest,
       CreateChannelSecurityConnectorFailNoCredentials) {
  auto connector =
      TlsChannelSecurityConnector::CreateTlsChannelSecurityConnector(
          nullptr, MakeRefCounted<grpc_tls_credentials_options>(), nullptr,
          kTargetName, nullptr, nullptr);
  EXPECT_EQ(connector, nullptr);
}

TEST_F(TlsSecurityConnectorTest, CreateChannelSecurityConnectorFailNoOptions) {
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  auto connector =
      TlsChannelSecurityConnector::CreateTlsChannelSecurityConnector(
          credential, nullptr, nullptr, kTargetName, nullptr, nullptr);
  EXPECT_EQ(connector, nullptr);
}

//
// Tests for Certificate Verifier in ChannelSecurityConnector.
//

TEST_F(TlsSecurityConnectorTest,
       ChannelSecurityConnectorWithSyncExternalVerifierSucceeds) {
  auto* sync_verifier_ = new SyncExternalVerifier(true);
  ExternalCertificateVerifier core_external_verifier(sync_verifier_->base());
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_verify_server_cert(true);
  options->set_certificate_verifier(core_external_verifier.Ref());
  options->set_check_call_host(false);
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  ChannelArgs new_args;
  RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, &new_args);
  EXPECT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  // Construct a basic TSI Peer.
  tsi_peer peer;
  GRPC_CHECK(tsi_construct_peer(2, &peer) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property(TSI_SSL_ALPN_SELECTED_PROTOCOL,
                                                "h2", strlen("h2"),
                                                &peer.properties[0]) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.bar.com",
                 &peer.properties[1]) == TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context;
  ExecCtx exec_ctx;
  grpc_closure* on_peer_checked = GRPC_CLOSURE_CREATE(
      VerifyExpectedErrorCallback, nullptr, grpc_schedule_on_exec_ctx);
  ChannelArgs args;
  tls_connector->check_peer(peer, nullptr, args, &auth_context,
                            on_peer_checked);
}

TEST_F(TlsSecurityConnectorTest,
       ChannelSecurityConnectorWithSyncExternalVerifierFails) {
  auto* sync_verifier_ = new SyncExternalVerifier(false);
  ExternalCertificateVerifier core_external_verifier(sync_verifier_->base());
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_verify_server_cert(true);
  options->set_certificate_verifier(core_external_verifier.Ref());
  options->set_check_call_host(false);
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  ChannelArgs new_args;
  RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, &new_args);
  EXPECT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  // Construct a basic TSI Peer.
  tsi_peer peer;
  GRPC_CHECK(tsi_construct_peer(2, &peer) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property(TSI_SSL_ALPN_SELECTED_PROTOCOL,
                                                "h2", strlen("h2"),
                                                &peer.properties[0]) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.bar.com",
                 &peer.properties[1]) == TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context;
  const char* expected_error_msg =
      "Custom verification check failed with error: UNAUTHENTICATED: "
      "SyncExternalVerifier failed";
  ExecCtx exec_ctx;
  grpc_closure* on_peer_checked = GRPC_CLOSURE_CREATE(
      VerifyExpectedErrorCallback, const_cast<char*>(expected_error_msg),
      grpc_schedule_on_exec_ctx);
  tls_connector->check_peer(peer, nullptr, new_args, &auth_context,
                            on_peer_checked);
}

TEST_F(TlsSecurityConnectorTest,
       CompareChannelSecurityConnectorSucceedsOnSameCredentials) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, std::nullopt);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  options->set_root_certificate_provider(std::move(provider));
  options->set_root_cert_name(kRootCertName);
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  ChannelArgs connector_args;
  ChannelArgs other_connector_args;
  RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName,
                                            &connector_args);
  RefCountedPtr<grpc_channel_security_connector> other_connector =
      credential->create_security_connector(nullptr, kTargetName,
                                            &other_connector_args);
  // Comparing the equality of security connectors generated from the same
  // channel credentials with same settings should succeed.
  EXPECT_EQ(connector->cmp(other_connector.get()), 0);
}

TEST_F(TlsSecurityConnectorTest,
       CompareChannelSecurityConnectorFailsOnDifferentChannelCredentials) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, std::nullopt);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  options->set_root_certificate_provider(provider);
  options->set_root_cert_name(kRootCertName);
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  ChannelArgs connector_args;
  RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName,
                                            &connector_args);
  auto other_options = MakeRefCounted<grpc_tls_credentials_options>();
  other_options->set_root_certificate_provider(provider);
  other_options->set_identity_certificate_provider(std::move(provider));
  other_options->set_root_cert_name(kRootCertName);
  other_options->set_identity_cert_name(kIdentityCertName);
  RefCountedPtr<TlsCredentials> other_credential =
      MakeRefCounted<TlsCredentials>(other_options);
  ChannelArgs other_connector_args;
  RefCountedPtr<grpc_channel_security_connector> other_connector =
      other_credential->create_security_connector(nullptr, kTargetName,
                                                  &other_connector_args);
  // Comparing the equality of security connectors generated from different
  // channel credentials should fail.
  EXPECT_NE(connector->cmp(other_connector.get()), 0);
}

TEST_F(TlsSecurityConnectorTest,
       CompareChannelSecurityConnectorFailsOnDifferentCallCredentials) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, std::nullopt);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  options->set_root_certificate_provider(std::move(provider));
  options->set_root_cert_name(kRootCertName);
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  ChannelArgs connector_args;
  RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName,
                                            &connector_args);
  grpc_call_credentials* call_creds =
      grpc_md_only_test_credentials_create("", "");
  ChannelArgs other_connector_args;
  RefCountedPtr<grpc_channel_security_connector> other_connector =
      credential->create_security_connector(
          RefCountedPtr<grpc_call_credentials>(call_creds), kTargetName,
          &other_connector_args);
  // Comparing the equality of security connectors generated with different call
  // credentials should fail.
  EXPECT_NE(connector->cmp(other_connector.get()), 0);
}

TEST_F(TlsSecurityConnectorTest,
       CompareChannelSecurityConnectorFailsOnDifferentTargetNames) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, std::nullopt);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  options->set_root_certificate_provider(std::move(provider));
  options->set_root_cert_name(kRootCertName);
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  ChannelArgs connector_args;
  ChannelArgs other_connector_args;
  RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName,
                                            &connector_args);
  RefCountedPtr<grpc_channel_security_connector> other_connector =
      credential->create_security_connector(nullptr, "", &other_connector_args);
  // Comparing the equality of security connectors generated with different
  // target names should fail.
  EXPECT_NE(connector->cmp(other_connector.get()), 0);
}

// Tests for ServerSecurityConnector.
TEST_F(TlsSecurityConnectorTest,
       ChannelSecurityConnectorWithAsyncExternalVerifierSucceeds) {
  auto* async_verifier = new AsyncExternalVerifier(true);
  auto* core_external_verifier =
      new ExternalCertificateVerifier(async_verifier->base());
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  options->set_verify_server_cert(true);
  options->set_certificate_verifier(core_external_verifier->Ref());
  options->set_check_call_host(false);
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  ChannelArgs new_args;
  RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, &new_args);
  EXPECT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  // Construct a basic TSI Peer.
  tsi_peer peer;
  GRPC_CHECK(tsi_construct_peer(2, &peer) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property(TSI_SSL_ALPN_SELECTED_PROTOCOL,
                                                "h2", strlen("h2"),
                                                &peer.properties[0]) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.bar.com",
                 &peer.properties[1]) == TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context;
  ExecCtx exec_ctx;
  grpc_closure* on_peer_checked = GRPC_CLOSURE_CREATE(
      VerifyExpectedErrorCallback, nullptr, grpc_schedule_on_exec_ctx);
  tls_connector->check_peer(peer, nullptr, new_args, &auth_context,
                            on_peer_checked);
  core_external_verifier->Unref();
}

TEST_F(TlsSecurityConnectorTest,
       ChannelSecurityConnectorWithAsyncExternalVerifierFails) {
  auto* async_verifier = new AsyncExternalVerifier(false);
  auto* core_external_verifier =
      new ExternalCertificateVerifier(async_verifier->base());
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  options->set_verify_server_cert(true);
  options->set_certificate_verifier(core_external_verifier->Ref());
  options->set_check_call_host(false);
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  ChannelArgs new_args;
  RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, &new_args);
  EXPECT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  // Construct a basic TSI Peer.
  tsi_peer peer;
  GRPC_CHECK(tsi_construct_peer(2, &peer) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property(TSI_SSL_ALPN_SELECTED_PROTOCOL,
                                                "h2", strlen("h2"),
                                                &peer.properties[0]) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.bar.com",
                 &peer.properties[1]) == TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context;
  const char* expected_error_msg =
      "Custom verification check failed with error: UNAUTHENTICATED: "
      "AsyncExternalVerifier failed";
  ExecCtx exec_ctx;
  grpc_closure* on_peer_checked = GRPC_CLOSURE_CREATE(
      VerifyExpectedErrorCallback, const_cast<char*>(expected_error_msg),
      grpc_schedule_on_exec_ctx);
  tls_connector->check_peer(peer, nullptr, new_args, &auth_context,
                            on_peer_checked);
  core_external_verifier->Unref();
}

TEST_F(TlsSecurityConnectorTest,
       ChannelSecurityConnectorHostnameVerifierSucceeds) {
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_verify_server_cert(true);
  options->set_certificate_verifier(hostname_certificate_verifier_.Ref());
  options->set_check_call_host(false);
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  ChannelArgs new_args;
  RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, &new_args);
  EXPECT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  // Construct a full TSI Peer.
  tsi_peer peer;
  GRPC_CHECK(tsi_construct_peer(7, &peer) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property(TSI_SSL_ALPN_SELECTED_PROTOCOL,
                                                "h2", strlen("h2"),
                                                &peer.properties[0]) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.bar.com",
                 &peer.properties[1]) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_PEM_CERT_PROPERTY, "pem_cert", &peer.properties[2]) ==
             TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property_from_cstring(
                 TSI_SECURITY_LEVEL_PEER_PROPERTY,
                 tsi_security_level_to_string(TSI_PRIVACY_AND_INTEGRITY),
                 &peer.properties[3]) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_PEM_CERT_CHAIN_PROPERTY, "pem_cert_chain",
                 &peer.properties[4]) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY, "foo.bar.com",
                 &peer.properties[5]) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY, "foo.baz.com",
                 &peer.properties[6]) == TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context;
  ExecCtx exec_ctx;
  grpc_closure* on_peer_checked = GRPC_CLOSURE_CREATE(
      VerifyExpectedErrorCallback, nullptr, grpc_schedule_on_exec_ctx);
  tls_connector->check_peer(peer, nullptr, new_args, &auth_context,
                            on_peer_checked);
}

TEST_F(TlsSecurityConnectorTest,
       ChannelSecurityConnectorHostnameVerifierFails) {
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_verify_server_cert(true);
  options->set_certificate_verifier(hostname_certificate_verifier_.Ref());
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  ChannelArgs new_args;
  RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, &new_args);
  EXPECT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  // Construct a full TSI Peer.
  tsi_peer peer;
  GRPC_CHECK(tsi_construct_peer(7, &peer) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property(TSI_SSL_ALPN_SELECTED_PROTOCOL,
                                                "h2", strlen("h2"),
                                                &peer.properties[0]) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.com",
                 &peer.properties[1]) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_PEM_CERT_PROPERTY, "pem_cert", &peer.properties[2]) ==
             TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property_from_cstring(
                 TSI_SECURITY_LEVEL_PEER_PROPERTY,
                 tsi_security_level_to_string(TSI_PRIVACY_AND_INTEGRITY),
                 &peer.properties[3]) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_PEM_CERT_CHAIN_PROPERTY, "pem_cert_chain",
                 &peer.properties[4]) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY, "*.com",
                 &peer.properties[5]) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY, "foo.baz.com",
                 &peer.properties[6]) == TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context;
  const char* expected_error_msg =
      "Custom verification check failed with error: UNAUTHENTICATED: Hostname "
      "Verification "
      "Check failed.";
  ExecCtx exec_ctx;
  grpc_closure* on_peer_checked = GRPC_CLOSURE_CREATE(
      VerifyExpectedErrorCallback, const_cast<char*>(expected_error_msg),
      grpc_schedule_on_exec_ctx);
  tls_connector->check_peer(peer, nullptr, new_args, &auth_context,
                            on_peer_checked);
}

TEST_F(TlsSecurityConnectorTest,
       ChannelSecurityConnectorWithVerifiedRootCertSubjectSucceeds) {
  auto* sync_verifier = new SyncExternalVerifier(true);
  ExternalCertificateVerifier core_external_verifier(sync_verifier->base());
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_verify_server_cert(true);
  options->set_certificate_verifier(core_external_verifier.Ref());
  options->set_check_call_host(false);
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  ChannelArgs new_args;
  RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, &new_args);
  EXPECT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  // Construct a basic TSI Peer.
  std::string expected_subject =
      "CN=testca,O=Internet Widgits Pty Ltd,ST=Some-State,C=AU";
  tsi_peer peer;
  EXPECT_EQ(tsi_construct_peer(2, &peer), TSI_OK);
  EXPECT_EQ(
      tsi_construct_string_peer_property(TSI_SSL_ALPN_SELECTED_PROTOCOL, "h2",
                                         strlen("h2"), &peer.properties[0]),
      TSI_OK);
  EXPECT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_VERIFIED_ROOT_CERT_SUBECT_PEER_PROPERTY,
                expected_subject.c_str(), &peer.properties[1]),
            TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context;
  ExecCtx exec_ctx;
  grpc_closure* on_peer_checked = GRPC_CLOSURE_CREATE(
      VerifyExpectedErrorCallback, nullptr, grpc_schedule_on_exec_ctx);
  ChannelArgs args;
  tls_connector->check_peer(peer, nullptr, args, &auth_context,
                            on_peer_checked);
}

//
// Tests for Certificate Providers in ServerSecurityConnector.
//

TEST_F(TlsSecurityConnectorTest,
       RootAndIdentityCertsObtainedWhenCreateServerSecurityConnector) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, std::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, nullptr, identity_pairs_0_);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_root_certificate_provider(provider);
  options->set_identity_certificate_provider(std::move(provider));
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  RefCountedPtr<TlsServerCredentials> credential =
      MakeRefCounted<TlsServerCredentials>(options);
  RefCountedPtr<grpc_server_security_connector> connector =
      credential->create_security_connector(ChannelArgs());
  EXPECT_NE(connector, nullptr);
  TlsServerSecurityConnector* tls_connector =
      static_cast<TlsServerSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertInfoForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  distributor->SetKeyMaterials(kRootCertName, root_cert_1_, std::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, nullptr, identity_pairs_1_);
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertInfoForTesting(), root_cert_1_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_1_);
}

// Note that on server side, we don't have tests watching root certs only,
// because in TLS, the identity certs should always be presented. If we don't
// provide, it will try to load certs from some default system locations, and
// will hence fail on some systems.
TEST_F(TlsSecurityConnectorTest,
       IdentityCertsObtainedWhenCreateServerSecurityConnector) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, std::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, nullptr, identity_pairs_0_);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  // Create options only watching for identity certificates.
  RefCountedPtr<grpc_tls_credentials_options> identity_options =
      MakeRefCounted<grpc_tls_credentials_options>();
  identity_options->set_identity_certificate_provider(std::move(provider));
  identity_options->set_identity_cert_name(kIdentityCertName);
  RefCountedPtr<TlsServerCredentials> identity_credential =
      MakeRefCounted<TlsServerCredentials>(identity_options);
  RefCountedPtr<grpc_server_security_connector> identity_connector =
      identity_credential->create_security_connector(ChannelArgs());
  EXPECT_NE(identity_connector, nullptr);
  TlsServerSecurityConnector* tls_identity_connector =
      static_cast<TlsServerSecurityConnector*>(identity_connector.get());
  EXPECT_NE(tls_identity_connector->ServerHandshakerFactoryForTesting(),
            nullptr);
  EXPECT_EQ(tls_identity_connector->KeyCertPairListForTesting(),
            identity_pairs_0_);
  distributor->SetKeyMaterials(kIdentityCertName, nullptr, identity_pairs_1_);
  EXPECT_NE(tls_identity_connector->ServerHandshakerFactoryForTesting(),
            nullptr);
  EXPECT_EQ(tls_identity_connector->KeyCertPairListForTesting(),
            identity_pairs_1_);
}

TEST_F(TlsSecurityConnectorTest,
       CertPartiallyObtainedWhenCreateServerSecurityConnector) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kIdentityCertName, nullptr, identity_pairs_0_);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  // Registered the options watching both certs, but only root certs are
  // available at distributor right now.
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_root_certificate_provider(provider);
  options->set_identity_certificate_provider(std::move(provider));
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  RefCountedPtr<TlsServerCredentials> credential =
      MakeRefCounted<TlsServerCredentials>(options);
  RefCountedPtr<grpc_server_security_connector> connector =
      credential->create_security_connector(ChannelArgs());
  EXPECT_NE(connector, nullptr);
  TlsServerSecurityConnector* tls_connector =
      static_cast<TlsServerSecurityConnector*>(connector.get());
  // The server_handshaker_factory_ shouldn't be updated.
  EXPECT_EQ(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  // After updating the root certs, the server_handshaker_factory_ should be
  // updated.
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, std::nullopt);
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertInfoForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
}

TEST_F(TlsSecurityConnectorTest,
       DistributorHasErrorForServerSecurityConnector) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, std::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, nullptr, identity_pairs_0_);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_root_certificate_provider(provider);
  options->set_identity_certificate_provider(std::move(provider));
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  RefCountedPtr<TlsServerCredentials> credential =
      MakeRefCounted<TlsServerCredentials>(options);
  RefCountedPtr<grpc_server_security_connector> connector =
      credential->create_security_connector(ChannelArgs());
  EXPECT_NE(connector, nullptr);
  TlsServerSecurityConnector* tls_connector =
      static_cast<TlsServerSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertInfoForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  // Calling SetErrorForCert on distributor shouldn't invalidate the previous
  // valid credentials.
  distributor->SetErrorForCert(kRootCertName, GRPC_ERROR_CREATE(kErrorMessage),
                               std::nullopt);
  distributor->SetErrorForCert(kIdentityCertName, std::nullopt,
                               GRPC_ERROR_CREATE(kErrorMessage));
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertInfoForTesting(), root_cert_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
}

TEST_F(TlsSecurityConnectorTest,
       CreateServerSecurityConnectorFailNoCredentials) {
  auto connector = TlsServerSecurityConnector::CreateTlsServerSecurityConnector(
      nullptr, MakeRefCounted<grpc_tls_credentials_options>());
  EXPECT_EQ(connector, nullptr);
}

TEST_F(TlsSecurityConnectorTest, CreateServerSecurityConnectorFailNoOptions) {
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  RefCountedPtr<TlsServerCredentials> credential =
      MakeRefCounted<TlsServerCredentials>(options);
  auto connector = TlsServerSecurityConnector::CreateTlsServerSecurityConnector(
      credential, nullptr);
  EXPECT_EQ(connector, nullptr);
}

TEST_F(TlsSecurityConnectorTest,
       CompareServerSecurityConnectorSucceedsOnSameCredentials) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kIdentityCertName, nullptr, identity_pairs_0_);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  options->set_identity_certificate_provider(std::move(provider));
  options->set_identity_cert_name(kIdentityCertName);
  RefCountedPtr<TlsServerCredentials> credential =
      MakeRefCounted<TlsServerCredentials>(options);
  RefCountedPtr<grpc_server_security_connector> connector =
      credential->create_security_connector(ChannelArgs());
  RefCountedPtr<grpc_server_security_connector> other_connector =
      credential->create_security_connector(ChannelArgs());
  // Comparing the equality of security connectors generated from the same
  // server credentials with same settings should succeed.
  EXPECT_EQ(connector->cmp(other_connector.get()), 0);
}

TEST_F(TlsSecurityConnectorTest,
       CompareServerSecurityConnectorFailsOnDifferentServerCredentials) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kIdentityCertName, nullptr, identity_pairs_0_);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  options->set_identity_certificate_provider(std::move(provider));
  options->set_identity_cert_name(kIdentityCertName);
  RefCountedPtr<TlsServerCredentials> credential =
      MakeRefCounted<TlsServerCredentials>(options);
  RefCountedPtr<grpc_server_security_connector> connector =
      credential->create_security_connector(ChannelArgs());
  RefCountedPtr<TlsServerCredentials> other_credential =
      MakeRefCounted<TlsServerCredentials>(options);
  RefCountedPtr<grpc_server_security_connector> other_connector =
      other_credential->create_security_connector(ChannelArgs());
  // Comparing the equality of security connectors generated from different
  // server credentials should fail.
  EXPECT_NE(connector->cmp(other_connector.get()), 0);
}

//
// Tests for Certificate Verifier in ServerSecurityConnector.
//

TEST_F(TlsSecurityConnectorTest,
       ServerSecurityConnectorWithSyncExternalVerifierSucceeds) {
  auto* sync_verifier = new SyncExternalVerifier(true);
  ExternalCertificateVerifier core_external_verifier(sync_verifier->base());
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_cert_request_type(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
  options->set_certificate_verifier(core_external_verifier.Ref());
  auto provider = MakeRefCounted<InMemoryCertificateProvider>();
  options->set_identity_certificate_provider(std::move(provider));
  auto credentials = MakeRefCounted<TlsServerCredentials>(options);
  auto connector = credentials->create_security_connector(ChannelArgs());
  // Construct a basic TSI Peer.
  tsi_peer peer;
  GRPC_CHECK(tsi_construct_peer(2, &peer) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property(TSI_SSL_ALPN_SELECTED_PROTOCOL,
                                                "h2", strlen("h2"),
                                                &peer.properties[0]) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.bar.com",
                 &peer.properties[1]) == TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context;
  ExecCtx exec_ctx;
  grpc_closure* on_peer_checked = GRPC_CLOSURE_CREATE(
      VerifyExpectedErrorCallback, nullptr, grpc_schedule_on_exec_ctx);
  ChannelArgs args;
  connector->check_peer(peer, nullptr, args, &auth_context, on_peer_checked);
}

TEST_F(TlsSecurityConnectorTest,
       ServerSecurityConnectorWithSyncExternalVerifierFails) {
  auto* sync_verifier = new SyncExternalVerifier(false);
  ExternalCertificateVerifier core_external_verifier(sync_verifier->base());
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_cert_request_type(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
  options->set_certificate_verifier(core_external_verifier.Ref());
  auto provider = MakeRefCounted<InMemoryCertificateProvider>();
  options->set_identity_certificate_provider(std::move(provider));
  auto credentials = MakeRefCounted<TlsServerCredentials>(options);
  auto connector = credentials->create_security_connector(ChannelArgs());
  // Construct a basic TSI Peer.
  tsi_peer peer;
  GRPC_CHECK(tsi_construct_peer(2, &peer) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property(TSI_SSL_ALPN_SELECTED_PROTOCOL,
                                                "h2", strlen("h2"),
                                                &peer.properties[0]) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.bar.com",
                 &peer.properties[1]) == TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context;
  const char* expected_error_msg =
      "Custom verification check failed with error: UNAUTHENTICATED: "
      "SyncExternalVerifier failed";
  ExecCtx exec_ctx;
  grpc_closure* on_peer_checked = GRPC_CLOSURE_CREATE(
      VerifyExpectedErrorCallback, const_cast<char*>(expected_error_msg),
      grpc_schedule_on_exec_ctx);
  ChannelArgs args;
  connector->check_peer(peer, nullptr, args, &auth_context, on_peer_checked);
}

TEST_F(TlsSecurityConnectorTest,
       ServerSecurityConnectorWithAsyncExternalVerifierSucceeds) {
  auto* async_verifier = new AsyncExternalVerifier(true);
  auto* core_external_verifier =
      new ExternalCertificateVerifier(async_verifier->base());
  auto options = MakeRefCounted<grpc_tls_credentials_options>();
  options->set_cert_request_type(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
  options->set_certificate_verifier(core_external_verifier->Ref());
  auto provider = MakeRefCounted<InMemoryCertificateProvider>();
  options->set_identity_certificate_provider(std::move(provider));
  auto credentials = MakeRefCounted<TlsServerCredentials>(options);
  auto connector = credentials->create_security_connector(ChannelArgs());
  // Construct a basic TSI Peer.
  tsi_peer peer;
  GRPC_CHECK(tsi_construct_peer(2, &peer) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property(TSI_SSL_ALPN_SELECTED_PROTOCOL,
                                                "h2", strlen("h2"),
                                                &peer.properties[0]) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.bar.com",
                 &peer.properties[1]) == TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context;
  ExecCtx exec_ctx;
  grpc_closure* on_peer_checked = GRPC_CLOSURE_CREATE(
      VerifyExpectedErrorCallback, nullptr, grpc_schedule_on_exec_ctx);
  ChannelArgs args;
  connector->check_peer(peer, nullptr, args, &auth_context, on_peer_checked);
  core_external_verifier->Unref();
}

TEST_F(TlsSecurityConnectorTest,
       ServerSecurityConnectorWithAsyncExternalVerifierFails) {
  auto* async_verifier = new AsyncExternalVerifier(false);
  auto* core_external_verifier =
      new ExternalCertificateVerifier(async_verifier->base());
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_cert_request_type(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
  options->set_certificate_verifier(core_external_verifier->Ref());
  auto provider = MakeRefCounted<InMemoryCertificateProvider>();
  options->set_identity_certificate_provider(std::move(provider));
  auto credentials = MakeRefCounted<TlsServerCredentials>(options);
  auto connector = credentials->create_security_connector(ChannelArgs());
  // Construct a basic TSI Peer.
  tsi_peer peer;
  GRPC_CHECK(tsi_construct_peer(2, &peer) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property(TSI_SSL_ALPN_SELECTED_PROTOCOL,
                                                "h2", strlen("h2"),
                                                &peer.properties[0]) == TSI_OK);
  GRPC_CHECK(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.bar.com",
                 &peer.properties[1]) == TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context;
  const char* expected_error_msg =
      "Custom verification check failed with error: UNAUTHENTICATED: "
      "AsyncExternalVerifier failed";
  ExecCtx exec_ctx;
  grpc_closure* on_peer_checked = GRPC_CLOSURE_CREATE(
      VerifyExpectedErrorCallback, const_cast<char*>(expected_error_msg),
      grpc_schedule_on_exec_ctx);
  ChannelArgs args;
  connector->check_peer(peer, nullptr, args, &auth_context, on_peer_checked);
  core_external_verifier->Unref();
}

TEST_F(TlsSecurityConnectorTest,
       ServerSecurityConnectorWithVerifiedRootSubjectCertSucceeds) {
  auto* sync_verifier = new SyncExternalVerifier(true);
  ExternalCertificateVerifier core_external_verifier(sync_verifier->base());
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_cert_request_type(
      GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  options->set_certificate_verifier(core_external_verifier.Ref());
  auto provider = MakeRefCounted<InMemoryCertificateProvider>();
  options->set_identity_certificate_provider(std::move(provider));
  auto credentials = MakeRefCounted<TlsServerCredentials>(options);
  auto connector = credentials->create_security_connector(ChannelArgs());
  // Construct a basic TSI Peer.
  std::string expected_subject =
      "CN=testca,O=Internet Widgits Pty Ltd,ST=Some-State,C=AU";
  tsi_peer peer;
  EXPECT_EQ(tsi_construct_peer(2, &peer), TSI_OK);
  EXPECT_EQ(
      tsi_construct_string_peer_property(TSI_SSL_ALPN_SELECTED_PROTOCOL, "h2",
                                         strlen("h2"), &peer.properties[0]),
      TSI_OK);
  EXPECT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_VERIFIED_ROOT_CERT_SUBECT_PEER_PROPERTY,
                expected_subject.c_str(), &peer.properties[1]),
            TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context;
  ExecCtx exec_ctx;
  grpc_closure* on_peer_checked = GRPC_CLOSURE_CREATE(
      VerifyExpectedErrorCallback, nullptr, grpc_schedule_on_exec_ctx);
  ChannelArgs args;
  connector->check_peer(peer, nullptr, args, &auth_context, on_peer_checked);
}

// TODO(gtcooke94): Enable when support for SPIFFE verification is complete.
TEST_F(
    TlsSecurityConnectorTest,
    DISABLED_SpiffeRootAndIdentityCertsObtainedWhenCreateChannelSecurityConnector) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, spiffe_bundle_map_0_,
                               std::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, nullptr, identity_pairs_0_);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_root_certificate_provider(provider);
  options->set_identity_certificate_provider(std::move(provider));
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  ChannelArgs new_args;
  RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, &new_args);
  EXPECT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertInfoForTesting(), spiffe_bundle_map_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  distributor->SetKeyMaterials(kRootCertName, spiffe_bundle_map_1_,
                               std::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, nullptr, identity_pairs_1_);
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertInfoForTesting(), spiffe_bundle_map_1_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_1_);
}

// TODO(gtcooke94): Enable when support for SPIFFE verification is complete.
TEST_F(TlsSecurityConnectorTest,
       DISABLED_SpiffeDistributorHasErrorForChannelSecurityConnector) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, spiffe_bundle_map_0_,
                               std::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, nullptr, identity_pairs_0_);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_root_certificate_provider(provider);
  options->set_identity_certificate_provider(std::move(provider));
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  ChannelArgs new_args;
  RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, &new_args);
  EXPECT_NE(connector, nullptr);
  TlsChannelSecurityConnector* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertInfoForTesting(), spiffe_bundle_map_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  // Calling SetErrorForCert on distributor shouldn't invalidate the previous
  // valid credentials.
  distributor->SetErrorForCert(kRootCertName, GRPC_ERROR_CREATE(kErrorMessage),
                               std::nullopt);
  distributor->SetErrorForCert(kIdentityCertName, std::nullopt,
                               GRPC_ERROR_CREATE(kErrorMessage));
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertInfoForTesting(), spiffe_bundle_map_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
}

// TODO(gtcooke94): Enable when support for SPIFFE verification is complete.
TEST_F(
    TlsSecurityConnectorTest,
    DISABLED_SpiffeRootAndIdentityCertsObtainedWhenCreateServerSecurityConnector) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, spiffe_bundle_map_0_,
                               std::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, nullptr, identity_pairs_0_);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_root_certificate_provider(provider);
  options->set_identity_certificate_provider(std::move(provider));
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  RefCountedPtr<TlsServerCredentials> credential =
      MakeRefCounted<TlsServerCredentials>(options);
  RefCountedPtr<grpc_server_security_connector> connector =
      credential->create_security_connector(ChannelArgs());
  EXPECT_NE(connector, nullptr);
  TlsServerSecurityConnector* tls_connector =
      static_cast<TlsServerSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertInfoForTesting(), spiffe_bundle_map_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  distributor->SetKeyMaterials(kRootCertName, spiffe_bundle_map_1_,
                               std::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, nullptr, identity_pairs_1_);
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertInfoForTesting(), spiffe_bundle_map_1_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_1_);
}

// TODO(gtcooke94): Enable when support for SPIFFE verification is complete.
TEST_F(TlsSecurityConnectorTest,
       DISABLED_SpiffeDistributorHasErrorForServerSecurityConnector) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, spiffe_bundle_map_0_,
                               std::nullopt);
  distributor->SetKeyMaterials(kIdentityCertName, nullptr, identity_pairs_0_);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_root_certificate_provider(provider);
  options->set_identity_certificate_provider(std::move(provider));
  options->set_root_cert_name(kRootCertName);
  options->set_identity_cert_name(kIdentityCertName);
  RefCountedPtr<TlsServerCredentials> credential =
      MakeRefCounted<TlsServerCredentials>(options);
  RefCountedPtr<grpc_server_security_connector> connector =
      credential->create_security_connector(ChannelArgs());
  EXPECT_NE(connector, nullptr);
  TlsServerSecurityConnector* tls_connector =
      static_cast<TlsServerSecurityConnector*>(connector.get());
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertInfoForTesting(), spiffe_bundle_map_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
  // Calling SetErrorForCert on distributor shouldn't invalidate the previous
  // valid credentials.
  distributor->SetErrorForCert(kRootCertName, GRPC_ERROR_CREATE(kErrorMessage),
                               std::nullopt);
  distributor->SetErrorForCert(kIdentityCertName, std::nullopt,
                               GRPC_ERROR_CREATE(kErrorMessage));
  EXPECT_NE(tls_connector->ServerHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootCertInfoForTesting(), spiffe_bundle_map_0_);
  EXPECT_EQ(tls_connector->KeyCertPairListForTesting(), identity_pairs_0_);
}

// Verifies that multiple security connectors created from the same
// TlsCredentials with explicit PEM root certs share the same X509_STORE
// via X509_STORE_up_ref(), instead of each connector independently parsing
// the PEM into a fresh X509_STORE. This is a regression test for
// https://github.com/grpc/grpc/issues/42129.
TEST_F(TlsSecurityConnectorTest, MultipleConnectorsShareRootCertStore) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_1_, std::nullopt);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_root_certificate_provider(std::move(provider));
  options->set_root_cert_name(kRootCertName);
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  EXPECT_FALSE(credential->HasCachedRootStoreForTesting());
  // Create two security connectors from the same credentials.
  ChannelArgs args1;
  RefCountedPtr<grpc_channel_security_connector> connector1 =
      credential->create_security_connector(nullptr, kTargetName, &args1);
  ASSERT_NE(connector1, nullptr);
  ChannelArgs args2;
  RefCountedPtr<grpc_channel_security_connector> connector2 =
      credential->create_security_connector(nullptr, kTargetName, &args2);
  ASSERT_NE(connector2, nullptr);
  auto* tls_connector1 =
      static_cast<TlsChannelSecurityConnector*>(connector1.get());
  auto* tls_connector2 =
      static_cast<TlsChannelSecurityConnector*>(connector2.get());
  // Both should have handshaker factories.
  tsi_ssl_client_handshaker_factory* factory1 =
      tls_connector1->ClientHandshakerFactoryForTesting();
  tsi_ssl_client_handshaker_factory* factory2 =
      tls_connector2->ClientHandshakerFactoryForTesting();
  ASSERT_NE(factory1, nullptr);
  ASSERT_NE(factory2, nullptr);
  // The factories should be different (each connector has its own).
  EXPECT_NE(factory1, factory2);
  // But they should share the same X509_STORE (root cert store), meaning the
  // PEM was parsed once and shared via X509_STORE_up_ref().
  X509_STORE* store1 =
      tsi_ssl_client_handshaker_factory_get_cert_store(factory1);
  X509_STORE* store2 =
      tsi_ssl_client_handshaker_factory_get_cert_store(factory2);
  ASSERT_NE(store1, nullptr);
  ASSERT_NE(store2, nullptr);
#if OPENSSL_VERSION_NUMBER >= 0x10100000
  EXPECT_EQ(store1, store2);
  const long expected_flags =
      X509_V_FLAG_PARTIAL_CHAIN | X509_V_FLAG_TRUSTED_FIRST;
  EXPECT_EQ(GetVerificationFlags(store1) & expected_flags, expected_flags);
  EXPECT_TRUE(credential->HasCachedRootStoreForTesting());
#else
  EXPECT_FALSE(credential->HasCachedRootStoreForTesting());
#endif
}

// Verifies that root rotation updates the existing connector to a new
// X509_STORE and that new connectors converge on the latest store.
TEST_F(TlsSecurityConnectorTest, RootCertRotationProducesNewCertStore) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_1_, std::nullopt);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_root_certificate_provider(std::move(provider));
  options->set_root_cert_name(kRootCertName);
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  // Create a connector with the initial root certs.
  ChannelArgs args1;
  RefCountedPtr<grpc_channel_security_connector> connector1 =
      credential->create_security_connector(nullptr, kTargetName, &args1);
  ASSERT_NE(connector1, nullptr);
  auto* tls_connector1 =
      static_cast<TlsChannelSecurityConnector*>(connector1.get());
  tsi_ssl_client_handshaker_factory* factory1 =
      tls_connector1->ClientHandshakerFactoryForTesting();
  ASSERT_NE(factory1, nullptr);
  tsi_ssl_client_handshaker_factory* retained_factory1 =
      tsi_ssl_client_handshaker_factory_ref(factory1);
  X509_STORE* old_store =
      tsi_ssl_client_handshaker_factory_get_cert_store(retained_factory1);
  ASSERT_NE(old_store, nullptr);
#if OPENSSL_VERSION_NUMBER >= 0x10100000
  EXPECT_TRUE(credential->HasCachedRootStoreForTesting());
#else
  EXPECT_FALSE(credential->HasCachedRootStoreForTesting());
#endif
  // Rotate root certs via the distributor.
  distributor->SetKeyMaterials(kRootCertName, root_cert_0_, std::nullopt);
#if OPENSSL_VERSION_NUMBER >= 0x10100000
  EXPECT_TRUE(credential->HasCachedRootStoreForTesting());
  ASSERT_NE(tls_connector1->RootStoreForTesting(), nullptr);
#else
  EXPECT_FALSE(credential->HasCachedRootStoreForTesting());
  EXPECT_EQ(tls_connector1->RootStoreForTesting(), nullptr);
#endif
  X509_STORE* rotated_store =
      tsi_ssl_client_handshaker_factory_get_cert_store(
          tls_connector1->ClientHandshakerFactoryForTesting());
  ASSERT_NE(rotated_store, nullptr);
  EXPECT_NE(old_store, rotated_store);
  // Create a second connector which picks up the new root certs.
  ChannelArgs args2;
  RefCountedPtr<grpc_channel_security_connector> connector2 =
      credential->create_security_connector(nullptr, kTargetName, &args2);
  ASSERT_NE(connector2, nullptr);
  auto* tls_connector2 =
      static_cast<TlsChannelSecurityConnector*>(connector2.get());
  tsi_ssl_client_handshaker_factory* factory2 =
      tls_connector2->ClientHandshakerFactoryForTesting();
  ASSERT_NE(factory2, nullptr);
  X509_STORE* store2 =
      tsi_ssl_client_handshaker_factory_get_cert_store(factory2);
  ASSERT_NE(store2, nullptr);
#if OPENSSL_VERSION_NUMBER >= 0x10100000
  EXPECT_TRUE(credential->HasCachedRootStoreForTesting());
  EXPECT_EQ(rotated_store, store2);
  EXPECT_EQ(tls_connector1->RootStoreForTesting(),
            tls_connector2->RootStoreForTesting());
#else
  EXPECT_FALSE(credential->HasCachedRootStoreForTesting());
  EXPECT_EQ(tls_connector2->RootStoreForTesting(), nullptr);
#endif
  tsi_ssl_client_handshaker_factory_unref(retained_factory1);
}

TEST_F(TlsSecurityConnectorTest,
       ConnectorsDoNotShareRootCertStoreWhenCrlDirectoryIsConfigured) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_1_, std::nullopt);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_root_certificate_provider(std::move(provider));
  options->set_root_cert_name(kRootCertName);
  options->set_crl_directory(kFakeCrlDirectory);
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  ChannelArgs args1;
  RefCountedPtr<grpc_channel_security_connector> connector1 =
      credential->create_security_connector(nullptr, kTargetName, &args1);
  ASSERT_NE(connector1, nullptr);
  ChannelArgs args2;
  RefCountedPtr<grpc_channel_security_connector> connector2 =
      credential->create_security_connector(nullptr, kTargetName, &args2);
  ASSERT_NE(connector2, nullptr);
  auto* tls_connector1 =
      static_cast<TlsChannelSecurityConnector*>(connector1.get());
  auto* tls_connector2 =
      static_cast<TlsChannelSecurityConnector*>(connector2.get());
  tsi_ssl_client_handshaker_factory* factory1 =
      tls_connector1->ClientHandshakerFactoryForTesting();
  tsi_ssl_client_handshaker_factory* factory2 =
      tls_connector2->ClientHandshakerFactoryForTesting();
  ASSERT_NE(factory1, nullptr);
  ASSERT_NE(factory2, nullptr);
  X509_STORE* store1 =
      tsi_ssl_client_handshaker_factory_get_cert_store(factory1);
  X509_STORE* store2 =
      tsi_ssl_client_handshaker_factory_get_cert_store(factory2);
  ASSERT_NE(store1, nullptr);
  ASSERT_NE(store2, nullptr);
#if OPENSSL_VERSION_NUMBER >= 0x10100000
  EXPECT_NE(store1, store2);
#endif
  EXPECT_EQ(tls_connector1->RootStoreForTesting(), nullptr);
  EXPECT_EQ(tls_connector2->RootStoreForTesting(), nullptr);
  EXPECT_FALSE(credential->HasCachedRootStoreForTesting());
}

TEST_F(TlsSecurityConnectorTest, PemRootCacheClearedWhenRootsRotateToSpiffe) {
  RefCountedPtr<grpc_tls_certificate_distributor> distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials(kRootCertName, root_cert_1_, std::nullopt);
  RefCountedPtr<grpc_tls_certificate_provider> provider =
      MakeRefCounted<TlsTestCertificateProvider>(distributor);
  RefCountedPtr<grpc_tls_credentials_options> options =
      MakeRefCounted<grpc_tls_credentials_options>();
  options->set_root_certificate_provider(std::move(provider));
  options->set_root_cert_name(kRootCertName);
  RefCountedPtr<TlsCredentials> credential =
      MakeRefCounted<TlsCredentials>(options);
  ChannelArgs args;
  RefCountedPtr<grpc_channel_security_connector> connector =
      credential->create_security_connector(nullptr, kTargetName, &args);
  ASSERT_NE(connector, nullptr);
  auto* tls_connector =
      static_cast<TlsChannelSecurityConnector*>(connector.get());
#if OPENSSL_VERSION_NUMBER >= 0x10100000
  EXPECT_TRUE(credential->HasCachedRootStoreForTesting());
#else
  EXPECT_FALSE(credential->HasCachedRootStoreForTesting());
#endif
  EXPECT_EQ(tls_connector->RootCertInfoForTesting(), root_cert_1_);
  distributor->SetKeyMaterials(kRootCertName, spiffe_bundle_map_0_,
                               std::nullopt);
  EXPECT_EQ(tls_connector->RootCertInfoForTesting(), spiffe_bundle_map_0_);
  EXPECT_NE(tls_connector->ClientHandshakerFactoryForTesting(), nullptr);
  EXPECT_EQ(tls_connector->RootStoreForTesting(), nullptr);
  EXPECT_FALSE(credential->HasCachedRootStoreForTesting());
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
