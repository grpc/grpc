// Copyright 2021 gRPC authors.

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

// http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/core/tsi/ssl_transport_security.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/tsi/transport_security.h"
#include "src/core/tsi/transport_security_interface.h"
#include "test/core/tsi/transport_security_test_lib.h"
#include "test/core/util/test_config.h"

extern "C" {
#include <openssl/crypto.h>
#include <openssl/pem.h>
}

static const int kSslTsiTestRevokedKeyCertPairsNum = 1;
static const int kSslTsiTestValidKeyCertPairsNum = 1;
static const std::string kSslTsiTestCrlSupportedCredentialsDir =
    "test/core/tsi/test_creds/";

class CrlSslTransportSecurityTest
    : public testing::TestWithParam<tsi_tls_version> {
 protected:
  struct SslTsiTestFixture {
    tsi_test_fixture base;
    bool use_revoked_server_cert;
    bool use_revoked_client_cert;
    char* root_cert;
    tsi_ssl_root_certs_store* root_store;
    tsi_ssl_pem_key_cert_pair* revoked_pem_key_cert_pairs;
    tsi_ssl_pem_key_cert_pair* valid_pem_key_cert_pairs;
    uint16_t revoked_num_key_cert_pairs;
    uint16_t valid_num_key_cert_pairs;
    std::string crl_directory;
    std::string server_name_indication;
    bool session_reused;
    std::string session_ticket_key;
    size_t session_ticket_key_size;
    tsi_ssl_server_handshaker_factory* server_handshaker_factory;
    tsi_ssl_client_handshaker_factory* client_handshaker_factory;
  };

  void SetUp() override {
    // Set up the vtable.
    vtable_.setup_handshakers =
        &CrlSslTransportSecurityTest::ssl_test_setup_handshakers;
    vtable_.check_handshaker_peers =
        &CrlSslTransportSecurityTest::ssl_test_check_handshaker_peers;
    vtable_.destruct = &CrlSslTransportSecurityTest::ssl_test_destruct;
    // Set up the SSL fixture that will be used for the handshake.
    ssl_fixture_ =
        static_cast<SslTsiTestFixture*>(gpr_zalloc(sizeof(*ssl_fixture_)));
    tsi_test_fixture_init(&ssl_fixture_->base);
    ssl_fixture_->base.test_unused_bytes = true;
    ssl_fixture_->base.vtable = &vtable_;
    ssl_fixture_->revoked_num_key_cert_pairs =
        kSslTsiTestRevokedKeyCertPairsNum;
    ssl_fixture_->valid_num_key_cert_pairs = kSslTsiTestValidKeyCertPairsNum;
    ssl_fixture_->revoked_pem_key_cert_pairs =
        static_cast<tsi_ssl_pem_key_cert_pair*>(
            gpr_malloc(sizeof(tsi_ssl_pem_key_cert_pair) *
                       ssl_fixture_->revoked_num_key_cert_pairs));
    ssl_fixture_->valid_pem_key_cert_pairs =
        static_cast<tsi_ssl_pem_key_cert_pair*>(
            gpr_malloc(sizeof(tsi_ssl_pem_key_cert_pair) *
                       ssl_fixture_->valid_num_key_cert_pairs));
    ssl_fixture_->revoked_pem_key_cert_pairs[0].private_key =
        LoadFile(kSslTsiTestCrlSupportedCredentialsDir + "revoked.key");
    ssl_fixture_->revoked_pem_key_cert_pairs[0].cert_chain =
        LoadFile(kSslTsiTestCrlSupportedCredentialsDir + "revoked.pem");
    ssl_fixture_->valid_pem_key_cert_pairs[0].private_key =
        LoadFile(kSslTsiTestCrlSupportedCredentialsDir + "valid.key");
    ssl_fixture_->valid_pem_key_cert_pairs[0].cert_chain =
        LoadFile(kSslTsiTestCrlSupportedCredentialsDir + "valid.pem");
    ssl_fixture_->root_cert =
        LoadFile(kSslTsiTestCrlSupportedCredentialsDir + "ca.pem");
    ssl_fixture_->root_store =
        tsi_ssl_root_certs_store_create(ssl_fixture_->root_cert);
    ssl_fixture_->crl_directory = kSslTsiTestCrlSupportedCredentialsDir;
    GPR_ASSERT(ssl_fixture_->root_store != nullptr);
  }

  void TearDown() override { tsi_test_fixture_destroy(&ssl_fixture_->base); }

  SslTsiTestFixture* ssl_fixture_;
  tsi_test_fixture_vtable vtable_;

 private:
  static void ssl_test_setup_handshakers(tsi_test_fixture* fixture) {
    SslTsiTestFixture* ssl_fixture =
        reinterpret_cast<SslTsiTestFixture*>(fixture);
    GPR_ASSERT(ssl_fixture != nullptr);
    /* Create client handshaker factory. */

    tsi_ssl_client_handshaker_options client_options;
    client_options.pem_root_certs = ssl_fixture->root_cert;
    if (ssl_fixture->use_revoked_client_cert) {
      client_options.pem_key_cert_pair =
          ssl_fixture->revoked_pem_key_cert_pairs;
    } else {
      client_options.pem_key_cert_pair = ssl_fixture->valid_pem_key_cert_pairs;
    }
    client_options.crl_directory = ssl_fixture->crl_directory.c_str();

    client_options.root_store = ssl_fixture->root_store;
    client_options.min_tls_version = GetParam();
    client_options.max_tls_version = GetParam();
    GPR_ASSERT(tsi_create_ssl_client_handshaker_factory_with_options(
                   &client_options, &ssl_fixture->client_handshaker_factory) ==
               TSI_OK);
    /* Create server handshaker factory. */
    tsi_ssl_server_handshaker_options server_options;

    if (ssl_fixture->use_revoked_server_cert) {
      server_options.pem_key_cert_pairs =
          ssl_fixture->revoked_pem_key_cert_pairs;
      server_options.num_key_cert_pairs =
          ssl_fixture->revoked_num_key_cert_pairs;
    } else {
      server_options.pem_key_cert_pairs = ssl_fixture->valid_pem_key_cert_pairs;
      server_options.num_key_cert_pairs = ssl_fixture->valid_num_key_cert_pairs;
    }
    server_options.pem_client_root_certs = ssl_fixture->root_cert;
    server_options.crl_directory = ssl_fixture->crl_directory.c_str();
    server_options.client_certificate_request =
        TSI_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;
    server_options.session_ticket_key = ssl_fixture->session_ticket_key.c_str();
    server_options.session_ticket_key_size =
        ssl_fixture->session_ticket_key_size;
    server_options.min_tls_version = GetParam();
    server_options.max_tls_version = GetParam();
    GPR_ASSERT(tsi_create_ssl_server_handshaker_factory_with_options(
                   &server_options, &ssl_fixture->server_handshaker_factory) ==
               TSI_OK);
    /* Create server and client handshakers. */
    GPR_ASSERT(tsi_ssl_client_handshaker_factory_create_handshaker(
                   ssl_fixture->client_handshaker_factory,
                   ssl_fixture->server_name_indication.c_str(),
                   &ssl_fixture->base.client_handshaker) == TSI_OK);
    GPR_ASSERT(tsi_ssl_server_handshaker_factory_create_handshaker(
                   ssl_fixture->server_handshaker_factory,
                   &ssl_fixture->base.server_handshaker) == TSI_OK);
  }

  static void ssl_test_check_handshaker_peers(tsi_test_fixture* fixture) {
    SslTsiTestFixture* ssl_fixture =
        reinterpret_cast<SslTsiTestFixture*>(fixture);
    GPR_ASSERT(ssl_fixture != nullptr);
    tsi_peer peer;
    // In TLS 1.3, the client-side handshake succeeds even if the client sends a
    // revoked certificate. In such a case, the server would fail the TLS
    // handshake and send an alert to the client as the first application data
    // message. In TLS 1.2, the client-side handshake will fail if the client
    // sends a revoked certificate.
    //
    // For OpenSSL versions < 1.1, TLS 1.3 is not supported, so the client-side
    // handshake should succeed precisely when the server-side handshake
    // succeeds.
    bool expect_server_success = !(ssl_fixture->use_revoked_server_cert ||
                                   ssl_fixture->use_revoked_client_cert);
#if OPENSSL_VERSION_NUMBER >= 0x10100000
    bool expect_client_success = GetParam() == tsi_tls_version::TSI_TLS1_2
                                     ? expect_server_success
                                     : !(ssl_fixture->use_revoked_server_cert);
#else
    bool expect_client_success = expect_server_success;
#endif

    if (expect_client_success) {
      GPR_ASSERT(tsi_handshaker_result_extract_peer(
                     ssl_fixture->base.client_result, &peer) == TSI_OK);
      tsi_peer_destruct(&peer);
    } else {
      GPR_ASSERT(ssl_fixture->base.client_result == nullptr);
    }
    if (expect_server_success) {
      GPR_ASSERT(tsi_handshaker_result_extract_peer(
                     ssl_fixture->base.server_result, &peer) == TSI_OK);
      tsi_peer_destruct(&peer);
    } else {
      GPR_ASSERT(ssl_fixture->base.server_result == nullptr);
    }
  }

  static void ssl_test_pem_key_cert_pair_destroy(tsi_ssl_pem_key_cert_pair kp) {
    gpr_free(const_cast<char*>(kp.private_key));
    gpr_free(const_cast<char*>(kp.cert_chain));
  }

  static void ssl_test_destruct(tsi_test_fixture* fixture) {
    SslTsiTestFixture* ssl_fixture =
        reinterpret_cast<SslTsiTestFixture*>(fixture);
    if (ssl_fixture == nullptr) {
      return;
    }
    /* Destroy SslKeyCertLib-> */
    for (size_t i = 0; i < ssl_fixture->valid_num_key_cert_pairs; i++) {
      ssl_test_pem_key_cert_pair_destroy(
          ssl_fixture->valid_pem_key_cert_pairs[i]);
    }
    gpr_free(ssl_fixture->valid_pem_key_cert_pairs);

    for (size_t i = 0; i < ssl_fixture->revoked_num_key_cert_pairs; i++) {
      ssl_test_pem_key_cert_pair_destroy(
          ssl_fixture->revoked_pem_key_cert_pairs[i]);
    }
    gpr_free(ssl_fixture->revoked_pem_key_cert_pairs);

    gpr_free(ssl_fixture->root_cert);
    tsi_ssl_root_certs_store_destroy(ssl_fixture->root_store);
    /* Unreference others. */
    tsi_ssl_server_handshaker_factory_unref(
        ssl_fixture->server_handshaker_factory);
    tsi_ssl_client_handshaker_factory_unref(
        ssl_fixture->client_handshaker_factory);
  }

  static char* LoadFile(absl::string_view file_path) {
    grpc_slice slice;
    GPR_ASSERT(grpc_load_file(file_path.data(), 1, &slice) == GRPC_ERROR_NONE);
    char* data = grpc_slice_to_c_string(slice);
    grpc_slice_unref(slice);
    return data;
  }
};

TEST_P(CrlSslTransportSecurityTest,
       ssl_tsi_test_do_handshake_with_revoked_server_cert) {
  ssl_fixture_->use_revoked_server_cert = true;
  tsi_test_do_handshake(&ssl_fixture_->base);
}
TEST_P(CrlSslTransportSecurityTest,
       ssl_tsi_test_do_handshake_with_revoked_client_cert) {
  ssl_fixture_->use_revoked_client_cert = true;
  tsi_test_do_handshake(&ssl_fixture_->base);
}

TEST_P(CrlSslTransportSecurityTest,
       ssl_tsi_test_do_handshake_with_valid_certs) {
  tsi_test_do_handshake(&ssl_fixture_->base);
}

INSTANTIATE_TEST_SUITE_P(TLSVersionsTest, CrlSslTransportSecurityTest,
                         testing::Values(tsi_tls_version::TSI_TLS1_2,
                                         tsi_tls_version::TSI_TLS1_3));

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
