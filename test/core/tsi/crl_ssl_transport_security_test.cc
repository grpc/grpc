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
const char* const kSslTsiTestCrlSupportedCredentialsDir =
    "test/core/tsi/test_creds/";

// Indicates the TLS version used for the test.
static tsi_tls_version test_tls_version = tsi_tls_version::TSI_TLS1_3;

class SslTestFixture {
 public:
  SslTestFixture(bool use_revoked_server_cert, bool use_revoked_client_cert,
                 tsi_test_fixture_vtable* vtable) {
    tsi_test_fixture_init(&base);
    base.test_unused_bytes = true;
    base.vtable = vtable;
    revoked_num_key_cert_pairs = kSslTsiTestRevokedKeyCertPairsNum;
    valid_num_key_cert_pairs = kSslTsiTestValidKeyCertPairsNum;
    use_revoked_client_cert_ = use_revoked_client_cert;
    use_revoked_server_cert_ = use_revoked_server_cert;
    revoked_pem_key_cert_pairs =
        static_cast<tsi_ssl_pem_key_cert_pair*>(gpr_malloc(
            sizeof(tsi_ssl_pem_key_cert_pair) * revoked_num_key_cert_pairs));
    valid_pem_key_cert_pairs =
        static_cast<tsi_ssl_pem_key_cert_pair*>(gpr_malloc(
            sizeof(tsi_ssl_pem_key_cert_pair) * valid_num_key_cert_pairs));
    revoked_pem_key_cert_pairs[0].private_key =
        load_file(kSslTsiTestCrlSupportedCredentialsDir, "revoked.key");
    revoked_pem_key_cert_pairs[0].cert_chain =
        load_file(kSslTsiTestCrlSupportedCredentialsDir, "revoked.pem");
    valid_pem_key_cert_pairs[0].private_key =
        load_file(kSslTsiTestCrlSupportedCredentialsDir, "valid.key");
    valid_pem_key_cert_pairs[0].cert_chain =
        load_file(kSslTsiTestCrlSupportedCredentialsDir, "valid.pem");
    root_cert = load_file(kSslTsiTestCrlSupportedCredentialsDir, "ca.pem");
    root_store = tsi_ssl_root_certs_store_create(root_cert);
    GPR_ASSERT(root_store != nullptr);
    crl_directory = kSslTsiTestCrlSupportedCredentialsDir;
    // server_name_indication = nullptr;
    // session_ticket_key = nullptr;
    // session_ticket_key_size = 0;
  }

  ~SslTestFixture() { gpr_log(GPR_INFO, "Called destructor"); }

 public:
  static void ssl_test_setup_handshakers(tsi_test_fixture* fixture) {
    SslTestFixture* ssl_fixture = reinterpret_cast<SslTestFixture*>(fixture);
    GPR_ASSERT(ssl_fixture != nullptr);
    /* Create client handshaker factory. */

    tsi_ssl_client_handshaker_options client_options;
    client_options.pem_root_certs = ssl_fixture->root_cert;
    if (ssl_fixture->use_revoked_client_cert_) {
      client_options.pem_key_cert_pair =
          ssl_fixture->revoked_pem_key_cert_pairs;
    } else {
      client_options.pem_key_cert_pair = ssl_fixture->valid_pem_key_cert_pairs;
    }
    client_options.crl_directory = ssl_fixture->crl_directory;

    client_options.root_store = ssl_fixture->root_store;
    client_options.min_tls_version = test_tls_version;
    client_options.max_tls_version = test_tls_version;
    GPR_ASSERT(tsi_create_ssl_client_handshaker_factory_with_options(
                   &client_options, &ssl_fixture->client_handshaker_factory) ==
               TSI_OK);
    /* Create server handshaker factory. */
    tsi_ssl_server_handshaker_options server_options;

    if (ssl_fixture->use_revoked_server_cert_) {
      server_options.pem_key_cert_pairs =
          ssl_fixture->revoked_pem_key_cert_pairs;
      server_options.num_key_cert_pairs =
          ssl_fixture->revoked_num_key_cert_pairs;
    } else {
      server_options.pem_key_cert_pairs = ssl_fixture->valid_pem_key_cert_pairs;
      server_options.num_key_cert_pairs = ssl_fixture->valid_num_key_cert_pairs;
    }

    server_options.pem_client_root_certs = ssl_fixture->root_cert;
    server_options.crl_directory = ssl_fixture->crl_directory;

    server_options.client_certificate_request =
        TSI_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;
    server_options.session_ticket_key = ssl_fixture->session_ticket_key;
    server_options.session_ticket_key_size =
        ssl_fixture->session_ticket_key_size;
    server_options.min_tls_version = test_tls_version;
    server_options.max_tls_version = test_tls_version;
    GPR_ASSERT(tsi_create_ssl_server_handshaker_factory_with_options(
                   &server_options, &ssl_fixture->server_handshaker_factory) ==
               TSI_OK);
    /* Create server and client handshakers. */
    GPR_ASSERT(tsi_ssl_client_handshaker_factory_create_handshaker(
                   ssl_fixture->client_handshaker_factory,
                   ssl_fixture->server_name_indication,
                   &ssl_fixture->base.client_handshaker) == TSI_OK);
    gpr_log(GPR_INFO, "HERE1");
    GPR_ASSERT(tsi_ssl_server_handshaker_factory_create_handshaker(
                   ssl_fixture->server_handshaker_factory,
                   &ssl_fixture->base.server_handshaker) == TSI_OK);
    gpr_log(GPR_INFO, "HERE2");
  }

  static void ssl_test_check_handshaker_peers(tsi_test_fixture* fixture) {
    SslTestFixture* ssl_fixture = reinterpret_cast<SslTestFixture*>(fixture);
    GPR_ASSERT(ssl_fixture != nullptr);
    tsi_peer peer;
    gpr_log(GPR_INFO, "CHECJ HANDHSKAPER PEER");

    // In TLS 1.3, the client-side handshake succeeds even if the client sends a
    // revoked certificate. In such a case, the server would fail the TLS
    // handshake and send an alert to the client as the first application data
    // message. In TLS 1.2, the client-side handshake will fail if the client
    // sends a revoked certificate.
    //
    // For OpenSSL versions < 1.1, TLS 1.3 is not supported, so the client-side
    // handshake should succeed precisely when the server-side handshake
    // succeeds.
    bool expect_server_success = !(ssl_fixture->use_revoked_server_cert_ ||
                                   ssl_fixture->use_revoked_client_cert_);
#if OPENSSL_VERSION_NUMBER >= 0x10100000
    bool expect_client_success = test_tls_version == tsi_tls_version::TSI_TLS1_2
                                     ? expect_server_success
                                     : !(ssl_fixture->use_revoked_server_cert_);
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
    SslTestFixture* ssl_fixture = reinterpret_cast<SslTestFixture*>(fixture);
    gpr_log(GPR_INFO, "DO ssl_test_destruct");

    if (ssl_fixture == nullptr) {
      return;
    }

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

  static char* load_file(const char* dir_path, const char* file_name) {
    char* file_path = static_cast<char*>(
        gpr_zalloc(sizeof(char) * (strlen(dir_path) + strlen(file_name) + 1)));
    memcpy(file_path, dir_path, strlen(dir_path));
    memcpy(file_path + strlen(dir_path), file_name, strlen(file_name));
    grpc_slice slice;
    GPR_ASSERT(grpc_load_file(file_path, 1, &slice) == GRPC_ERROR_NONE);
    char* data = grpc_slice_to_c_string(slice);
    grpc_slice_unref(slice);
    gpr_free(file_path);
    return data;
  }
  tsi_test_fixture base;
  bool use_revoked_server_cert_;
  bool use_revoked_client_cert_;
  char* root_cert;
  tsi_ssl_root_certs_store* root_store;
  tsi_ssl_pem_key_cert_pair* revoked_pem_key_cert_pairs;
  tsi_ssl_pem_key_cert_pair* valid_pem_key_cert_pairs;
  uint16_t revoked_num_key_cert_pairs;
  uint16_t valid_num_key_cert_pairs;
  const char* crl_directory;
  char* server_name_indication;
  const char* session_ticket_key;
  size_t session_ticket_key_size;
  tsi_ssl_server_handshaker_factory* server_handshaker_factory;
  tsi_ssl_client_handshaker_factory* client_handshaker_factory;
};

static struct tsi_test_fixture_vtable kVtable = {
    &SslTestFixture::ssl_test_setup_handshakers,
    &SslTestFixture::ssl_test_check_handshaker_peers,
    &SslTestFixture::ssl_test_destruct};

class CrlSslTransportSecurityTest : public ::testing::Test {
 protected:
  CrlSslTransportSecurityTest() {}
  void SetUp() override {}

  void TearDown() override {}

 private:
};

TEST_F(CrlSslTransportSecurityTest,
       ssl_tsi_test_do_handshake_with_revoked_server_cert) {
  SslTestFixture* fixture = new SslTestFixture(true, false, &kVtable);
  tsi_test_fixture* base = reinterpret_cast<tsi_test_fixture*>(fixture);
  tsi_test_do_handshake(base);
  tsi_test_fixture_destroy(base, false);
  delete fixture;
}
TEST_F(CrlSslTransportSecurityTest,
       ssl_tsi_test_do_handshake_with_revoked_client_cert) {
  SslTestFixture* fixture = new SslTestFixture(false, true, &kVtable);
  tsi_test_fixture* base = reinterpret_cast<tsi_test_fixture*>(fixture);
  tsi_test_do_handshake(base);
  tsi_test_fixture_destroy(base, false);
}

TEST_F(CrlSslTransportSecurityTest,
       ssl_tsi_test_do_handshake_with_valid_certs) {
  SslTestFixture* fixture = new SslTestFixture(false, false, &kVtable);
  tsi_test_fixture* base = reinterpret_cast<tsi_test_fixture*>(fixture);
  tsi_test_do_handshake(base);
  tsi_test_fixture_destroy(base, false);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  const size_t number_tls_versions = 2;
  const tsi_tls_version tls_versions[] = {tsi_tls_version::TSI_TLS1_2,
                                          tsi_tls_version::TSI_TLS1_3};
  for (size_t i = 0; i < number_tls_versions; i++) {
    // Set the TLS version to be used in the tests.
    test_tls_version = tls_versions[i];
    // Run all the tests using that TLS version for both the client and
    // server.
    int test_result = RUN_ALL_TESTS();
    if (test_result != 0) {
      return test_result;
    };
  }
  grpc_shutdown();
  return 0;
}
