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
#include "src/core/lib/security/transport/security_connector.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security_adapter.h"
#include "test/core/tsi/transport_security_test_lib.h"
#include "test/core/util/test_config.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#define SSL_TSI_TEST_ALPN1 "foo"
#define SSL_TSI_TEST_ALPN2 "toto"
#define SSL_TSI_TEST_ALPN3 "baz"
#define SSL_TSI_TEST_ALPN_NUM 2
#define SSL_TSI_TEST_SERVER_KEY_CERT_PAIRS_NUM 2
#define SSL_TSI_TEST_BAD_SERVER_KEY_CERT_PAIRS_NUM 1
#define SSL_TSI_TEST_CREDENTIALS_DIR "src/core/tsi/test_creds/"

typedef enum AlpnMode {
  NO_ALPN,
  ALPN_CLIENT_NO_SERVER,
  ALPN_SERVER_NO_CLIENT,
  ALPN_CLIENT_SERVER_OK,
  ALPN_CLIENT_SERVER_MISMATCH
} AlpnMode;

typedef struct ssl_alpn_lib {
  AlpnMode alpn_mode;
  char **server_alpn_protocols;
  char **client_alpn_protocols;
  uint16_t num_server_alpn_protocols;
  uint16_t num_client_alpn_protocols;
} ssl_alpn_lib;

typedef struct ssl_key_cert_lib {
  bool use_bad_server_cert;
  bool use_bad_client_cert;
  char *root_cert;
  tsi_ssl_pem_key_cert_pair *server_pem_key_cert_pairs;
  tsi_ssl_pem_key_cert_pair *bad_server_pem_key_cert_pairs;
  tsi_ssl_pem_key_cert_pair client_pem_key_cert_pair;
  tsi_ssl_pem_key_cert_pair bad_client_pem_key_cert_pair;
  uint16_t server_num_key_cert_pairs;
  uint16_t bad_server_num_key_cert_pairs;
} ssl_key_cert_lib;

typedef struct ssl_tsi_test_fixture {
  tsi_test_fixture base;
  ssl_key_cert_lib *key_cert_lib;
  ssl_alpn_lib *alpn_lib;
  bool force_client_auth;
  char *server_name_indication;
  tsi_ssl_server_handshaker_factory *server_handshaker_factory;
  tsi_ssl_client_handshaker_factory *client_handshaker_factory;
} ssl_tsi_test_fixture;

static void ssl_test_setup_handshakers(tsi_test_fixture *fixture) {
  ssl_tsi_test_fixture *ssl_fixture = (ssl_tsi_test_fixture *)fixture;
  GPR_ASSERT(ssl_fixture != NULL);
  GPR_ASSERT(ssl_fixture->key_cert_lib != NULL);
  GPR_ASSERT(ssl_fixture->alpn_lib != NULL);
  ssl_key_cert_lib *key_cert_lib = ssl_fixture->key_cert_lib;
  ssl_alpn_lib *alpn_lib = ssl_fixture->alpn_lib;

  /* Create client handshaker factory. */
  tsi_ssl_pem_key_cert_pair *client_key_cert_pair = NULL;
  if (ssl_fixture->force_client_auth) {
    client_key_cert_pair = key_cert_lib->use_bad_client_cert
                               ? &key_cert_lib->bad_client_pem_key_cert_pair
                               : &key_cert_lib->client_pem_key_cert_pair;
  }
  char **client_alpn_protocols = NULL;
  uint16_t num_client_alpn_protocols = 0;
  if (alpn_lib->alpn_mode == ALPN_CLIENT_NO_SERVER ||
      alpn_lib->alpn_mode == ALPN_CLIENT_SERVER_OK ||
      alpn_lib->alpn_mode == ALPN_CLIENT_SERVER_MISMATCH) {
    client_alpn_protocols = alpn_lib->client_alpn_protocols;
    num_client_alpn_protocols = alpn_lib->num_client_alpn_protocols;
  }
  GPR_ASSERT(tsi_create_ssl_client_handshaker_factory(
                 client_key_cert_pair, key_cert_lib->root_cert, NULL,
                 (const char **)client_alpn_protocols,
                 num_client_alpn_protocols,
                 &ssl_fixture->client_handshaker_factory) == TSI_OK);

  /* Create server handshaker factory. */
  char **server_alpn_protocols = NULL;
  uint16_t num_server_alpn_protocols = 0;
  if (alpn_lib->alpn_mode == ALPN_SERVER_NO_CLIENT ||
      alpn_lib->alpn_mode == ALPN_CLIENT_SERVER_OK ||
      alpn_lib->alpn_mode == ALPN_CLIENT_SERVER_MISMATCH) {
    server_alpn_protocols = alpn_lib->server_alpn_protocols;
    num_server_alpn_protocols = alpn_lib->num_server_alpn_protocols;
    if (alpn_lib->alpn_mode == ALPN_CLIENT_SERVER_MISMATCH) {
      num_server_alpn_protocols--;
    }
  }

  GPR_ASSERT(tsi_create_ssl_server_handshaker_factory(
                 key_cert_lib->use_bad_server_cert
                     ? key_cert_lib->bad_server_pem_key_cert_pairs
                     : key_cert_lib->server_pem_key_cert_pairs,
                 key_cert_lib->use_bad_server_cert
                     ? key_cert_lib->bad_server_num_key_cert_pairs
                     : key_cert_lib->server_num_key_cert_pairs,
                 key_cert_lib->root_cert, ssl_fixture->force_client_auth, NULL,
                 (const char **)server_alpn_protocols,
                 num_server_alpn_protocols,
                 &ssl_fixture->server_handshaker_factory) == TSI_OK);

  /* Create server and client handshakers. */
  tsi_handshaker *client_handshaker = NULL;
  GPR_ASSERT(tsi_ssl_client_handshaker_factory_create_handshaker(
                 ssl_fixture->client_handshaker_factory,
                 ssl_fixture->server_name_indication,
                 &client_handshaker) == TSI_OK);
  ssl_fixture->base.client_handshaker =
      tsi_create_adapter_handshaker(client_handshaker);

  tsi_handshaker *server_handshaker = NULL;
  GPR_ASSERT(tsi_ssl_server_handshaker_factory_create_handshaker(
                 ssl_fixture->server_handshaker_factory, &server_handshaker) ==
             TSI_OK);
  ssl_fixture->base.server_handshaker =
      tsi_create_adapter_handshaker(server_handshaker);
}

static void check_alpn(ssl_tsi_test_fixture *ssl_fixture,
                       const tsi_peer *peer) {
  GPR_ASSERT(ssl_fixture != NULL);
  GPR_ASSERT(ssl_fixture->alpn_lib != NULL);
  ssl_alpn_lib *alpn_lib = ssl_fixture->alpn_lib;
  const tsi_peer_property *alpn_property =
      tsi_peer_get_property_by_name(peer, TSI_SSL_ALPN_SELECTED_PROTOCOL);
  if (alpn_lib->alpn_mode != ALPN_CLIENT_SERVER_OK) {
    GPR_ASSERT(alpn_property == NULL);
  } else {
    GPR_ASSERT(alpn_property != NULL);
    const char *expected_match = "baz";
    GPR_ASSERT(memcmp(alpn_property->value.data, expected_match,
                      alpn_property->value.length) == 0);
  }
}

static const tsi_peer_property *
check_basic_authenticated_peer_and_get_common_name(const tsi_peer *peer) {
  const tsi_peer_property *cert_type_property =
      tsi_peer_get_property_by_name(peer, TSI_CERTIFICATE_TYPE_PEER_PROPERTY);
  GPR_ASSERT(cert_type_property != NULL);
  GPR_ASSERT(memcmp(cert_type_property->value.data, TSI_X509_CERTIFICATE_TYPE,
                    cert_type_property->value.length) == 0);
  const tsi_peer_property *property = tsi_peer_get_property_by_name(
      peer, TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY);
  GPR_ASSERT(property != NULL);
  return property;
}

void check_server0_peer(tsi_peer *peer) {
  const tsi_peer_property *property =
      check_basic_authenticated_peer_and_get_common_name(peer);
  const char *expected_match = "*.test.google.com.au";
  GPR_ASSERT(memcmp(property->value.data, expected_match,
                    property->value.length) == 0);
  GPR_ASSERT(tsi_peer_get_property_by_name(
                 peer, TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY) ==
             NULL);
  GPR_ASSERT(tsi_ssl_peer_matches_name(peer, "foo.test.google.com.au") == 1);
  GPR_ASSERT(tsi_ssl_peer_matches_name(peer, "bar.test.google.com.au") == 1);
  GPR_ASSERT(tsi_ssl_peer_matches_name(peer, "bar.test.google.blah") == 0);
  GPR_ASSERT(tsi_ssl_peer_matches_name(peer, "foo.bar.test.google.com.au") ==
             0);
  GPR_ASSERT(tsi_ssl_peer_matches_name(peer, "test.google.com.au") == 0);
  tsi_peer_destruct(peer);
}

static bool check_subject_alt_name(tsi_peer *peer, const char *name) {
  size_t i;
  for (i = 0; i < peer->property_count; i++) {
    const tsi_peer_property *prop = &peer->properties[i];
    if (strcmp(prop->name, TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY) ==
        0) {
      if (memcmp(prop->value.data, name, prop->value.length) == 0) {
        return true;
      }
    }
  }
  return false;
}

void check_server1_peer(tsi_peer *peer) {
  const tsi_peer_property *property =
      check_basic_authenticated_peer_and_get_common_name(peer);
  const char *expected_match = "*.test.google.com";
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

static void check_client_peer(ssl_tsi_test_fixture *ssl_fixture,
                              tsi_peer *peer) {
  GPR_ASSERT(ssl_fixture != NULL);
  GPR_ASSERT(ssl_fixture->alpn_lib != NULL);
  ssl_alpn_lib *alpn_lib = ssl_fixture->alpn_lib;
  if (!ssl_fixture->force_client_auth) {
    GPR_ASSERT(peer->property_count ==
               (alpn_lib->alpn_mode == ALPN_CLIENT_SERVER_OK ? 1 : 0));
  } else {
    const tsi_peer_property *property =
        check_basic_authenticated_peer_and_get_common_name(peer);
    const char *expected_match = "testclient";
    GPR_ASSERT(memcmp(property->value.data, expected_match,
                      property->value.length) == 0);
  }
  tsi_peer_destruct(peer);
}

static void ssl_test_check_handshake_results(tsi_test_fixture *fixture) {
  ssl_tsi_test_fixture *ssl_fixture = (ssl_tsi_test_fixture *)fixture;
  GPR_ASSERT(ssl_fixture != NULL);
  GPR_ASSERT(ssl_fixture->key_cert_lib != NULL);
  ssl_key_cert_lib *key_cert_lib = ssl_fixture->key_cert_lib;
  tsi_peer peer;
  bool expect_success =
      !(key_cert_lib->use_bad_server_cert ||
        (key_cert_lib->use_bad_client_cert && ssl_fixture->force_client_auth));

  if (expect_success) {
    GPR_ASSERT(tsi_handshaker_result_extract_peer(
                   ssl_fixture->base.client_result, &peer) == TSI_OK);
    check_alpn(ssl_fixture, &peer);

    if (ssl_fixture->server_name_indication != NULL) {
      check_server1_peer(&peer);
    } else {
      check_server0_peer(&peer);
    }
  } else {
    GPR_ASSERT(ssl_fixture->base.client_result == NULL);
  }

  if (expect_success) {
    GPR_ASSERT(tsi_handshaker_result_extract_peer(
                   ssl_fixture->base.server_result, &peer) == TSI_OK);
    check_alpn(ssl_fixture, &peer);
    check_client_peer(ssl_fixture, &peer);
  } else {
    GPR_ASSERT(ssl_fixture->base.server_result == NULL);
  }
}

static void ssl_test_pem_key_cert_pair_destroy(tsi_ssl_pem_key_cert_pair kp) {
  gpr_free((void *)kp.private_key);
  gpr_free((void *)kp.cert_chain);
}

static void ssl_test_destruct(tsi_test_fixture *fixture) {
  ssl_tsi_test_fixture *ssl_fixture = (ssl_tsi_test_fixture *)fixture;
  if (ssl_fixture == NULL) {
    return;
  }
  /* Destroy base. */
  tsi_test_frame_protector_config_destroy(ssl_fixture->base.config);
  tsi_handshaker_destroy(ssl_fixture->base.client_handshaker);
  tsi_handshaker_destroy(ssl_fixture->base.server_handshaker);
  tsi_handshaker_result_destroy(ssl_fixture->base.client_result);
  tsi_handshaker_result_destroy(ssl_fixture->base.server_result);
  gpr_free(ssl_fixture->base.client_channel);
  gpr_free(ssl_fixture->base.server_channel);

  /* Destory ssl_alpn_lib. */
  ssl_alpn_lib *alpn_lib = ssl_fixture->alpn_lib;
  size_t i;
  for (i = 0; i < alpn_lib->num_server_alpn_protocols; i++) {
    gpr_free(alpn_lib->server_alpn_protocols[i]);
  }
  gpr_free(alpn_lib->server_alpn_protocols);
  for (i = 0; i < alpn_lib->num_client_alpn_protocols; i++) {
    gpr_free(alpn_lib->client_alpn_protocols[i]);
  }
  gpr_free(alpn_lib->client_alpn_protocols);
  gpr_free(alpn_lib);

  /* Destroy ssl_key_cert_lib. */
  ssl_key_cert_lib *key_cert_lib = ssl_fixture->key_cert_lib;
  for (i = 0; i < key_cert_lib->server_num_key_cert_pairs; i++) {
    ssl_test_pem_key_cert_pair_destroy(
        key_cert_lib->server_pem_key_cert_pairs[i]);
  }
  gpr_free(key_cert_lib->server_pem_key_cert_pairs);
  for (i = 0; i < key_cert_lib->bad_server_num_key_cert_pairs; i++) {
    ssl_test_pem_key_cert_pair_destroy(
        key_cert_lib->bad_server_pem_key_cert_pairs[i]);
  }
  gpr_free(key_cert_lib->bad_server_pem_key_cert_pairs);
  ssl_test_pem_key_cert_pair_destroy(key_cert_lib->client_pem_key_cert_pair);
  ssl_test_pem_key_cert_pair_destroy(
      key_cert_lib->bad_client_pem_key_cert_pair);
  gpr_free(key_cert_lib->root_cert);
  gpr_free(key_cert_lib);

  /* Destroy others. */
  tsi_ssl_server_handshaker_factory_destroy(
      ssl_fixture->server_handshaker_factory);
  tsi_ssl_client_handshaker_factory_destroy(
      ssl_fixture->client_handshaker_factory);
}

static const struct tsi_test_fixture_vtable vtable = {
    ssl_test_setup_handshakers, ssl_test_check_handshake_results,
    ssl_test_destruct};

static void malloc_and_copy(const char *src, char **dst) {
  *dst = gpr_zalloc(strlen(src) + 1);
  memcpy(*dst, src, strlen(src) + 1);
}

static char *load_file(const char *dir_path, const char *file_name) {
  char *file_path =
      gpr_zalloc(sizeof(char) * (strlen(dir_path) + strlen(file_name) + 1));
  memcpy(file_path, dir_path, strlen(dir_path));
  memcpy(file_path + strlen(dir_path), file_name, strlen(file_name));
  grpc_slice slice;
  GPR_ASSERT(grpc_load_file(file_path, 1, &slice) == GRPC_ERROR_NONE);
  char *data = grpc_slice_to_c_string(slice);
  grpc_slice_unref(slice);
  gpr_free(file_path);
  return data;
}

static tsi_test_fixture *ssl_tsi_test_fixture_create() {
  ssl_tsi_test_fixture *ssl_fixture = gpr_zalloc(sizeof(*ssl_fixture));

  /* Create tsi_test_frame_protector_config. */
  tsi_test_frame_protector_config *config =
      tsi_test_frame_protector_config_create(true, true, true, true, true, true,
                                             true, true);
  ssl_fixture->base.config = config;

  /* Create ssl_key_cert_lib. */
  ssl_key_cert_lib *key_cert_lib = gpr_zalloc(sizeof(*key_cert_lib));
  key_cert_lib->use_bad_server_cert = false;
  key_cert_lib->use_bad_client_cert = false;
  key_cert_lib->server_num_key_cert_pairs =
      SSL_TSI_TEST_SERVER_KEY_CERT_PAIRS_NUM;
  key_cert_lib->bad_server_num_key_cert_pairs =
      SSL_TSI_TEST_BAD_SERVER_KEY_CERT_PAIRS_NUM;
  key_cert_lib->server_pem_key_cert_pairs =
      gpr_malloc(sizeof(tsi_ssl_pem_key_cert_pair) *
                 key_cert_lib->server_num_key_cert_pairs);
  key_cert_lib->bad_server_pem_key_cert_pairs =
      gpr_malloc(sizeof(tsi_ssl_pem_key_cert_pair) *
                 key_cert_lib->bad_server_num_key_cert_pairs);
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
  ssl_fixture->key_cert_lib = key_cert_lib;

  /* Create ssl_alpn_lib. */
  ssl_alpn_lib *alpn_lib = gpr_zalloc(sizeof(*alpn_lib));
  alpn_lib->server_alpn_protocols =
      gpr_zalloc(sizeof(char *) * SSL_TSI_TEST_ALPN_NUM);
  alpn_lib->client_alpn_protocols =
      gpr_zalloc(sizeof(char *) * SSL_TSI_TEST_ALPN_NUM);
  malloc_and_copy(SSL_TSI_TEST_ALPN1, &alpn_lib->server_alpn_protocols[0]);
  malloc_and_copy(SSL_TSI_TEST_ALPN3, &alpn_lib->server_alpn_protocols[1]);
  malloc_and_copy(SSL_TSI_TEST_ALPN2, &alpn_lib->client_alpn_protocols[0]);
  malloc_and_copy(SSL_TSI_TEST_ALPN3, &alpn_lib->client_alpn_protocols[1]);
  alpn_lib->num_server_alpn_protocols = SSL_TSI_TEST_ALPN_NUM;
  alpn_lib->num_client_alpn_protocols = SSL_TSI_TEST_ALPN_NUM;
  alpn_lib->alpn_mode = NO_ALPN;
  ssl_fixture->alpn_lib = alpn_lib;
  ssl_fixture->base.vtable = &vtable;
  ssl_fixture->base.handshake_buffer_size = TSI_TEST_DEFAULT_BUFFER_SIZE;
  ssl_fixture->base.client_channel = gpr_zalloc(TSI_TEST_DEFAULT_CHANNEL_SIZE);
  ssl_fixture->base.server_channel = gpr_zalloc(TSI_TEST_DEFAULT_CHANNEL_SIZE);
  ssl_fixture->base.bytes_written_to_client_channel = 0;
  ssl_fixture->base.bytes_written_to_server_channel = 0;
  ssl_fixture->base.bytes_read_from_client_channel = 0;
  ssl_fixture->base.bytes_read_from_server_channel = 0;
  ssl_fixture->server_name_indication = NULL;
  ssl_fixture->force_client_auth = false;
  return &ssl_fixture->base;
}

void ssl_tsi_test_do_handshake_tiny_handshake_buffer() {
  tsi_test_fixture *fixture = ssl_tsi_test_fixture_create();
  fixture->handshake_buffer_size = TSI_TEST_TINY_HANDSHAKE_BUFFER_SIZE;
  tsi_test_do_handshake(fixture);
  tsi_test_destroy(fixture);
}

void ssl_tsi_test_do_handshake_small_handshake_buffer() {
  tsi_test_fixture *fixture = ssl_tsi_test_fixture_create();
  fixture->handshake_buffer_size = TSI_TEST_SMALL_HANDSHAKE_BUFFER_SIZE;
  tsi_test_do_handshake(fixture);
  tsi_test_destroy(fixture);
}

void ssl_tsi_test_do_handshake() {
  tsi_test_fixture *fixture = ssl_tsi_test_fixture_create();
  tsi_test_do_handshake(fixture);
  tsi_test_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_client_authentication() {
  tsi_test_fixture *fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture *ssl_fixture = (ssl_tsi_test_fixture *)fixture;
  ssl_fixture->force_client_auth = true;
  tsi_test_do_handshake(fixture);
  tsi_test_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_server_name_indication_exact_domain() {
  /* server1 cert contains "waterzooi.test.google.be" in SAN. */
  tsi_test_fixture *fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture *ssl_fixture = (ssl_tsi_test_fixture *)fixture;
  ssl_fixture->server_name_indication = "waterzooi.test.google.be";
  tsi_test_do_handshake(fixture);
  tsi_test_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_server_name_indication_wild_star_domain() {
  /* server1 cert contains "*.test.google.fr" in SAN. */
  tsi_test_fixture *fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture *ssl_fixture = (ssl_tsi_test_fixture *)fixture;
  ssl_fixture->server_name_indication = "juju.test.google.fr";
  tsi_test_do_handshake(fixture);
  tsi_test_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_bad_server_cert() {
  tsi_test_fixture *fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture *ssl_fixture = (ssl_tsi_test_fixture *)fixture;
  ssl_fixture->key_cert_lib->use_bad_server_cert = true;
  tsi_test_do_handshake(fixture);
  tsi_test_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_bad_client_cert() {
  tsi_test_fixture *fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture *ssl_fixture = (ssl_tsi_test_fixture *)fixture;
  ssl_fixture->key_cert_lib->use_bad_client_cert = true;
  ssl_fixture->force_client_auth = true;
  tsi_test_do_handshake(fixture);
  tsi_test_destroy(fixture);
}

void ssl_tsi_test_do_handshake_alpn_client_no_server() {
  tsi_test_fixture *fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture *ssl_fixture = (ssl_tsi_test_fixture *)fixture;
  ssl_fixture->alpn_lib->alpn_mode = ALPN_CLIENT_NO_SERVER;
  tsi_test_do_handshake(fixture);
  tsi_test_destroy(fixture);
}

void ssl_tsi_test_do_handshake_alpn_server_no_client() {
  tsi_test_fixture *fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture *ssl_fixture = (ssl_tsi_test_fixture *)fixture;
  ssl_fixture->alpn_lib->alpn_mode = ALPN_SERVER_NO_CLIENT;
  tsi_test_do_handshake(fixture);
  tsi_test_destroy(fixture);
}

void ssl_tsi_test_do_handshake_alpn_client_server_mismatch() {
  tsi_test_fixture *fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture *ssl_fixture = (ssl_tsi_test_fixture *)fixture;
  ssl_fixture->alpn_lib->alpn_mode = ALPN_CLIENT_SERVER_MISMATCH;
  tsi_test_do_handshake(fixture);
  tsi_test_destroy(fixture);
}

void ssl_tsi_test_do_handshake_alpn_client_server_ok() {
  tsi_test_fixture *fixture = ssl_tsi_test_fixture_create();
  ssl_tsi_test_fixture *ssl_fixture = (ssl_tsi_test_fixture *)fixture;
  ssl_fixture->alpn_lib->alpn_mode = ALPN_CLIENT_SERVER_OK;
  tsi_test_do_handshake(fixture);
  tsi_test_destroy(fixture);
}

void ssl_tsi_test_do_round_trip_for_all_configs() {
  unsigned int *bit_array =
      gpr_zalloc(sizeof(unsigned int) * TSI_TEST_NUM_OF_ARGUMENTS);
  unsigned int mask = 1U << (TSI_TEST_NUM_OF_ARGUMENTS - 1);
  unsigned int val = 0, ind = 0;
  for (val = 0; val < TSI_TEST_NUM_OF_COMBINATIONS; val++) {
    unsigned int v = val;
    for (ind = 0; ind < TSI_TEST_NUM_OF_ARGUMENTS; ind++) {
      bit_array[ind] = (v & mask) ? 1 : 0;
      v <<= 1;
    }
    tsi_test_fixture *fixture = ssl_tsi_test_fixture_create();
    ssl_tsi_test_fixture *ssl_fixture = (ssl_tsi_test_fixture *)fixture;
    tsi_test_frame_protector_config_destroy(ssl_fixture->base.config);
    ssl_fixture->base.config = tsi_test_frame_protector_config_create(
        bit_array[0], bit_array[1], bit_array[2], bit_array[3], bit_array[4],
        bit_array[5], bit_array[6], bit_array[7]);
    tsi_test_do_round_trip(&ssl_fixture->base);
    tsi_test_destroy(fixture);
  }
  gpr_free(bit_array);
}

void ssl_tsi_test_do_round_trip_odd_buffer_size() {
  size_t odd_sizes[] = {1025, 2051, 4103, 8207, 16409};
  size_t size = sizeof(odd_sizes) / sizeof(size_t);
  size_t ind1 = 0, ind2 = 0, ind3 = 0, ind4 = 0, ind5 = 0;
  for (ind1 = 0; ind1 < size; ind1++) {
    for (ind2 = 0; ind2 < size; ind2++) {
      for (ind3 = 0; ind3 < size; ind3++) {
        for (ind4 = 0; ind4 < size; ind4++) {
          for (ind5 = 0; ind5 < size; ind5++) {
            tsi_test_fixture *fixture = ssl_tsi_test_fixture_create();
            ssl_tsi_test_fixture *ssl_fixture = (ssl_tsi_test_fixture *)fixture;
            tsi_test_frame_protector_config_set_buffer_size(
                ssl_fixture->base.config, odd_sizes[ind1], odd_sizes[ind2],
                odd_sizes[ind3], odd_sizes[ind4], odd_sizes[ind5]);
            tsi_test_do_round_trip(&ssl_fixture->base);
            tsi_test_destroy(fixture);
          }
        }
      }
    }
  }
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  ssl_tsi_test_do_handshake_tiny_handshake_buffer();
  ssl_tsi_test_do_handshake_small_handshake_buffer();
  ssl_tsi_test_do_handshake();
  ssl_tsi_test_do_handshake_with_client_authentication();
  ssl_tsi_test_do_handshake_with_server_name_indication_exact_domain();
  ssl_tsi_test_do_handshake_with_server_name_indication_wild_star_domain();
  ssl_tsi_test_do_handshake_with_bad_server_cert();
  ssl_tsi_test_do_handshake_with_bad_client_cert();
  ssl_tsi_test_do_handshake_alpn_client_no_server();
  ssl_tsi_test_do_handshake_alpn_server_no_client();
  ssl_tsi_test_do_handshake_alpn_client_server_mismatch();
  ssl_tsi_test_do_handshake_alpn_client_server_ok();
  ssl_tsi_test_do_round_trip_for_all_configs();
  ssl_tsi_test_do_round_trip_odd_buffer_size();
  return 0;
}
