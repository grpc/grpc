// Copyright 2025 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <memory>
#include <utility>

#include "src/core/credentials/transport/security_connector.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security.h"
#include "src/core/tsi/transport_security_interface.h"
#include "src/core/util/crash.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "test/core/tsi/transport_security_test_lib.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

extern "C" {
#include <openssl/crypto.h>
#include <openssl/pem.h>
}

namespace {
constexpr absl::string_view kCaPemPath =
    "test/core/tsi/test_creds/spiffe_end2end/ca.pem";
constexpr absl::string_view kClientKeyPath =
    "test/core/tsi/test_creds/spiffe_end2end/client.key";
constexpr absl::string_view kClientCertPath =
    "test/core/tsi/test_creds/spiffe_end2end/client_spiffe.pem";
constexpr absl::string_view kServerKeyPath =
    "test/core/tsi/test_creds/spiffe_end2end/server.key";
constexpr absl::string_view kServerCertPath =
    "test/core/tsi/test_creds/spiffe_end2end/server_spiffe.pem";
constexpr absl::string_view kServerChainKeyPath =
    "test/core/tsi/test_creds/spiffe_end2end/leaf_signed_by_intermediate.key";
constexpr absl::string_view kServerChainCertPath =
    "test/core/tsi/test_creds/spiffe_end2end/leaf_and_intermediate_chain.pem";
constexpr absl::string_view kClientSpiffeBundleMapPath =
    "test/core/tsi/test_creds/spiffe_end2end/client_spiffebundle.json";
constexpr absl::string_view kServerSpiffeBundleMapPath =
    "test/core/tsi/test_creds/spiffe_end2end/server_spiffebundle.json";

constexpr absl::string_view kNonSpiffeKeyPath =
    "test/core/tsi/test_creds/crl_data/valid.key";
constexpr absl::string_view kNonSpiffeCertPath =
    "test/core/tsi/test_creds/crl_data/valid.pem";
constexpr absl::string_view kMultiSanKeyPath =
    "test/core/tsi/test_creds/spiffe_end2end/multi_san.key";
constexpr absl::string_view kMultiSanCertPath =
    "test/core/tsi/test_creds/spiffe_end2end/multi_san_spiffe.pem";
constexpr absl::string_view kInvalidUtf8SanKeyPath =
    "test/core/tsi/test_creds/spiffe_end2end/invalid_utf8_san.key";
constexpr absl::string_view kInvalidUtf8SanCertPath =
    "test/core/tsi/test_creds/spiffe_end2end/invalid_utf8_san_spiffe.pem";

class SpiffeSslTransportSecurityTest
    : public testing::TestWithParam<tsi_tls_version> {
 protected:
  // A tsi_test_fixture implementation.
  class SslTsiTestFixture {
   public:
    SslTsiTestFixture(
        absl::string_view server_key_path, absl::string_view server_cert_path,
        absl::string_view client_key_path, absl::string_view client_cert_path,
        absl::string_view server_spiffe_bundle_map_path,
        absl::string_view client_spiffe_bundle_map_path,
        std::optional<absl::string_view> ca_path, bool expect_server_success,
        bool expect_client_success_1_2, bool expect_client_success_1_3) {
      tsi_test_fixture_init(&base_);
      base_.test_unused_bytes = true;
      base_.vtable = &kVtable;
      server_key_ = grpc_core::testing::GetFileContents(server_key_path.data());
      server_cert_ =
          grpc_core::testing::GetFileContents(server_cert_path.data());
      client_key_ = grpc_core::testing::GetFileContents(client_key_path.data());
      client_cert_ =
          grpc_core::testing::GetFileContents(client_cert_path.data());
      // We set this and it shouldn't matter if we set spiffe bundles
      if (ca_path.has_value()) {
        ca_certificates_ = grpc_core::testing::GetFileContents(ca_path->data());
      }
      if (!server_spiffe_bundle_map_path.empty()) {
        auto server_map =
            grpc_core::SpiffeBundleMap::FromFile(server_spiffe_bundle_map_path);
        CHECK(server_map.ok());
        server_spiffe_bundle_map_ = std::make_shared<RootCertInfo>(*server_map);
      }
      if (!client_spiffe_bundle_map_path.empty()) {
        auto client_map =
            grpc_core::SpiffeBundleMap::FromFile(client_spiffe_bundle_map_path);
        CHECK(client_map.ok());
        client_spiffe_bundle_map_ = std::make_shared<RootCertInfo>(*client_map);
      }
      expect_server_success_ = expect_server_success;
      expect_client_success_1_2_ = expect_client_success_1_2;
      expect_client_success_1_3_ = expect_client_success_1_3;

      server_pem_key_cert_pairs_ = static_cast<tsi_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(tsi_ssl_pem_key_cert_pair)));
      server_pem_key_cert_pairs_[0].private_key = server_key_;
      server_pem_key_cert_pairs_[0].cert_chain = server_cert_;
      client_pem_key_cert_pairs_ = static_cast<tsi_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(tsi_ssl_pem_key_cert_pair)));
      client_pem_key_cert_pairs_[0].private_key = client_key_;
      client_pem_key_cert_pairs_[0].cert_chain = client_cert_;
    }

    void Run() {
      tsi_test_do_handshake(&base_);
      tsi_test_fixture_destroy(&base_);
    }

    ~SslTsiTestFixture() {
      gpr_free(server_pem_key_cert_pairs_);
      gpr_free(client_pem_key_cert_pairs_);

      tsi_ssl_server_handshaker_factory_unref(server_handshaker_factory_);
      tsi_ssl_client_handshaker_factory_unref(client_handshaker_factory_);
    }

   private:
    static void SetupHandshakers(tsi_test_fixture* fixture) {
      CHECK_NE(fixture, nullptr);
      auto* self = reinterpret_cast<SslTsiTestFixture*>(fixture);
      self->SetupHandshakers();
    }

    void SetupHandshakers() {
      // Create client handshaker factory.
      tsi_ssl_client_handshaker_options client_options;
      client_options.pem_key_cert_pair = client_pem_key_cert_pairs_;
      if (client_spiffe_bundle_map_ != nullptr) {
        client_options.root_cert_info = client_spiffe_bundle_map_;
      } else {
        client_options.root_cert_info =
            std::make_shared<RootCertInfo>(ca_certificates_);
      }
      client_options.min_tls_version = GetParam();
      client_options.max_tls_version = GetParam();
      EXPECT_EQ(tsi_create_ssl_client_handshaker_factory_with_options(
                    &client_options, &client_handshaker_factory_),
                TSI_OK);
      // Create server handshaker factory.
      tsi_ssl_server_handshaker_options server_options;
      server_options.pem_key_cert_pairs = server_pem_key_cert_pairs_;
      server_options.num_key_cert_pairs = 1;
      if (server_spiffe_bundle_map_ != nullptr) {
        server_options.root_cert_info = server_spiffe_bundle_map_;
      } else {
        server_options.root_cert_info =
            std::make_shared<RootCertInfo>(ca_certificates_);
      }
      server_options.client_certificate_request =
          TSI_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;
      server_options.session_ticket_key = nullptr;
      server_options.session_ticket_key_size = 0;
      server_options.min_tls_version = GetParam();
      server_options.max_tls_version = GetParam();
      EXPECT_EQ(tsi_create_ssl_server_handshaker_factory_with_options(
                    &server_options, &server_handshaker_factory_),
                TSI_OK);
      // Create server and client handshakers.
      EXPECT_EQ(tsi_ssl_client_handshaker_factory_create_handshaker(
                    client_handshaker_factory_, nullptr, 0, 0,
                    /*alpn_preferred_protocol_list=*/std::nullopt,
                    &base_.client_handshaker),
                TSI_OK);
      EXPECT_EQ(tsi_ssl_server_handshaker_factory_create_handshaker(
                    server_handshaker_factory_, 0, 0, &base_.server_handshaker),
                TSI_OK);
    }

    static void CheckHandshakerPeers(tsi_test_fixture* fixture) {
      CHECK_NE(fixture, nullptr);
      auto* self = reinterpret_cast<SslTsiTestFixture*>(fixture);
      self->CheckHandshakerPeers();
    }

    void CheckHandshakerPeers() {
      bool expect_server_success = expect_server_success_;
      bool expect_client_success = false;
#if OPENSSL_VERSION_NUMBER >= 0x10100000
      expect_client_success = GetParam() == tsi_tls_version::TSI_TLS1_2
                                  ? expect_client_success_1_2_
                                  : expect_client_success_1_3_;
#else
      //  If using OpenSSL version < 1.1, the CRL revocation won't
      //  be enabled anyways, so we always expect the connection to
      //  be successful.
      expect_server_success = true;
      expect_client_success = expect_server_success;
#endif
      tsi_peer peer;
      if (expect_client_success) {
        EXPECT_EQ(
            tsi_handshaker_result_extract_peer(base_.client_result, &peer),
            TSI_OK);
        tsi_peer_destruct(&peer);
      } else {
        EXPECT_EQ(base_.client_result, nullptr);
      }
      if (expect_server_success) {
        EXPECT_EQ(
            tsi_handshaker_result_extract_peer(base_.server_result, &peer),
            TSI_OK);
        tsi_peer_destruct(&peer);
      } else {
        EXPECT_EQ(base_.server_result, nullptr);
      }
    }

    static void Destruct(tsi_test_fixture* fixture) {
      auto* self = reinterpret_cast<SslTsiTestFixture*>(fixture);
      delete self;
    }

    static struct tsi_test_fixture_vtable kVtable;

    tsi_test_fixture base_;
    std::string ca_certificates_;
    tsi_ssl_server_handshaker_factory* server_handshaker_factory_;
    tsi_ssl_client_handshaker_factory* client_handshaker_factory_;
    std::shared_ptr<RootCertInfo> server_spiffe_bundle_map_;
    std::shared_ptr<RootCertInfo> client_spiffe_bundle_map_;

    std::string server_key_;
    std::string server_cert_;
    std::string client_key_;
    std::string client_cert_;
    bool expect_server_success_;
    bool expect_client_success_1_2_;
    bool expect_client_success_1_3_;
    tsi_ssl_pem_key_cert_pair* client_pem_key_cert_pairs_;
    tsi_ssl_pem_key_cert_pair* server_pem_key_cert_pairs_;
  };
};

struct tsi_test_fixture_vtable
    SpiffeSslTransportSecurityTest::SslTsiTestFixture::kVtable = {
        &SpiffeSslTransportSecurityTest::SslTsiTestFixture::SetupHandshakers,
        &SpiffeSslTransportSecurityTest::SslTsiTestFixture::
            CheckHandshakerPeers,
        &SpiffeSslTransportSecurityTest::SslTsiTestFixture::Destruct};

// Valid SPIFFE Bundles on both sides with the root configured for the
// appropriate trust domain
TEST_P(SpiffeSslTransportSecurityTest, MTLSSpiffe) {
  auto* fixture = new SslTsiTestFixture(
      kServerKeyPath, kServerCertPath, kClientKeyPath, kClientCertPath,
      kServerSpiffeBundleMapPath, kClientSpiffeBundleMapPath, std::nullopt,
      /*expect_server_success=*/true,
      /*expect_client_success_1_2=*/true, /*expect_client_success_1_3=*/true);
  fixture->Run();
}

// Valid SPIFFE Bundles on both sides with the root configured for the
// appropriate trust domain, and a certificate chain with an intermediate CA on
// the server side
TEST_P(SpiffeSslTransportSecurityTest, MTLSSpiffeChain) {
  auto* fixture = new SslTsiTestFixture(
      kServerChainKeyPath, kServerChainCertPath, kClientKeyPath,
      kClientCertPath, kServerSpiffeBundleMapPath, kClientSpiffeBundleMapPath,
      std::nullopt,
      /*expect_server_success=*/true,
      /*expect_client_success_1_2=*/true,
      /*expect_client_success_1_3=*/true);
  fixture->Run();
}

// Valid SPIFFE bundle on the client side, but the server side has a flat list
// of CA certificates.
TEST_P(SpiffeSslTransportSecurityTest, ClientSideSpiffeBundle) {
  auto* fixture = new SslTsiTestFixture(kServerKeyPath, kServerCertPath,
                                        kClientKeyPath, kClientCertPath, "",
                                        kClientSpiffeBundleMapPath, kCaPemPath,
                                        /*expect_server_success=*/true,
                                        /*expect_client_success_1_2=*/true,
                                        /*expect_client_success_1_3=*/true);
  fixture->Run();
}

// Valid SPIFFE bundle on the server side, but the client side has a flat list
// of CA certificates.
TEST_P(SpiffeSslTransportSecurityTest, ServerSideSpiffeBundle) {
  auto* fixture = new SslTsiTestFixture(
      kServerKeyPath, kServerCertPath, kClientKeyPath, kClientCertPath,
      kServerSpiffeBundleMapPath, "", kCaPemPath,
      /*expect_server_success=*/true,
      /*expect_client_success_1_2=*/true,
      /*expect_client_success_1_3=*/true);
  fixture->Run();
}

// Valid SPIFFE bundle on the client side, but the server side has a SPIFFE
// bundle that does not have a trust domain that will match the client leaf
// certificate. When negotiating TLS 1.3, the client-side handshake succeeds
// because server verification of the client certificate occurs after the
// client-side handshake is complete.
TEST_P(SpiffeSslTransportSecurityTest, MTLSSpiffeServerMismatchFail) {
  auto* fixture = new SslTsiTestFixture(
      kServerKeyPath, kServerCertPath, kClientKeyPath, kClientCertPath,
      kClientSpiffeBundleMapPath, kClientSpiffeBundleMapPath, std::nullopt,
      /*expect_server_success=*/false,
      /*expect_client_success_1_2=*/false,
      /*expect_client_success_1_3=*/true);
  fixture->Run();
}

// Valid SPIFFE bundle on the server side, but the client side has a SPIFFE
// bundle that does not have a trust domain that will match the server leaf
// certificate.
TEST_P(SpiffeSslTransportSecurityTest, MTLSSpiffeClientMismatchFail) {
  auto* fixture = new SslTsiTestFixture(
      kServerKeyPath, kServerCertPath, kClientKeyPath, kClientCertPath,
      kServerSpiffeBundleMapPath, kServerSpiffeBundleMapPath, std::nullopt,
      /*expect_server_success=*/false,
      /*expect_client_success_1_2=*/false,
      /*expect_client_success_1_3=*/false);
  fixture->Run();
}

// The client side is configured with only a SPIFFE bundle, but the server leaf
// certificate does not have a SPIFFE ID.
TEST_P(SpiffeSslTransportSecurityTest, NonSpiffeServerCertFail) {
  auto* fixture = new SslTsiTestFixture(
      kNonSpiffeKeyPath, kNonSpiffeCertPath, kClientKeyPath, kClientCertPath,
      kServerSpiffeBundleMapPath, kClientSpiffeBundleMapPath, std::nullopt,
      /*expect_server_success=*/false,
      /*expect_client_success_1_2=*/false,
      /*expect_client_success_1_3=*/false);
  fixture->Run();
}

// The server side is configured with only a SPIFFE bundle, but the client leaf
// certificate does not have a SPIFFE ID. When negotiating TLS 1.3, the
// client-side handshake succeeds because server verification of the client
// certificate occurs after the client-side handshake is complete.
TEST_P(SpiffeSslTransportSecurityTest, NonSpiffeClientCertFail) {
  // TLS1.3 client will pass because it validates the server
  auto* fixture = new SslTsiTestFixture(
      kServerKeyPath, kServerCertPath, kNonSpiffeKeyPath, kNonSpiffeCertPath,
      kServerSpiffeBundleMapPath, kClientSpiffeBundleMapPath, std::nullopt,
      /*expect_server_success=*/false,
      /*expect_client_success_1_2=*/false,
      /*expect_client_success_1_3=*/true);
  fixture->Run();
}

// The server side is configued with a SPIFFE bundle, but the client side has a
// certificate with multiple URI SANs which should fail SPIFFE verification. The
// client's certificate is otherwise valid. This specific failure should show up
// in logs. If SPIFFE verification is NOT done, we would expect this to pass -
// it's a function of the SPIFFE spec to fail on multiple URI SANs. We verify
// that the certificates used here would otherwise succeed when the root CA is
// used directly rather than the SPIFFE Bundle Map, then that same setup fails
// when a SPIFFE Bundle Map is used.
TEST_P(SpiffeSslTransportSecurityTest, MultiSanSpiffeCertFails) {
  // Passes because SPIFFE verification is not done, and this would be valid in
  // that case.
  auto* fixture_pass =
      new SslTsiTestFixture(kServerKeyPath, kServerCertPath, kMultiSanKeyPath,
                            kMultiSanCertPath, "", "", kCaPemPath,
                            /*expect_server_success=*/true,
                            /*expect_client_success_1_2=*/true,
                            /*expect_client_success_1_3=*/true);
  fixture_pass->Run();
  // Should fail SPIFFE verification because of multiple URI SANs.
  auto* fixture_fail = new SslTsiTestFixture(
      kServerKeyPath, kServerCertPath, kMultiSanKeyPath, kMultiSanCertPath,
      kServerSpiffeBundleMapPath, "", kCaPemPath,
      /*expect_server_success=*/false,
      /*expect_client_success_1_2=*/false,
      /*expect_client_success_1_3=*/true);
  fixture_fail->Run();
}

// The server side is configued with a SPIFFE bundle, but the client side has a
// certificate with multiple URI SANs which should fail SPIFFE verification. The
// client's certificate is otherwise valid. This specific failure should show up
// in logs. If SPIFFE verification is NOT done, we would expect this to pass -
// it's a function of the SPIFFE spec to fail on multiple URI SANs. We verify
// that the certificates used here would otherwise succeed when the root CA is
// used directly rather than the SPIFFE Bundle Map, then that same setup fails
// when a SPIFFE Bundle Map is used.
TEST_P(SpiffeSslTransportSecurityTest, InvalidUTF8Fails) {
  // Passes because SPIFFE verification is not done, and this would be valid in
  // that case.
  auto* fixture_pass = new SslTsiTestFixture(
      kServerKeyPath, kServerCertPath, kInvalidUtf8SanKeyPath,
      kInvalidUtf8SanCertPath, "", "", kCaPemPath,
      /*expect_server_success=*/true,
      /*expect_client_success_1_2=*/true,
      /*expect_client_success_1_3=*/true);
  fixture_pass->Run();
  // Should fail SPIFFE verification because of multiple URI SANs.
  auto* fixture_fail = new SslTsiTestFixture(
      kServerKeyPath, kServerCertPath, kInvalidUtf8SanKeyPath,
      kInvalidUtf8SanCertPath, kServerSpiffeBundleMapPath, "", kCaPemPath,
      /*expect_server_success=*/false,
      /*expect_client_success_1_2=*/false,
      /*expect_client_success_1_3=*/true);
  fixture_fail->Run();
}

std::string TestNameSuffix(
    const ::testing::TestParamInfo<tsi_tls_version>& version) {
  if (version.param == tsi_tls_version::TSI_TLS1_2) return "TLS_1_2";
  CHECK(version.param == tsi_tls_version::TSI_TLS1_3);
  return "TLS_1_3";
}

INSTANTIATE_TEST_SUITE_P(TLSVersionsTest, SpiffeSslTransportSecurityTest,
                         testing::Values(tsi_tls_version::TSI_TLS1_2,
                                         tsi_tls_version::TSI_TLS1_3),
                         &TestNameSuffix);

}  // namespace

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
