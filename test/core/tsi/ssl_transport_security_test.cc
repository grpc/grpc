//
//
// Copyright 2017 gRPC authors.
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

#include "src/core/tsi/ssl_transport_security.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <gtest/gtest.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/pem.h>

#include "absl/strings/str_cat.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/tsi/transport_security.h"
#include "src/core/tsi/transport_security_interface.h"
#include "test/core/tsi/transport_security_test_lib.h"
#include "test/core/util/build.h"
#include "test/core/util/test_config.h"

#define SSL_TSI_TEST_ALPN1 "foo"
#define SSL_TSI_TEST_ALPN2 "toto"
#define SSL_TSI_TEST_ALPN3 "baz"
#define SSL_TSI_TEST_ALPN_NUM 2
#define SSL_TSI_TEST_SERVER_KEY_CERT_PAIRS_NUM 2
#define SSL_TSI_TEST_BAD_SERVER_KEY_CERT_PAIRS_NUM 1
#define SSL_TSI_TEST_LEAF_SIGNED_BY_INTERMEDIATE_KEY_CERT_PAIRS_NUM 1
#define SSL_TSI_TEST_CREDENTIALS_DIR "src/core/tsi/test_creds/"
#define SSL_TSI_TEST_WRONG_SNI "test.google.cn"

// OpenSSL 1.1 uses AES256 for encryption session ticket by default so specify
// different STEK size.
#if OPENSSL_VERSION_NUMBER >= 0x10100000 && !defined(OPENSSL_IS_BORINGSSL)
const size_t kSessionTicketEncryptionKeySize = 80;
#else
const size_t kSessionTicketEncryptionKeySize = 48;
#endif

// Indicates the TLS version used for the test.
static tsi_tls_version test_tls_version = tsi_tls_version::TSI_TLS1_3;
static bool test_send_client_ca_list = false;

typedef enum AlpnMode {
  NO_ALPN,
  ALPN_CLIENT_NO_SERVER,
  ALPN_SERVER_NO_CLIENT,
  ALPN_CLIENT_SERVER_OK,
  ALPN_CLIENT_SERVER_MISMATCH
} AlpnMode;

typedef struct ssl_alpn_lib {
  AlpnMode alpn_mode;
  const char** server_alpn_protocols;
  const char** client_alpn_protocols;
  uint16_t num_server_alpn_protocols;
  uint16_t num_client_alpn_protocols;
} ssl_alpn_lib;

typedef struct ssl_key_cert_lib {
  bool use_bad_server_cert;
  bool use_bad_client_cert;
  bool use_root_store;
  bool use_cert_signed_by_intermediate_ca;
  char* root_cert;
  tsi_ssl_root_certs_store* root_store;
  tsi_ssl_pem_key_cert_pair* server_pem_key_cert_pairs;
  tsi_ssl_pem_key_cert_pair* bad_server_pem_key_cert_pairs;
  tsi_ssl_pem_key_cert_pair* leaf_signed_by_intermediate_key_cert_pairs;
  tsi_ssl_pem_key_cert_pair client_pem_key_cert_pair;
  tsi_ssl_pem_key_cert_pair bad_client_pem_key_cert_pair;
  uint16_t server_num_key_cert_pairs;
  uint16_t bad_server_num_key_cert_pairs;
  uint16_t leaf_signed_by_intermediate_num_key_cert_pairs;
} ssl_key_cert_lib;

typedef struct ssl_tsi_test_fixture {
  tsi_test_fixture base;
  ssl_key_cert_lib* key_cert_lib;
  ssl_alpn_lib* alpn_lib;
  bool force_client_auth;
  char* server_name_indication;
  tsi_ssl_session_cache* session_cache;
  bool session_reused;
  const char* session_ticket_key;
  size_t session_ticket_key_size;
  size_t network_bio_buf_size;
  size_t ssl_bio_buf_size;
  tsi_ssl_server_handshaker_factory* server_handshaker_factory;
  tsi_ssl_client_handshaker_factory* client_handshaker_factory;
} ssl_tsi_test_fixture;

static void ssl_test_setup_handshakers(tsi_test_fixture* fixture) {
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ASSERT_NE(ssl_fixture, nullptr);
  ASSERT_NE(ssl_fixture->key_cert_lib, nullptr);
  ASSERT_NE(ssl_fixture->alpn_lib, nullptr);
  ssl_key_cert_lib* key_cert_lib = ssl_fixture->key_cert_lib;
  ssl_alpn_lib* alpn_lib = ssl_fixture->alpn_lib;
  // Create client handshaker factory.
  tsi_ssl_client_handshaker_options client_options;
  client_options.pem_root_certs = key_cert_lib->root_cert;
  if (ssl_fixture->force_client_auth) {
    client_options.pem_key_cert_pair =
        key_cert_lib->use_bad_client_cert
            ? &key_cert_lib->bad_client_pem_key_cert_pair
            : &key_cert_lib->client_pem_key_cert_pair;
  }
  if (alpn_lib->alpn_mode == ALPN_CLIENT_NO_SERVER ||
      alpn_lib->alpn_mode == ALPN_CLIENT_SERVER_OK ||
      alpn_lib->alpn_mode == ALPN_CLIENT_SERVER_MISMATCH) {
    client_options.alpn_protocols = alpn_lib->client_alpn_protocols;
    client_options.num_alpn_protocols = alpn_lib->num_client_alpn_protocols;
  }
  client_options.root_store =
      key_cert_lib->use_root_store ? key_cert_lib->root_store : nullptr;
  if (ssl_fixture->session_cache != nullptr) {
    client_options.session_cache = ssl_fixture->session_cache;
  }
  client_options.min_tls_version = test_tls_version;
  client_options.max_tls_version = test_tls_version;
  ASSERT_EQ(tsi_create_ssl_client_handshaker_factory_with_options(
                &client_options, &ssl_fixture->client_handshaker_factory),
            TSI_OK);
  // Create server handshaker factory.
  tsi_ssl_server_handshaker_options server_options;
  if (alpn_lib->alpn_mode == ALPN_SERVER_NO_CLIENT ||
      alpn_lib->alpn_mode == ALPN_CLIENT_SERVER_OK ||
      alpn_lib->alpn_mode == ALPN_CLIENT_SERVER_MISMATCH) {
    server_options.alpn_protocols = alpn_lib->server_alpn_protocols;
    server_options.num_alpn_protocols = alpn_lib->num_server_alpn_protocols;
    if (alpn_lib->alpn_mode == ALPN_CLIENT_SERVER_MISMATCH) {
      server_options.num_alpn_protocols--;
    }
  }
  if (key_cert_lib->use_cert_signed_by_intermediate_ca) {
    server_options.pem_key_cert_pairs =
        key_cert_lib->leaf_signed_by_intermediate_key_cert_pairs;
    server_options.num_key_cert_pairs =
        key_cert_lib->leaf_signed_by_intermediate_num_key_cert_pairs;
  } else {
    server_options.pem_key_cert_pairs =
        key_cert_lib->use_bad_server_cert
            ? key_cert_lib->bad_server_pem_key_cert_pairs
            : key_cert_lib->server_pem_key_cert_pairs;
    server_options.num_key_cert_pairs =
        key_cert_lib->use_bad_server_cert
            ? key_cert_lib->bad_server_num_key_cert_pairs
            : key_cert_lib->server_num_key_cert_pairs;
  }
  server_options.pem_client_root_certs = key_cert_lib->root_cert;
  if (ssl_fixture->force_client_auth) {
    server_options.client_certificate_request =
        TSI_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;
  } else {
    server_options.client_certificate_request =
        TSI_DONT_REQUEST_CLIENT_CERTIFICATE;
  }
  server_options.send_client_ca_list = test_send_client_ca_list;
  server_options.session_ticket_key = ssl_fixture->session_ticket_key;
  server_options.session_ticket_key_size = ssl_fixture->session_ticket_key_size;
  server_options.min_tls_version = test_tls_version;
  server_options.max_tls_version = test_tls_version;
  ASSERT_EQ(tsi_create_ssl_server_handshaker_factory_with_options(
                &server_options, &ssl_fixture->server_handshaker_factory),
            TSI_OK);
  // Create server and client handshakers.
  ASSERT_EQ(
      tsi_ssl_client_handshaker_factory_create_handshaker(
          ssl_fixture->client_handshaker_factory,
          ssl_fixture->server_name_indication,
          ssl_fixture->network_bio_buf_size, ssl_fixture->ssl_bio_buf_size,
          &ssl_fixture->base.client_handshaker),
      TSI_OK);
  ASSERT_EQ(
      tsi_ssl_server_handshaker_factory_create_handshaker(
          ssl_fixture->server_handshaker_factory,
          ssl_fixture->network_bio_buf_size, ssl_fixture->ssl_bio_buf_size,
          &ssl_fixture->base.server_handshaker),
      TSI_OK);
}

static void check_verified_root_cert_subject(
    ssl_tsi_test_fixture* /*ssl_fixture*/, const tsi_peer* peer) {
  const tsi_peer_property* verified_root_cert_subject =
      tsi_peer_get_property_by_name(
          peer, TSI_X509_VERIFIED_ROOT_CERT_SUBECT_PEER_PROPERTY);
  ASSERT_NE(verified_root_cert_subject, nullptr);
  const char* expected_match =
      "CN=testca,O=Internet Widgits Pty Ltd,ST=Some-State,C=AU";
  ASSERT_EQ(memcmp(verified_root_cert_subject->value.data, expected_match,
                   verified_root_cert_subject->value.length),
            0);
}

static void check_verified_root_cert_subject_unset(
    ssl_tsi_test_fixture* /*ssl_fixture*/, const tsi_peer* peer) {
  const tsi_peer_property* verified_root_cert_subject =
      tsi_peer_get_property_by_name(
          peer, TSI_X509_VERIFIED_ROOT_CERT_SUBECT_PEER_PROPERTY);
  ASSERT_EQ(verified_root_cert_subject, nullptr);
}

static void check_alpn(ssl_tsi_test_fixture* ssl_fixture,
                       const tsi_peer* peer) {
  ASSERT_NE(ssl_fixture, nullptr);
  ASSERT_NE(ssl_fixture->alpn_lib, nullptr);
  ssl_alpn_lib* alpn_lib = ssl_fixture->alpn_lib;
  const tsi_peer_property* alpn_property =
      tsi_peer_get_property_by_name(peer, TSI_SSL_ALPN_SELECTED_PROTOCOL);
  if (alpn_lib->alpn_mode != ALPN_CLIENT_SERVER_OK) {
    ASSERT_EQ(alpn_property, nullptr);
  } else {
    ASSERT_NE(alpn_property, nullptr);
    const char* expected_match = "baz";
    ASSERT_EQ(memcmp(alpn_property->value.data, expected_match,
                     alpn_property->value.length),
              0);
  }
}

static void check_security_level(const tsi_peer* peer) {
  const tsi_peer_property* security_level =
      tsi_peer_get_property_by_name(peer, TSI_SECURITY_LEVEL_PEER_PROPERTY);
  ASSERT_NE(security_level, nullptr);
  const char* expected_match = "TSI_PRIVACY_AND_INTEGRITY";
  ASSERT_EQ(memcmp(security_level->value.data, expected_match,
                   security_level->value.length),
            0);
}

static const tsi_peer_property*
check_basic_authenticated_peer_and_get_common_name(const tsi_peer* peer) {
  const tsi_peer_property* cert_type_property =
      tsi_peer_get_property_by_name(peer, TSI_CERTIFICATE_TYPE_PEER_PROPERTY);
  EXPECT_NE(cert_type_property, nullptr);
  EXPECT_EQ(memcmp(cert_type_property->value.data, TSI_X509_CERTIFICATE_TYPE,
                   cert_type_property->value.length),
            0);
  const tsi_peer_property* property = tsi_peer_get_property_by_name(
      peer, TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY);
  EXPECT_NE(property, nullptr);
  return property;
}

static void check_session_reusage(ssl_tsi_test_fixture* ssl_fixture,
                                  tsi_peer* peer) {
  const tsi_peer_property* session_reused =
      tsi_peer_get_property_by_name(peer, TSI_SSL_SESSION_REUSED_PEER_PROPERTY);
  ASSERT_NE(session_reused, nullptr);
  if (ssl_fixture->session_reused) {
    ASSERT_EQ(strncmp(session_reused->value.data, "true",
                      session_reused->value.length),
              0);
  } else {
    ASSERT_EQ(strncmp(session_reused->value.data, "false",
                      session_reused->value.length),
              0);
  }
}

void check_server0_peer(tsi_peer* peer) {
  const tsi_peer_property* property =
      check_basic_authenticated_peer_and_get_common_name(peer);
  const char* expected_match = "*.test.google.com.au";
  ASSERT_EQ(
      memcmp(property->value.data, expected_match, property->value.length), 0);
  ASSERT_EQ(tsi_peer_get_property_by_name(
                peer, TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY),
            nullptr);
  ASSERT_EQ(tsi_ssl_peer_matches_name(peer, "foo.test.google.com.au"), 1);
  ASSERT_EQ(tsi_ssl_peer_matches_name(peer, "bar.test.google.com.au"), 1);
  ASSERT_EQ(tsi_ssl_peer_matches_name(peer, "BAR.TEST.GOOGLE.COM.AU"), 1);
  ASSERT_EQ(tsi_ssl_peer_matches_name(peer, "Bar.Test.Google.Com.Au"), 1);
  ASSERT_EQ(tsi_ssl_peer_matches_name(peer, "bAr.TeST.gOOgle.cOm.AU"), 1);
  ASSERT_EQ(tsi_ssl_peer_matches_name(peer, "bar.test.google.blah"), 0);
  ASSERT_EQ(tsi_ssl_peer_matches_name(peer, "foo.bar.test.google.com.au"), 0);
  ASSERT_EQ(tsi_ssl_peer_matches_name(peer, "test.google.com.au"), 0);
  tsi_peer_destruct(peer);
}

static bool check_property(tsi_peer* peer, const char* property_name,
                           const char* property_value) {
  for (size_t i = 0; i < peer->property_count; i++) {
    const tsi_peer_property* prop = &peer->properties[i];
    if (strcmp(prop->name, property_name) == 0) {
      if (strlen(property_value) == prop->value.length &&
          memcmp(prop->value.data, property_value, prop->value.length) == 0) {
        return true;
      }
    }
  }
  return false;
}

void check_server1_peer(tsi_peer* peer) {
  const tsi_peer_property* property =
      check_basic_authenticated_peer_and_get_common_name(peer);
  const char* expected_match = "*.test.google.com";
  ASSERT_EQ(
      memcmp(property->value.data, expected_match, property->value.length), 0);
  ASSERT_EQ(
      check_property(peer, TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY,
                     "*.test.google.fr"),
      1);
  ASSERT_EQ(
      check_property(peer, TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY,
                     "waterzooi.test.google.be"),
      1);
  ASSERT_EQ(tsi_ssl_peer_matches_name(peer, "foo.test.google.fr"), 1);
  ASSERT_EQ(tsi_ssl_peer_matches_name(peer, "bar.test.google.fr"), 1);
  ASSERT_EQ(tsi_ssl_peer_matches_name(peer, "waterzooi.test.google.be"), 1);
  ASSERT_EQ(tsi_ssl_peer_matches_name(peer, "foo.test.youtube.com"), 1);
  ASSERT_EQ(tsi_ssl_peer_matches_name(peer, "bar.foo.test.google.com"), 0);
  ASSERT_EQ(tsi_ssl_peer_matches_name(peer, "test.google.fr"), 0);
  ASSERT_EQ(tsi_ssl_peer_matches_name(peer, "tartines.test.google.be"), 0);
  ASSERT_EQ(tsi_ssl_peer_matches_name(peer, "tartines.youtube.com"), 0);
  tsi_peer_destruct(peer);
}

static void check_client_peer(ssl_tsi_test_fixture* ssl_fixture,
                              tsi_peer* peer) {
  ASSERT_NE(ssl_fixture, nullptr);
  ASSERT_NE(ssl_fixture->alpn_lib, nullptr);
  ssl_alpn_lib* alpn_lib = ssl_fixture->alpn_lib;
  if (!ssl_fixture->force_client_auth) {
    ASSERT_EQ(peer->property_count,
              (alpn_lib->alpn_mode == ALPN_CLIENT_SERVER_OK ? 3 : 2));
  } else {
    const tsi_peer_property* property =
        check_basic_authenticated_peer_and_get_common_name(peer);
    const char* expected_match = "testclient";
    ASSERT_EQ(
        memcmp(property->value.data, expected_match, property->value.length),
        0);
  }
  tsi_peer_destruct(peer);
}

static void ssl_test_check_handshaker_peers(tsi_test_fixture* fixture) {
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ASSERT_NE(ssl_fixture, nullptr);
  ASSERT_NE(ssl_fixture->key_cert_lib, nullptr);
  ssl_key_cert_lib* key_cert_lib = ssl_fixture->key_cert_lib;
  tsi_peer peer;
  // In TLS 1.3, the client-side handshake succeeds even if the client sends a
  // bad certificate. In such a case, the server would fail the TLS handshake
  // and send an alert to the client as the first application data message. In
  // TLS 1.2, the client-side handshake will fail if the client sends a bad
  // certificate.
  //
  // For OpenSSL versions < 1.1, TLS 1.3 is not supported, so the client-side
  // handshake should succeed precisely when the server-side handshake
  // succeeds.
  bool expect_server_success =
      !(key_cert_lib->use_bad_server_cert ||
        (key_cert_lib->use_bad_client_cert && ssl_fixture->force_client_auth));
#if OPENSSL_VERSION_NUMBER >= 0x10100000
  bool expect_client_success = test_tls_version == tsi_tls_version::TSI_TLS1_2
                                   ? expect_server_success
                                   : !key_cert_lib->use_bad_server_cert;
#else
  bool expect_client_success = expect_server_success;
#endif
  if (expect_client_success) {
    ASSERT_EQ(tsi_handshaker_result_extract_peer(
                  ssl_fixture->base.client_result, &peer),
              TSI_OK);
    check_session_reusage(ssl_fixture, &peer);
    check_alpn(ssl_fixture, &peer);
    check_security_level(&peer);
    if (!ssl_fixture->session_reused) {
      check_verified_root_cert_subject(ssl_fixture, &peer);
    } else {
      check_verified_root_cert_subject_unset(ssl_fixture, &peer);
    }
    if (ssl_fixture->server_name_indication == nullptr ||
        strcmp(ssl_fixture->server_name_indication, SSL_TSI_TEST_WRONG_SNI) ==
            0) {
      // Expect server to use default server0.pem.
      check_server0_peer(&peer);
    } else {
      // Expect server to use server1.pem.
      check_server1_peer(&peer);
    }
  } else {
    ASSERT_EQ(ssl_fixture->base.client_result, nullptr);
  }
  if (expect_server_success) {
    ASSERT_EQ(tsi_handshaker_result_extract_peer(
                  ssl_fixture->base.server_result, &peer),
              TSI_OK);
    check_session_reusage(ssl_fixture, &peer);
    check_alpn(ssl_fixture, &peer);
    check_security_level(&peer);
    if (ssl_fixture->force_client_auth && !ssl_fixture->session_reused) {
      check_verified_root_cert_subject(ssl_fixture, &peer);
    } else {
      check_verified_root_cert_subject_unset(ssl_fixture, &peer);
    }
    check_client_peer(ssl_fixture, &peer);
  } else {
    ASSERT_EQ(ssl_fixture->base.server_result, nullptr);
  }
}

static void ssl_test_pem_key_cert_pair_destroy(tsi_ssl_pem_key_cert_pair kp) {
  gpr_free(const_cast<char*>(kp.private_key));
  gpr_free(const_cast<char*>(kp.cert_chain));
}

static void ssl_test_destruct(tsi_test_fixture* fixture) {
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  if (ssl_fixture == nullptr) {
    return;
  }
  // Destroy ssl_alpn_lib.
  ssl_alpn_lib* alpn_lib = ssl_fixture->alpn_lib;
  for (size_t i = 0; i < alpn_lib->num_server_alpn_protocols; i++) {
    gpr_free(const_cast<char*>(alpn_lib->server_alpn_protocols[i]));
  }
  gpr_free(alpn_lib->server_alpn_protocols);
  for (size_t i = 0; i < alpn_lib->num_client_alpn_protocols; i++) {
    gpr_free(const_cast<char*>(alpn_lib->client_alpn_protocols[i]));
  }
  gpr_free(alpn_lib->client_alpn_protocols);
  gpr_free(alpn_lib);
  // Destroy ssl_key_cert_lib.
  ssl_key_cert_lib* key_cert_lib = ssl_fixture->key_cert_lib;
  for (size_t i = 0; i < key_cert_lib->server_num_key_cert_pairs; i++) {
    ssl_test_pem_key_cert_pair_destroy(
        key_cert_lib->server_pem_key_cert_pairs[i]);
  }
  gpr_free(key_cert_lib->server_pem_key_cert_pairs);
  for (size_t i = 0; i < key_cert_lib->bad_server_num_key_cert_pairs; i++) {
    ssl_test_pem_key_cert_pair_destroy(
        key_cert_lib->bad_server_pem_key_cert_pairs[i]);
  }
  gpr_free(key_cert_lib->bad_server_pem_key_cert_pairs);
  for (size_t i = 0;
       i < key_cert_lib->leaf_signed_by_intermediate_num_key_cert_pairs; i++) {
    ssl_test_pem_key_cert_pair_destroy(
        key_cert_lib->leaf_signed_by_intermediate_key_cert_pairs[i]);
  }
  gpr_free(key_cert_lib->leaf_signed_by_intermediate_key_cert_pairs);
  ssl_test_pem_key_cert_pair_destroy(key_cert_lib->client_pem_key_cert_pair);
  ssl_test_pem_key_cert_pair_destroy(
      key_cert_lib->bad_client_pem_key_cert_pair);
  gpr_free(key_cert_lib->root_cert);
  tsi_ssl_root_certs_store_destroy(key_cert_lib->root_store);
  gpr_free(key_cert_lib);
  if (ssl_fixture->session_cache != nullptr) {
    tsi_ssl_session_cache_unref(ssl_fixture->session_cache);
  }
  // Unreference others.
  tsi_ssl_server_handshaker_factory_unref(
      ssl_fixture->server_handshaker_factory);
  tsi_ssl_client_handshaker_factory_unref(
      ssl_fixture->client_handshaker_factory);
  gpr_free(ssl_fixture);
}

static const struct tsi_test_fixture_vtable vtable = {
    ssl_test_setup_handshakers, ssl_test_check_handshaker_peers,
    ssl_test_destruct};

static char* load_file(const char* dir_path, const char* file_name) {
  char* file_path = static_cast<char*>(
      gpr_zalloc(sizeof(char) * (strlen(dir_path) + strlen(file_name) + 1)));
  memcpy(file_path, dir_path, strlen(dir_path));
  memcpy(file_path + strlen(dir_path), file_name, strlen(file_name));
  grpc_slice slice;
  EXPECT_EQ(grpc_load_file(file_path, 1, &slice), absl::OkStatus());
  char* data = grpc_slice_to_c_string(slice);
  grpc_slice_unref(slice);
  gpr_free(file_path);
  return data;
}

static bool is_slow_build() {
#if defined(GPR_ARCH_32) || defined(__APPLE__)
  return true;
#else
  return BuiltUnderMsan() || BuiltUnderTsan() || BuiltUnderUbsan();
#endif
}

static std::string GenerateTrustBundle() {
  // Create a trust bundle, consisting of 200 self-signed certs. The self-signed
  // certs have subject DNs that are sufficiently big and complex that they
  // substantially increase the server handshake message size.
  std::string trust_bundle;
  int trust_bundle_size = is_slow_build() ? 20 : 200;
  for (int i = 0; i < trust_bundle_size; ++i) {
    SelfSignedCertificateOptions options;
    options.common_name =
        absl::StrCat("{46f0eaed-6e05-43f5-9289-379104612fc", i, "}");
    options.organization = absl::StrCat("organization-", i);
    options.organizational_unit = absl::StrCat("organizational-unit-", i);
    std::string self_signed_cert = GenerateSelfSignedCertificate(options);
    trust_bundle.append(self_signed_cert);
  }
  return trust_bundle;
}

static tsi_test_fixture* ssl_tsi_test_fixture_create() {
  ssl_tsi_test_fixture* ssl_fixture = grpc_core::Zalloc<ssl_tsi_test_fixture>();
  tsi_test_fixture_init(&ssl_fixture->base);
  ssl_fixture->base.test_unused_bytes = true;
  ssl_fixture->base.vtable = &vtable;
  // Create ssl_key_cert_lib.
  ssl_key_cert_lib* key_cert_lib = grpc_core::Zalloc<ssl_key_cert_lib>();
  key_cert_lib->use_bad_server_cert = false;
  key_cert_lib->use_bad_client_cert = false;
  key_cert_lib->use_root_store = false;
  key_cert_lib->server_num_key_cert_pairs =
      SSL_TSI_TEST_SERVER_KEY_CERT_PAIRS_NUM;
  key_cert_lib->bad_server_num_key_cert_pairs =
      SSL_TSI_TEST_BAD_SERVER_KEY_CERT_PAIRS_NUM;
  key_cert_lib->leaf_signed_by_intermediate_num_key_cert_pairs =
      SSL_TSI_TEST_LEAF_SIGNED_BY_INTERMEDIATE_KEY_CERT_PAIRS_NUM;
  key_cert_lib->server_pem_key_cert_pairs =
      static_cast<tsi_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(tsi_ssl_pem_key_cert_pair) *
                     key_cert_lib->server_num_key_cert_pairs));
  key_cert_lib->bad_server_pem_key_cert_pairs =
      static_cast<tsi_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(tsi_ssl_pem_key_cert_pair) *
                     key_cert_lib->bad_server_num_key_cert_pairs));
  key_cert_lib->leaf_signed_by_intermediate_key_cert_pairs =
      static_cast<tsi_ssl_pem_key_cert_pair*>(gpr_malloc(
          sizeof(tsi_ssl_pem_key_cert_pair) *
          key_cert_lib->leaf_signed_by_intermediate_num_key_cert_pairs));
  key_cert_lib->server_pem_key_cert_pairs[0].private_key =
      load_file(SSL_TSI_TEST_CREDENTIALS_DIR, "server0.key");
  key_cert_lib->server_pem_key_cert_pairs[0].cert_chain =
      load_file(SSL_TSI_TEST_CREDENTIALS_DIR, "server0.pem");
  key_cert_lib->server_pem_key_cert_pairs[1].private_key =
      load_file(SSL_TSI_TEST_CREDENTIALS_DIR, "server1.key");
  key_cert_lib->server_pem_key_cert_pairs[1].cert_chain =
      load_file(SSL_TSI_TEST_CREDENTIALS_DIR, "server1.pem");
  key_cert_lib->bad_server_pem_key_cert_pairs[0].private_key =
      load_file(SSL_TSI_TEST_CREDENTIALS_DIR, "badserver.key");
  key_cert_lib->bad_server_pem_key_cert_pairs[0].cert_chain =
      load_file(SSL_TSI_TEST_CREDENTIALS_DIR, "badserver.pem");
  key_cert_lib->client_pem_key_cert_pair.private_key =
      load_file(SSL_TSI_TEST_CREDENTIALS_DIR, "client.key");
  key_cert_lib->client_pem_key_cert_pair.cert_chain =
      load_file(SSL_TSI_TEST_CREDENTIALS_DIR, "client.pem");
  key_cert_lib->bad_client_pem_key_cert_pair.private_key =
      load_file(SSL_TSI_TEST_CREDENTIALS_DIR, "badclient.key");
  key_cert_lib->bad_client_pem_key_cert_pair.cert_chain =
      load_file(SSL_TSI_TEST_CREDENTIALS_DIR, "badclient.pem");
  key_cert_lib->leaf_signed_by_intermediate_key_cert_pairs[0].private_key =
      load_file(SSL_TSI_TEST_CREDENTIALS_DIR,
                "leaf_signed_by_intermediate.key");
  key_cert_lib->leaf_signed_by_intermediate_key_cert_pairs[0].cert_chain =
      load_file(SSL_TSI_TEST_CREDENTIALS_DIR,
                "leaf_and_intermediate_chain.pem");
  key_cert_lib->root_cert = load_file(SSL_TSI_TEST_CREDENTIALS_DIR, "ca.pem");
  key_cert_lib->root_store =
      tsi_ssl_root_certs_store_create(key_cert_lib->root_cert);
  EXPECT_NE(key_cert_lib->root_store, nullptr);
  ssl_fixture->key_cert_lib = key_cert_lib;
  // Create ssl_alpn_lib.
  ssl_alpn_lib* alpn_lib = grpc_core::Zalloc<ssl_alpn_lib>();
  alpn_lib->server_alpn_protocols = static_cast<const char**>(
      gpr_zalloc(sizeof(char*) * SSL_TSI_TEST_ALPN_NUM));
  alpn_lib->client_alpn_protocols = static_cast<const char**>(
      gpr_zalloc(sizeof(char*) * SSL_TSI_TEST_ALPN_NUM));
  alpn_lib->server_alpn_protocols[0] = gpr_strdup(SSL_TSI_TEST_ALPN1);
  alpn_lib->server_alpn_protocols[1] = gpr_strdup(SSL_TSI_TEST_ALPN3);
  alpn_lib->client_alpn_protocols[0] = gpr_strdup(SSL_TSI_TEST_ALPN2);
  alpn_lib->client_alpn_protocols[1] = gpr_strdup(SSL_TSI_TEST_ALPN3);
  alpn_lib->num_server_alpn_protocols = SSL_TSI_TEST_ALPN_NUM;
  alpn_lib->num_client_alpn_protocols = SSL_TSI_TEST_ALPN_NUM;
  alpn_lib->alpn_mode = NO_ALPN;
  ssl_fixture->alpn_lib = alpn_lib;
  ssl_fixture->base.vtable = &vtable;
  ssl_fixture->server_name_indication = nullptr;
  ssl_fixture->session_reused = false;
  ssl_fixture->session_ticket_key = nullptr;
  ssl_fixture->session_ticket_key_size = 0;
  ssl_fixture->force_client_auth = false;
  ssl_fixture->network_bio_buf_size = 0;
  ssl_fixture->ssl_bio_buf_size = 0;
  return &ssl_fixture->base;
}

void ssl_tsi_test_do_handshake_tiny_handshake_buffer() {
  gpr_log(GPR_INFO, "ssl_tsi_test_do_handshake_tiny_handshake_buffer");
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  fixture->handshake_buffer_size = TSI_TEST_TINY_HANDSHAKE_BUFFER_SIZE;
  // Handshake buffer is too small to hold both handshake messages and the
  // unused bytes.
  fixture->test_unused_bytes = false;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_small_handshake_buffer() {
  gpr_log(GPR_INFO, "ssl_tsi_test_do_handshake_small_handshake_buffer");
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  fixture->handshake_buffer_size = TSI_TEST_SMALL_HANDSHAKE_BUFFER_SIZE;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake() {
  gpr_log(GPR_INFO, "ssl_tsi_test_do_handshake");
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_root_store() {
  gpr_log(GPR_INFO, "ssl_tsi_test_do_handshake_with_root_store");
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->key_cert_lib->use_root_store = true;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_large_server_handshake_messages(
    const std::string& trust_bundle) {
  gpr_log(GPR_INFO,
          "ssl_tsi_test_do_handshake_with_large_server_handshake_messages");
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  // Force the test to read more handshake bytes from the peer than we have room
  // for in the BIO. The default BIO buffer size is 17kB.
  fixture->handshake_buffer_size = 18000;
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  // Make a copy of the root cert and free the original.
  std::string root_cert(ssl_fixture->key_cert_lib->root_cert);
  gpr_free(ssl_fixture->key_cert_lib->root_cert);
  ssl_fixture->key_cert_lib->root_cert = nullptr;
  // Create a new root store, consisting of the root cert that is actually
  // needed and 200 self-signed certs.
  std::string effective_trust_bundle = absl::StrCat(root_cert, trust_bundle);
  tsi_ssl_root_certs_store_destroy(ssl_fixture->key_cert_lib->root_store);
  ssl_fixture->key_cert_lib->root_cert =
      const_cast<char*>(effective_trust_bundle.c_str());
  ssl_fixture->key_cert_lib->root_store =
      tsi_ssl_root_certs_store_create(effective_trust_bundle.c_str());
  ssl_fixture->key_cert_lib->use_root_store = true;
  ssl_fixture->force_client_auth = true;
  tsi_test_do_handshake(fixture);
  // Overwrite the root_cert pointer so that tsi_test_fixture_destroy does not
  // try to gpr_free it.
  ssl_fixture->key_cert_lib->root_cert = nullptr;
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_client_authentication() {
  gpr_log(GPR_INFO, "ssl_tsi_test_do_handshake_with_client_authentication");
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->force_client_auth = true;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_client_authentication_and_root_store() {
  gpr_log(
      GPR_INFO,
      "ssl_tsi_test_do_handshake_with_client_authentication_and_root_store");
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->force_client_auth = true;
  ssl_fixture->key_cert_lib->use_root_store = true;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_server_name_indication_exact_domain() {
  gpr_log(GPR_INFO,
          "ssl_tsi_test_do_handshake_with_server_name_indication_exact_domain");
  // server1 cert contains "waterzooi.test.google.be" in SAN.
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->server_name_indication =
      const_cast<char*>("waterzooi.test.google.be");
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_server_name_indication_wild_star_domain() {
  gpr_log(
      GPR_INFO,
      "ssl_tsi_test_do_handshake_with_server_name_indication_wild_star_domain");
  // server1 cert contains "*.test.google.fr" in SAN.
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->server_name_indication =
      const_cast<char*>("juju.test.google.fr");
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_wrong_server_name_indication() {
  gpr_log(GPR_INFO,
          "ssl_tsi_test_do_handshake_with_wrong_server_name_indication");
  // server certs do not contain "test.google.cn".
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->server_name_indication =
      const_cast<char*>(SSL_TSI_TEST_WRONG_SNI);
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_bad_server_cert() {
  gpr_log(GPR_INFO, "ssl_tsi_test_do_handshake_with_bad_server_cert");
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->key_cert_lib->use_bad_server_cert = true;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_bad_client_cert() {
  gpr_log(GPR_INFO, "ssl_tsi_test_do_handshake_with_bad_client_cert");
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->key_cert_lib->use_bad_client_cert = true;
  ssl_fixture->force_client_auth = true;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_alpn_client_no_server() {
  gpr_log(GPR_INFO, "ssl_tsi_test_do_handshake_alpn_client_no_server");
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->alpn_lib->alpn_mode = ALPN_CLIENT_NO_SERVER;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_alpn_server_no_client() {
  gpr_log(GPR_INFO, "ssl_tsi_test_do_handshake_alpn_server_no_client");
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->alpn_lib->alpn_mode = ALPN_SERVER_NO_CLIENT;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_alpn_client_server_mismatch() {
  gpr_log(GPR_INFO, "ssl_tsi_test_do_handshake_alpn_server_no_client");
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->alpn_lib->alpn_mode = ALPN_CLIENT_SERVER_MISMATCH;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_alpn_client_server_ok() {
  gpr_log(GPR_INFO, "ssl_tsi_test_do_handshake_alpn_client_server_ok");
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->alpn_lib->alpn_mode = ALPN_CLIENT_SERVER_OK;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_round_trip_for_all_configs() {
  gpr_log(GPR_INFO, "ssl_tsi_test_do_round_trip_for_all_configs");
  unsigned int* bit_array = static_cast<unsigned int*>(
      gpr_zalloc(sizeof(unsigned int) * TSI_TEST_NUM_OF_ARGUMENTS));
  const unsigned int mask = 1U << (TSI_TEST_NUM_OF_ARGUMENTS - 1);
  for (unsigned int val = 0; val < TSI_TEST_NUM_OF_COMBINATIONS; val++) {
    unsigned int v = val;
    for (unsigned int ind = 0; ind < TSI_TEST_NUM_OF_ARGUMENTS; ind++) {
      bit_array[ind] = (v & mask) ? 1 : 0;
      v <<= 1;
    }
    tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
    ssl_tsi_test_fixture* ssl_fixture =
        reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
    tsi_test_frame_protector_config_destroy(ssl_fixture->base.config);
    ssl_fixture->base.config = tsi_test_frame_protector_config_create(
        bit_array[0], bit_array[1], bit_array[2], bit_array[3], bit_array[4],
        bit_array[5], bit_array[6]);
    tsi_test_do_round_trip(&ssl_fixture->base);
    tsi_test_fixture_destroy(fixture);
  }
  gpr_free(bit_array);
}

void ssl_tsi_test_do_round_trip_with_error_on_stack() {
  gpr_log(GPR_INFO, "ssl_tsi_test_do_round_trip_with_error_on_stack");
  // Invoke an SSL function that causes an error, and ensure the error
  // makes it to the stack.
  ASSERT_FALSE(EC_KEY_new_by_curve_name(NID_rsa));
  ASSERT_NE(ERR_peek_error(), 0);
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  tsi_test_do_round_trip(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_round_trip_odd_buffer_size() {
  gpr_log(GPR_INFO, "ssl_tsi_test_do_round_trip_odd_buffer_size");
  const size_t odd_sizes[] = {1025, 2051, 4103, 8207, 16409};
  size_t size = sizeof(odd_sizes) / sizeof(size_t);
  // 1. This test is extremely slow under MSAN and TSAN.
  // 2. On 32-bit, the test is much slower (probably due to lack of boringssl
  // asm optimizations) so we only run a subset of tests to avoid timeout.
  // 3. On Mac OS, we have slower testing machines so we only run a subset
  // of tests to avoid timeout.
  if (is_slow_build()) {
    size = 1;
  }
  for (size_t ind1 = 0; ind1 < size; ind1++) {
    for (size_t ind2 = 0; ind2 < size; ind2++) {
      for (size_t ind3 = 0; ind3 < size; ind3++) {
        for (size_t ind4 = 0; ind4 < size; ind4++) {
          for (size_t ind5 = 0; ind5 < size; ind5++) {
            tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
            ssl_tsi_test_fixture* ssl_fixture =
                reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
            tsi_test_frame_protector_config_set_buffer_size(
                ssl_fixture->base.config, odd_sizes[ind1], odd_sizes[ind2],
                odd_sizes[ind3], odd_sizes[ind4], odd_sizes[ind5]);
            tsi_test_do_round_trip(&ssl_fixture->base);
            tsi_test_fixture_destroy(fixture);
          }
        }
      }
    }
  }
}

void ssl_tsi_test_do_handshake_session_cache() {
  gpr_log(GPR_INFO, "ssl_tsi_test_do_handshake_session_cache");
  tsi_ssl_session_cache* session_cache = tsi_ssl_session_cache_create_lru(16);
  char session_ticket_key[kSessionTicketEncryptionKeySize];
  auto do_handshake = [&session_ticket_key,
                       &session_cache](bool session_reused) {
    tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
    ssl_tsi_test_fixture* ssl_fixture =
        reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
    ssl_fixture->server_name_indication =
        const_cast<char*>("waterzooi.test.google.be");
    ssl_fixture->session_ticket_key = session_ticket_key;
    ssl_fixture->session_ticket_key_size = sizeof(session_ticket_key);
    tsi_ssl_session_cache_ref(session_cache);
    ssl_fixture->session_cache = session_cache;
    ssl_fixture->session_reused = session_reused;
    tsi_test_do_round_trip(&ssl_fixture->base);
    tsi_test_fixture_destroy(fixture);
  };
  memset(session_ticket_key, 'a', sizeof(session_ticket_key));
  do_handshake(false);
  do_handshake(true);
  do_handshake(true);
  // Changing session_ticket_key on server invalidates ticket.
  memset(session_ticket_key, 'b', sizeof(session_ticket_key));
  do_handshake(false);
  do_handshake(true);
  memset(session_ticket_key, 'c', sizeof(session_ticket_key));
  do_handshake(false);
  do_handshake(true);
  tsi_ssl_session_cache_unref(session_cache);
}

void ssl_tsi_test_do_handshake_with_intermediate_ca() {
  gpr_log(
      GPR_INFO,
      "ssl_tsi_test_do_handshake_with_client_authentication_and_root_store");
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->force_client_auth = true;
  ssl_fixture->key_cert_lib->use_root_store = true;
  ssl_fixture->key_cert_lib->use_cert_signed_by_intermediate_ca = true;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

static const tsi_ssl_handshaker_factory_vtable* original_vtable;
static bool handshaker_factory_destructor_called;

static void ssl_tsi_test_handshaker_factory_destructor(
    tsi_ssl_handshaker_factory* factory) {
  ASSERT_NE(factory, nullptr);
  handshaker_factory_destructor_called = true;
  if (original_vtable != nullptr && original_vtable->destroy != nullptr) {
    original_vtable->destroy(factory);
  }
}

static tsi_ssl_handshaker_factory_vtable test_handshaker_factory_vtable = {
    ssl_tsi_test_handshaker_factory_destructor};

void test_tsi_ssl_client_handshaker_factory_refcounting() {
  int i;
  char* cert_chain = load_file(SSL_TSI_TEST_CREDENTIALS_DIR, "client.pem");

  tsi_ssl_client_handshaker_options options;
  options.pem_root_certs = cert_chain;
  tsi_ssl_client_handshaker_factory* client_handshaker_factory;
  ASSERT_EQ(tsi_create_ssl_client_handshaker_factory_with_options(
                &options, &client_handshaker_factory),
            TSI_OK);

  handshaker_factory_destructor_called = false;
  original_vtable = tsi_ssl_handshaker_factory_swap_vtable(
      reinterpret_cast<tsi_ssl_handshaker_factory*>(client_handshaker_factory),
      &test_handshaker_factory_vtable);

  tsi_handshaker* handshaker[3];

  for (i = 0; i < 3; ++i) {
    ASSERT_EQ(
        tsi_ssl_client_handshaker_factory_create_handshaker(
            client_handshaker_factory, "google.com", 0, 0, &handshaker[i]),
        TSI_OK);
  }

  client_handshaker_factory =
      tsi_ssl_client_handshaker_factory_ref(client_handshaker_factory);

  tsi_handshaker_destroy(handshaker[1]);
  ASSERT_FALSE(handshaker_factory_destructor_called);

  tsi_handshaker_destroy(handshaker[0]);
  ASSERT_FALSE(handshaker_factory_destructor_called);

  tsi_ssl_client_handshaker_factory_unref(client_handshaker_factory);
  ASSERT_FALSE(handshaker_factory_destructor_called);

  tsi_handshaker_destroy(handshaker[2]);
  ASSERT_FALSE(handshaker_factory_destructor_called);

  tsi_ssl_client_handshaker_factory_unref(client_handshaker_factory);
  ASSERT_TRUE(handshaker_factory_destructor_called);
  gpr_free(cert_chain);
}

void test_tsi_ssl_server_handshaker_factory_refcounting() {
  int i;
  tsi_ssl_server_handshaker_factory* server_handshaker_factory;
  tsi_handshaker* handshaker[3];
  const char* cert_chain =
      load_file(SSL_TSI_TEST_CREDENTIALS_DIR, "server0.pem");
  tsi_ssl_pem_key_cert_pair cert_pair;

  cert_pair.cert_chain = cert_chain;
  cert_pair.private_key =
      load_file(SSL_TSI_TEST_CREDENTIALS_DIR, "server0.key");
  tsi_ssl_server_handshaker_options options;
  options.pem_key_cert_pairs = &cert_pair;
  options.num_key_cert_pairs = 1;
  options.pem_client_root_certs = cert_chain;

  ASSERT_EQ(tsi_create_ssl_server_handshaker_factory_with_options(
                &options, &server_handshaker_factory),
            TSI_OK);

  handshaker_factory_destructor_called = false;
  original_vtable = tsi_ssl_handshaker_factory_swap_vtable(
      reinterpret_cast<tsi_ssl_handshaker_factory*>(server_handshaker_factory),
      &test_handshaker_factory_vtable);

  for (i = 0; i < 3; ++i) {
    ASSERT_EQ(tsi_ssl_server_handshaker_factory_create_handshaker(
                  server_handshaker_factory, 0, 0, &handshaker[i]),
              TSI_OK);
  }

  tsi_handshaker_destroy(handshaker[1]);
  ASSERT_FALSE(handshaker_factory_destructor_called);

  tsi_handshaker_destroy(handshaker[0]);
  ASSERT_FALSE(handshaker_factory_destructor_called);

  tsi_ssl_server_handshaker_factory_unref(server_handshaker_factory);
  ASSERT_FALSE(handshaker_factory_destructor_called);

  tsi_handshaker_destroy(handshaker[2]);
  ASSERT_TRUE(handshaker_factory_destructor_called);

  ssl_test_pem_key_cert_pair_destroy(cert_pair);
}

// Attempting to create a handshaker factory with invalid parameters should fail
// but not crash.
void test_tsi_ssl_client_handshaker_factory_bad_params() {
  const char* cert_chain = "This is not a valid PEM file.";

  tsi_ssl_client_handshaker_factory* client_handshaker_factory;
  tsi_ssl_client_handshaker_options options;
  options.pem_root_certs = cert_chain;
  ASSERT_EQ(tsi_create_ssl_client_handshaker_factory_with_options(
                &options, &client_handshaker_factory),
            TSI_INVALID_ARGUMENT);
  tsi_ssl_client_handshaker_factory_unref(client_handshaker_factory);
}

void ssl_tsi_test_handshaker_factory_internals() {
  gpr_log(GPR_INFO, "ssl_tsi_test_handshaker_factory_internals");
  test_tsi_ssl_client_handshaker_factory_refcounting();
  test_tsi_ssl_server_handshaker_factory_refcounting();
  test_tsi_ssl_client_handshaker_factory_bad_params();
}

void ssl_tsi_test_duplicate_root_certificates() {
  gpr_log(GPR_INFO, "ssl_tsi_test_duplicate_root_certificates");
  char* root_cert = load_file(SSL_TSI_TEST_CREDENTIALS_DIR, "ca.pem");
  char* dup_root_cert = static_cast<char*>(
      gpr_zalloc(sizeof(char) * (strlen(root_cert) * 2 + 1)));
  memcpy(dup_root_cert, root_cert, strlen(root_cert));
  memcpy(dup_root_cert + strlen(root_cert), root_cert, strlen(root_cert));
  tsi_ssl_root_certs_store* root_store =
      tsi_ssl_root_certs_store_create(dup_root_cert);
  ASSERT_NE(root_store, nullptr);
  // Free memory.
  tsi_ssl_root_certs_store_destroy(root_store);
  gpr_free(root_cert);
  gpr_free(dup_root_cert);
}

void ssl_tsi_test_extract_x509_subject_names() {
  gpr_log(GPR_INFO, "ssl_tsi_test_extract_x509_subject_names");
  char* cert = load_file(SSL_TSI_TEST_CREDENTIALS_DIR, "multi-domain.pem");
  tsi_peer peer;
  ASSERT_EQ(tsi_ssl_extract_x509_subject_names_from_pem_cert(cert, &peer),
            TSI_OK);
  // tsi_peer should include one subject, one common name, one certificate, one
  // security level, ten SAN fields, two DNS SAN fields, three URI fields, two
  // email addresses and two IP addresses.
  size_t expected_property_count = 22;
  ASSERT_EQ(peer.property_count, expected_property_count);
  // Check subject
  const char* expected_subject = "CN=xpigors,OU=Google,L=SF,ST=CA,C=US";
  const tsi_peer_property* property =
      tsi_peer_get_property_by_name(&peer, TSI_X509_SUBJECT_PEER_PROPERTY);
  ASSERT_NE(property, nullptr);
  ASSERT_EQ(
      memcmp(property->value.data, expected_subject, property->value.length),
      0);
  // Check common name
  const char* expected_cn = "xpigors";
  property = tsi_peer_get_property_by_name(
      &peer, TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY);
  ASSERT_NE(property, nullptr);
  ASSERT_EQ(memcmp(property->value.data, expected_cn, property->value.length),
            0);
  // Check certificate data
  property = tsi_peer_get_property_by_name(&peer, TSI_X509_PEM_CERT_PROPERTY);
  ASSERT_NE(property, nullptr);
  ASSERT_EQ(memcmp(property->value.data, cert, property->value.length), 0);
  // Check DNS
  ASSERT_EQ(
      check_property(&peer, TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY,
                     "foo.test.domain.com"),
      1);
  ASSERT_EQ(
      check_property(&peer, TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY,
                     "bar.test.domain.com"),
      1);
  ASSERT_EQ(
      check_property(&peer, TSI_X509_DNS_PEER_PROPERTY, "foo.test.domain.com"),
      1);
  ASSERT_EQ(
      check_property(&peer, TSI_X509_DNS_PEER_PROPERTY, "bar.test.domain.com"),
      1);
  // Check URI
  // Note that a valid SPIFFE certificate should only have one URI.
  ASSERT_EQ(
      check_property(&peer, TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY,
                     "spiffe://foo.com/bar/baz"),
      1);
  ASSERT_EQ(
      check_property(&peer, TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY,
                     "https://foo.test.domain.com/test"),
      1);
  ASSERT_EQ(
      check_property(&peer, TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY,
                     "https://bar.test.domain.com/test"),
      1);
  ASSERT_EQ(check_property(&peer, TSI_X509_URI_PEER_PROPERTY,
                           "spiffe://foo.com/bar/baz"),
            1);
  ASSERT_EQ(check_property(&peer, TSI_X509_URI_PEER_PROPERTY,
                           "https://foo.test.domain.com/test"),
            1);
  ASSERT_EQ(check_property(&peer, TSI_X509_URI_PEER_PROPERTY,
                           "https://bar.test.domain.com/test"),
            1);
  // Check email address
  ASSERT_EQ(
      check_property(&peer, TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY,
                     "foo@test.domain.com"),
      1);
  ASSERT_EQ(
      check_property(&peer, TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY,
                     "bar@test.domain.com"),
      1);
  ASSERT_EQ(check_property(&peer, TSI_X509_EMAIL_PEER_PROPERTY,
                           "foo@test.domain.com"),
            1);
  ASSERT_EQ(check_property(&peer, TSI_X509_EMAIL_PEER_PROPERTY,
                           "bar@test.domain.com"),
            1);
  // Check ip address
  ASSERT_EQ(
      check_property(&peer, TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY,
                     "192.168.7.1"),
      1);
  ASSERT_EQ(
      check_property(&peer, TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY,
                     "13::17"),
      1);
  ASSERT_EQ(check_property(&peer, TSI_X509_IP_PEER_PROPERTY, "192.168.7.1"), 1);
  ASSERT_EQ(check_property(&peer, TSI_X509_IP_PEER_PROPERTY, "13::17"), 1);
  // Check other fields
  ASSERT_EQ(
      check_property(&peer, TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY,
                     "other types of SAN"),
      1);
  // Free memory
  gpr_free(cert);
  tsi_peer_destruct(&peer);
}

void ssl_tsi_test_extract_cert_chain() {
  gpr_log(GPR_INFO, "ssl_tsi_test_extract_cert_chain");
  char* cert = load_file(SSL_TSI_TEST_CREDENTIALS_DIR, "server1.pem");
  char* ca = load_file(SSL_TSI_TEST_CREDENTIALS_DIR, "ca.pem");
  char* chain = static_cast<char*>(
      gpr_zalloc(sizeof(char) * (strlen(cert) + strlen(ca) + 1)));
  memcpy(chain, cert, strlen(cert));
  memcpy(chain + strlen(cert), ca, strlen(ca));
  STACK_OF(X509)* cert_chain = sk_X509_new_null();
  ASSERT_NE(cert_chain, nullptr);
  BIO* bio = BIO_new_mem_buf(chain, strlen(chain));
  ASSERT_NE(bio, nullptr);
  STACK_OF(X509_INFO)* certInfos =
      PEM_X509_INFO_read_bio(bio, nullptr, nullptr, nullptr);
  ASSERT_NE(certInfos, nullptr);
  for (size_t i = 0; i < sk_X509_INFO_num(certInfos); i++) {
    X509_INFO* certInfo = sk_X509_INFO_value(certInfos, i);
    if (certInfo->x509 != nullptr) {
      ASSERT_NE(sk_X509_push(cert_chain, certInfo->x509), 0);
#if OPENSSL_VERSION_NUMBER >= 0x10100000
      X509_up_ref(certInfo->x509);
#else
      certInfo->x509->references += 1;
#endif
    }
  }
  tsi_peer_property chain_property;
  ASSERT_EQ(tsi_ssl_get_cert_chain_contents(cert_chain, &chain_property),
            TSI_OK);
  ASSERT_EQ(
      memcmp(chain, chain_property.value.data, chain_property.value.length), 0);
  BIO_free(bio);
  gpr_free(chain);
  gpr_free(cert);
  gpr_free(ca);
  tsi_peer_property_destruct(&chain_property);
  sk_X509_INFO_pop_free(certInfos, X509_INFO_free);
  sk_X509_pop_free(cert_chain, X509_free);
}

void ssl_tsi_test_do_handshake_with_custom_bio_pair() {
  gpr_log(GPR_INFO, "ssl_tsi_test_do_handshake_with_custom_bio_pair");
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
#if OPENSSL_VERSION_NUMBER >= 0x10100000
  ssl_fixture->network_bio_buf_size = TSI_TEST_DEFAULT_BUFFER_SIZE;
  ssl_fixture->ssl_bio_buf_size = 256;
#endif
  ssl_fixture->force_client_auth = true;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

TEST(SslTransportSecurityTest, MainTest) {
  grpc_init();
  std::string trust_bundle = GenerateTrustBundle();
  const size_t number_tls_versions = 2;
  const tsi_tls_version tls_versions[] = {tsi_tls_version::TSI_TLS1_2,
                                          tsi_tls_version::TSI_TLS1_3};
  for (size_t i = 0; i < number_tls_versions; i++) {
    // Set the TLS version to be used in the tests.
    test_tls_version = tls_versions[i];
    for (bool send_client_ca_list : {true, false}) {
      test_send_client_ca_list = send_client_ca_list;
      ssl_tsi_test_do_handshake_tiny_handshake_buffer();
      ssl_tsi_test_do_handshake_small_handshake_buffer();
      ssl_tsi_test_do_handshake();
      ssl_tsi_test_do_handshake_with_root_store();
      ssl_tsi_test_do_handshake_with_large_server_handshake_messages(
          trust_bundle);
      ssl_tsi_test_do_handshake_with_client_authentication();
      ssl_tsi_test_do_handshake_with_client_authentication_and_root_store();
      ssl_tsi_test_do_handshake_with_server_name_indication_exact_domain();
      ssl_tsi_test_do_handshake_with_server_name_indication_wild_star_domain();
      ssl_tsi_test_do_handshake_with_wrong_server_name_indication();
      ssl_tsi_test_do_handshake_with_bad_server_cert();
      ssl_tsi_test_do_handshake_with_bad_client_cert();
#ifdef OPENSSL_IS_BORINGSSL
      // BoringSSL and OpenSSL have different behaviors on mismatched ALPN.
      ssl_tsi_test_do_handshake_alpn_client_no_server();
      ssl_tsi_test_do_handshake_alpn_client_server_mismatch();
      // These tests fail with openssl3 and openssl111 currently but not
      // boringssl
      ssl_tsi_test_do_handshake_session_cache();
      ssl_tsi_test_do_round_trip_for_all_configs();
      ssl_tsi_test_do_round_trip_with_error_on_stack();
      ssl_tsi_test_do_round_trip_odd_buffer_size();
#endif
      ssl_tsi_test_do_handshake_alpn_server_no_client();
      ssl_tsi_test_do_handshake_alpn_client_server_ok();
      ssl_tsi_test_handshaker_factory_internals();
      ssl_tsi_test_duplicate_root_certificates();
      ssl_tsi_test_extract_x509_subject_names();
      ssl_tsi_test_extract_cert_chain();
      ssl_tsi_test_do_handshake_with_custom_bio_pair();
      ssl_tsi_test_do_handshake_with_intermediate_ca();
    }
  }
  grpc_shutdown();
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
