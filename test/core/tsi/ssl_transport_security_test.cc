/*
 *
 * Copyright 2017 gRPC authors.
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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security.h"
#include "src/core/tsi/transport_security_adapter.h"
#include "src/core/tsi/transport_security_interface.h"
#include "test/core/tsi/transport_security_test_lib.h"
#include "test/core/util/test_config.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

extern "C" {
#include <openssl/crypto.h>
}

#define SSL_TSI_TEST_ALPN1 "foo"
#define SSL_TSI_TEST_ALPN2 "toto"
#define SSL_TSI_TEST_ALPN3 "baz"
#define SSL_TSI_TEST_ALPN_NUM 2
#define SSL_TSI_TEST_SERVER_KEY_CERT_PAIRS_NUM 2
#define SSL_TSI_TEST_BAD_SERVER_KEY_CERT_PAIRS_NUM 1
#define SSL_TSI_TEST_CREDENTIALS_DIR "src/core/tsi/test_creds/"

// OpenSSL 1.1 uses AES256 for encryption session ticket by default so specify
// different STEK size.
#if OPENSSL_VERSION_NUMBER >= 0x10100000 && !defined(OPENSSL_IS_BORINGSSL)
const size_t kSessionTicketEncryptionKeySize = 80;
#else
const size_t kSessionTicketEncryptionKeySize = 48;
#endif

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
  char* root_cert;
  tsi_ssl_root_certs_store* root_store;
  tsi_ssl_pem_key_cert_pair* server_pem_key_cert_pairs;
  tsi_ssl_pem_key_cert_pair* bad_server_pem_key_cert_pairs;
  tsi_ssl_pem_key_cert_pair client_pem_key_cert_pair;
  tsi_ssl_pem_key_cert_pair bad_client_pem_key_cert_pair;
  uint16_t server_num_key_cert_pairs;
  uint16_t bad_server_num_key_cert_pairs;
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
  tsi_ssl_server_handshaker_factory* server_handshaker_factory;
  tsi_ssl_client_handshaker_factory* client_handshaker_factory;
} ssl_tsi_test_fixture;

static void ssl_test_setup_handshakers(tsi_test_fixture* fixture) {
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  GPR_ASSERT(ssl_fixture != nullptr);
  GPR_ASSERT(ssl_fixture->key_cert_lib != nullptr);
  GPR_ASSERT(ssl_fixture->alpn_lib != nullptr);
  ssl_key_cert_lib* key_cert_lib = ssl_fixture->key_cert_lib;
  ssl_alpn_lib* alpn_lib = ssl_fixture->alpn_lib;
  /* Create client handshaker factory. */
  tsi_ssl_client_handshaker_options client_options;
  memset(&client_options, 0, sizeof(client_options));
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
  GPR_ASSERT(tsi_create_ssl_client_handshaker_factory_with_options(
                 &client_options, &ssl_fixture->client_handshaker_factory) ==
             TSI_OK);
  /* Create server handshaker factory. */
  tsi_ssl_server_handshaker_options server_options;
  memset(&server_options, 0, sizeof(server_options));
  if (alpn_lib->alpn_mode == ALPN_SERVER_NO_CLIENT ||
      alpn_lib->alpn_mode == ALPN_CLIENT_SERVER_OK ||
      alpn_lib->alpn_mode == ALPN_CLIENT_SERVER_MISMATCH) {
    server_options.alpn_protocols = alpn_lib->server_alpn_protocols;
    server_options.num_alpn_protocols = alpn_lib->num_server_alpn_protocols;
    if (alpn_lib->alpn_mode == ALPN_CLIENT_SERVER_MISMATCH) {
      server_options.num_alpn_protocols--;
    }
  }
  server_options.pem_key_cert_pairs =
      key_cert_lib->use_bad_server_cert
          ? key_cert_lib->bad_server_pem_key_cert_pairs
          : key_cert_lib->server_pem_key_cert_pairs;
  server_options.num_key_cert_pairs =
      key_cert_lib->use_bad_server_cert
          ? key_cert_lib->bad_server_num_key_cert_pairs
          : key_cert_lib->server_num_key_cert_pairs;
  server_options.pem_client_root_certs = key_cert_lib->root_cert;
  if (ssl_fixture->force_client_auth) {
    server_options.client_certificate_request =
        TSI_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;
  } else {
    server_options.client_certificate_request =
        TSI_DONT_REQUEST_CLIENT_CERTIFICATE;
  }
  server_options.session_ticket_key = ssl_fixture->session_ticket_key;
  server_options.session_ticket_key_size = ssl_fixture->session_ticket_key_size;
  GPR_ASSERT(tsi_create_ssl_server_handshaker_factory_with_options(
                 &server_options, &ssl_fixture->server_handshaker_factory) ==
             TSI_OK);
  /* Create server and client handshakers. */
  tsi_handshaker* client_handshaker = nullptr;
  GPR_ASSERT(tsi_ssl_client_handshaker_factory_create_handshaker(
                 ssl_fixture->client_handshaker_factory,
                 ssl_fixture->server_name_indication,
                 &client_handshaker) == TSI_OK);
  ssl_fixture->base.client_handshaker =
      tsi_create_adapter_handshaker(client_handshaker);
  tsi_handshaker* server_handshaker = nullptr;
  GPR_ASSERT(tsi_ssl_server_handshaker_factory_create_handshaker(
                 ssl_fixture->server_handshaker_factory, &server_handshaker) ==
             TSI_OK);
  ssl_fixture->base.server_handshaker =
      tsi_create_adapter_handshaker(server_handshaker);
}

static void check_alpn(ssl_tsi_test_fixture* ssl_fixture,
                       const tsi_peer* peer) {
  GPR_ASSERT(ssl_fixture != nullptr);
  GPR_ASSERT(ssl_fixture->alpn_lib != nullptr);
  ssl_alpn_lib* alpn_lib = ssl_fixture->alpn_lib;
  const tsi_peer_property* alpn_property =
      tsi_peer_get_property_by_name(peer, TSI_SSL_ALPN_SELECTED_PROTOCOL);
  if (alpn_lib->alpn_mode != ALPN_CLIENT_SERVER_OK) {
    GPR_ASSERT(alpn_property == nullptr);
  } else {
    GPR_ASSERT(alpn_property != nullptr);
    const char* expected_match = "baz";
    GPR_ASSERT(memcmp(alpn_property->value.data, expected_match,
                      alpn_property->value.length) == 0);
  }
}

static const tsi_peer_property*
check_basic_authenticated_peer_and_get_common_name(const tsi_peer* peer) {
  const tsi_peer_property* cert_type_property =
      tsi_peer_get_property_by_name(peer, TSI_CERTIFICATE_TYPE_PEER_PROPERTY);
  GPR_ASSERT(cert_type_property != nullptr);
  GPR_ASSERT(memcmp(cert_type_property->value.data, TSI_X509_CERTIFICATE_TYPE,
                    cert_type_property->value.length) == 0);
  const tsi_peer_property* property = tsi_peer_get_property_by_name(
      peer, TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY);
  GPR_ASSERT(property != nullptr);
  return property;
}

static void check_session_reusage(ssl_tsi_test_fixture* ssl_fixture,
                                  tsi_peer* peer) {
  const tsi_peer_property* session_reused =
      tsi_peer_get_property_by_name(peer, TSI_SSL_SESSION_REUSED_PEER_PROPERTY);
  GPR_ASSERT(session_reused != nullptr);
  if (ssl_fixture->session_reused) {
    GPR_ASSERT(strcmp(session_reused->value.data, "true") == 0);
  } else {
    GPR_ASSERT(strcmp(session_reused->value.data, "false") == 0);
  }
}

void check_server0_peer(tsi_peer* peer) {
  const tsi_peer_property* property =
      check_basic_authenticated_peer_and_get_common_name(peer);
  const char* expected_match = "*.test.google.com.au";
  GPR_ASSERT(memcmp(property->value.data, expected_match,
                    property->value.length) == 0);
  GPR_ASSERT(tsi_peer_get_property_by_name(
                 peer, TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY) ==
             nullptr);
  GPR_ASSERT(tsi_ssl_peer_matches_name(peer, "foo.test.google.com.au") == 1);
  GPR_ASSERT(tsi_ssl_peer_matches_name(peer, "bar.test.google.com.au") == 1);
  GPR_ASSERT(tsi_ssl_peer_matches_name(peer, "bar.test.google.blah") == 0);
  GPR_ASSERT(tsi_ssl_peer_matches_name(peer, "foo.bar.test.google.com.au") ==
             0);
  GPR_ASSERT(tsi_ssl_peer_matches_name(peer, "test.google.com.au") == 0);
  tsi_peer_destruct(peer);
}

static bool check_subject_alt_name(tsi_peer* peer, const char* name) {
  for (size_t i = 0; i < peer->property_count; i++) {
    const tsi_peer_property* prop = &peer->properties[i];
    if (strcmp(prop->name, TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY) ==
        0) {
      if (memcmp(prop->value.data, name, prop->value.length) == 0) {
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
  GPR_ASSERT(memcmp(property->value.data, expected_match,
                    property->value.length) == 0);
  GPR_ASSERT(check_subject_alt_name(peer, "*.test.google.fr") == 1);
  GPR_ASSERT(check_subject_alt_name(peer, "waterzooi.test.google.be") == 1);
  GPR_ASSERT(tsi_ssl_peer_matches_name(peer, "foo.test.google.fr") == 1);
  GPR_ASSERT(tsi_ssl_peer_matches_name(peer, "bar.test.google.fr") == 1);
  GPR_ASSERT(tsi_ssl_peer_matches_name(peer, "waterzooi.test.google.be") == 1);
  GPR_ASSERT(tsi_ssl_peer_matches_name(peer, "foo.test.youtube.com") == 1);
  GPR_ASSERT(tsi_ssl_peer_matches_name(peer, "bar.foo.test.google.com") == 0);
  GPR_ASSERT(tsi_ssl_peer_matches_name(peer, "test.google.fr") == 0);
  GPR_ASSERT(tsi_ssl_peer_matches_name(peer, "tartines.test.google.be") == 0);
  GPR_ASSERT(tsi_ssl_peer_matches_name(peer, "tartines.youtube.com") == 0);
  tsi_peer_destruct(peer);
}

static void check_client_peer(ssl_tsi_test_fixture* ssl_fixture,
                              tsi_peer* peer) {
  GPR_ASSERT(ssl_fixture != nullptr);
  GPR_ASSERT(ssl_fixture->alpn_lib != nullptr);
  ssl_alpn_lib* alpn_lib = ssl_fixture->alpn_lib;
  if (!ssl_fixture->force_client_auth) {
    GPR_ASSERT(peer->property_count ==
               (alpn_lib->alpn_mode == ALPN_CLIENT_SERVER_OK ? 2 : 1));
  } else {
    const tsi_peer_property* property =
        check_basic_authenticated_peer_and_get_common_name(peer);
    const char* expected_match = "testclient";
    GPR_ASSERT(memcmp(property->value.data, expected_match,
                      property->value.length) == 0);
  }
  tsi_peer_destruct(peer);
}

static void ssl_test_check_handshaker_peers(tsi_test_fixture* fixture) {
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  GPR_ASSERT(ssl_fixture != nullptr);
  GPR_ASSERT(ssl_fixture->key_cert_lib != nullptr);
  ssl_key_cert_lib* key_cert_lib = ssl_fixture->key_cert_lib;
  tsi_peer peer;
  bool expect_success =
      !(key_cert_lib->use_bad_server_cert ||
        (key_cert_lib->use_bad_client_cert && ssl_fixture->force_client_auth));
  if (expect_success) {
    GPR_ASSERT(tsi_handshaker_result_extract_peer(
                   ssl_fixture->base.client_result, &peer) == TSI_OK);
    check_session_reusage(ssl_fixture, &peer);
    check_alpn(ssl_fixture, &peer);
    if (ssl_fixture->server_name_indication != nullptr) {
      check_server1_peer(&peer);
    } else {
      check_server0_peer(&peer);
    }
  } else {
    GPR_ASSERT(ssl_fixture->base.client_result == nullptr);
  }
  if (expect_success) {
    GPR_ASSERT(tsi_handshaker_result_extract_peer(
                   ssl_fixture->base.server_result, &peer) == TSI_OK);
    check_session_reusage(ssl_fixture, &peer);
    check_alpn(ssl_fixture, &peer);
    check_client_peer(ssl_fixture, &peer);
  } else {
    GPR_ASSERT(ssl_fixture->base.server_result == nullptr);
  }
}

static void ssl_test_pem_key_cert_pair_destroy(tsi_ssl_pem_key_cert_pair kp) {
  gpr_free((void*)kp.private_key);
  gpr_free((void*)kp.cert_chain);
}

static void ssl_test_destruct(tsi_test_fixture* fixture) {
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  if (ssl_fixture == nullptr) {
    return;
  }
  /* Destroy ssl_alpn_lib. */
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
  /* Destroy ssl_key_cert_lib. */
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
  ssl_test_pem_key_cert_pair_destroy(key_cert_lib->client_pem_key_cert_pair);
  ssl_test_pem_key_cert_pair_destroy(
      key_cert_lib->bad_client_pem_key_cert_pair);
  gpr_free(key_cert_lib->root_cert);
  tsi_ssl_root_certs_store_destroy(key_cert_lib->root_store);
  gpr_free(key_cert_lib);
  if (ssl_fixture->session_cache != nullptr) {
    tsi_ssl_session_cache_unref(ssl_fixture->session_cache);
  }
  /* Unreference others. */
  tsi_ssl_server_handshaker_factory_unref(
      ssl_fixture->server_handshaker_factory);
  tsi_ssl_client_handshaker_factory_unref(
      ssl_fixture->client_handshaker_factory);
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
  GPR_ASSERT(grpc_load_file(file_path, 1, &slice) == GRPC_ERROR_NONE);
  char* data = grpc_slice_to_c_string(slice);
  grpc_slice_unref(slice);
  gpr_free(file_path);
  return data;
}

static tsi_test_fixture* ssl_tsi_test_fixture_create() {
  ssl_tsi_test_fixture* ssl_fixture =
      static_cast<ssl_tsi_test_fixture*>(gpr_zalloc(sizeof(*ssl_fixture)));
  tsi_test_fixture_init(&ssl_fixture->base);
  ssl_fixture->base.test_unused_bytes = false;
  ssl_fixture->base.vtable = &vtable;
  /* Create ssl_key_cert_lib. */
  ssl_key_cert_lib* key_cert_lib =
      static_cast<ssl_key_cert_lib*>(gpr_zalloc(sizeof(*key_cert_lib)));
  key_cert_lib->use_bad_server_cert = false;
  key_cert_lib->use_bad_client_cert = false;
  key_cert_lib->use_root_store = false;
  key_cert_lib->server_num_key_cert_pairs =
      SSL_TSI_TEST_SERVER_KEY_CERT_PAIRS_NUM;
  key_cert_lib->bad_server_num_key_cert_pairs =
      SSL_TSI_TEST_BAD_SERVER_KEY_CERT_PAIRS_NUM;
  key_cert_lib->server_pem_key_cert_pairs =
      static_cast<tsi_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(tsi_ssl_pem_key_cert_pair) *
                     key_cert_lib->server_num_key_cert_pairs));
  key_cert_lib->bad_server_pem_key_cert_pairs =
      static_cast<tsi_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(tsi_ssl_pem_key_cert_pair) *
                     key_cert_lib->bad_server_num_key_cert_pairs));
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
  key_cert_lib->root_cert = load_file(SSL_TSI_TEST_CREDENTIALS_DIR, "ca.pem");
  key_cert_lib->root_store =
      tsi_ssl_root_certs_store_create(key_cert_lib->root_cert);
  GPR_ASSERT(key_cert_lib->root_store != nullptr);
  ssl_fixture->key_cert_lib = key_cert_lib;
  /* Create ssl_alpn_lib. */
  ssl_alpn_lib* alpn_lib =
      static_cast<ssl_alpn_lib*>(gpr_zalloc(sizeof(*alpn_lib)));
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
  return &ssl_fixture->base;
}

void ssl_tsi_test_do_handshake_tiny_handshake_buffer() {
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  fixture->handshake_buffer_size = TSI_TEST_TINY_HANDSHAKE_BUFFER_SIZE;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_small_handshake_buffer() {
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  fixture->handshake_buffer_size = TSI_TEST_SMALL_HANDSHAKE_BUFFER_SIZE;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake() {
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_root_store() {
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->key_cert_lib->use_root_store = true;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_client_authentication() {
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->force_client_auth = true;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_client_authentication_and_root_store() {
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->force_client_auth = true;
  ssl_fixture->key_cert_lib->use_root_store = true;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_server_name_indication_exact_domain() {
  /* server1 cert contains "waterzooi.test.google.be" in SAN. */
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->server_name_indication =
      const_cast<char*>("waterzooi.test.google.be");
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_server_name_indication_wild_star_domain() {
  /* server1 cert contains "*.test.google.fr" in SAN. */
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->server_name_indication =
      const_cast<char*>("juju.test.google.fr");
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_bad_server_cert() {
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->key_cert_lib->use_bad_server_cert = true;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_bad_client_cert() {
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->key_cert_lib->use_bad_client_cert = true;
  ssl_fixture->force_client_auth = true;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_alpn_client_no_server() {
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->alpn_lib->alpn_mode = ALPN_CLIENT_NO_SERVER;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_alpn_server_no_client() {
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->alpn_lib->alpn_mode = ALPN_SERVER_NO_CLIENT;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_alpn_client_server_mismatch() {
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->alpn_lib->alpn_mode = ALPN_CLIENT_SERVER_MISMATCH;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_alpn_client_server_ok() {
  tsi_test_fixture* fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->alpn_lib->alpn_mode = ALPN_CLIENT_SERVER_OK;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_round_trip_for_all_configs() {
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

void ssl_tsi_test_do_round_trip_odd_buffer_size() {
  const size_t odd_sizes[] = {1025, 2051, 4103, 8207, 16409};
  const size_t size = sizeof(odd_sizes) / sizeof(size_t);
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

static const tsi_ssl_handshaker_factory_vtable* original_vtable;
static bool handshaker_factory_destructor_called;

static void ssl_tsi_test_handshaker_factory_destructor(
    tsi_ssl_handshaker_factory* factory) {
  GPR_ASSERT(factory != nullptr);
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
  memset(&options, 0, sizeof(options));
  options.pem_root_certs = cert_chain;
  tsi_ssl_client_handshaker_factory* client_handshaker_factory;
  GPR_ASSERT(tsi_create_ssl_client_handshaker_factory_with_options(
                 &options, &client_handshaker_factory) == TSI_OK);

  handshaker_factory_destructor_called = false;
  original_vtable = tsi_ssl_handshaker_factory_swap_vtable(
      reinterpret_cast<tsi_ssl_handshaker_factory*>(client_handshaker_factory),
      &test_handshaker_factory_vtable);

  tsi_handshaker* handshaker[3];

  for (i = 0; i < 3; ++i) {
    GPR_ASSERT(tsi_ssl_client_handshaker_factory_create_handshaker(
                   client_handshaker_factory, "google.com", &handshaker[i]) ==
               TSI_OK);
  }

  tsi_handshaker_destroy(handshaker[1]);
  GPR_ASSERT(!handshaker_factory_destructor_called);

  tsi_handshaker_destroy(handshaker[0]);
  GPR_ASSERT(!handshaker_factory_destructor_called);

  tsi_ssl_client_handshaker_factory_unref(client_handshaker_factory);
  GPR_ASSERT(!handshaker_factory_destructor_called);

  tsi_handshaker_destroy(handshaker[2]);
  GPR_ASSERT(handshaker_factory_destructor_called);

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

  GPR_ASSERT(tsi_create_ssl_server_handshaker_factory(
                 &cert_pair, 1, cert_chain, 0, nullptr, nullptr, 0,
                 &server_handshaker_factory) == TSI_OK);

  handshaker_factory_destructor_called = false;
  original_vtable = tsi_ssl_handshaker_factory_swap_vtable(
      reinterpret_cast<tsi_ssl_handshaker_factory*>(server_handshaker_factory),
      &test_handshaker_factory_vtable);

  for (i = 0; i < 3; ++i) {
    GPR_ASSERT(tsi_ssl_server_handshaker_factory_create_handshaker(
                   server_handshaker_factory, &handshaker[i]) == TSI_OK);
  }

  tsi_handshaker_destroy(handshaker[1]);
  GPR_ASSERT(!handshaker_factory_destructor_called);

  tsi_handshaker_destroy(handshaker[0]);
  GPR_ASSERT(!handshaker_factory_destructor_called);

  tsi_ssl_server_handshaker_factory_unref(server_handshaker_factory);
  GPR_ASSERT(!handshaker_factory_destructor_called);

  tsi_handshaker_destroy(handshaker[2]);
  GPR_ASSERT(handshaker_factory_destructor_called);

  ssl_test_pem_key_cert_pair_destroy(cert_pair);
}

/* Attempting to create a handshaker factory with invalid parameters should fail
 * but not crash. */
void test_tsi_ssl_client_handshaker_factory_bad_params() {
  const char* cert_chain = "This is not a valid PEM file.";

  tsi_ssl_client_handshaker_factory* client_handshaker_factory;
  tsi_ssl_client_handshaker_options options;
  memset(&options, 0, sizeof(options));
  options.pem_root_certs = cert_chain;
  GPR_ASSERT(tsi_create_ssl_client_handshaker_factory_with_options(
                 &options, &client_handshaker_factory) == TSI_INVALID_ARGUMENT);
  tsi_ssl_client_handshaker_factory_unref(client_handshaker_factory);
}

void ssl_tsi_test_handshaker_factory_internals() {
  test_tsi_ssl_client_handshaker_factory_refcounting();
  test_tsi_ssl_server_handshaker_factory_refcounting();
  test_tsi_ssl_client_handshaker_factory_bad_params();
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();

  ssl_tsi_test_do_handshake_tiny_handshake_buffer();
  ssl_tsi_test_do_handshake_small_handshake_buffer();
  ssl_tsi_test_do_handshake();
  ssl_tsi_test_do_handshake_with_root_store();
  ssl_tsi_test_do_handshake_with_client_authentication();
  ssl_tsi_test_do_handshake_with_client_authentication_and_root_store();
  ssl_tsi_test_do_handshake_with_server_name_indication_exact_domain();
  ssl_tsi_test_do_handshake_with_server_name_indication_wild_star_domain();
  ssl_tsi_test_do_handshake_with_bad_server_cert();
  ssl_tsi_test_do_handshake_with_bad_client_cert();
#ifdef OPENSSL_IS_BORINGSSL
  // BoringSSL and OpenSSL have different behaviors on mismatched ALPN.
  ssl_tsi_test_do_handshake_alpn_client_no_server();
  ssl_tsi_test_do_handshake_alpn_client_server_mismatch();
#endif
  ssl_tsi_test_do_handshake_alpn_server_no_client();
  ssl_tsi_test_do_handshake_alpn_client_server_ok();
  ssl_tsi_test_do_handshake_session_cache();
  ssl_tsi_test_do_round_trip_for_all_configs();
  ssl_tsi_test_do_round_trip_odd_buffer_size();
  ssl_tsi_test_handshaker_factory_internals();
  grpc_shutdown();
  return 0;
}
