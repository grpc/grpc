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

#ifndef GRPC_TEST_CORE_TSI_SSL_TRANSPORT_SECURITY_TEST_COMMON_H_
#define GRPC_TEST_CORE_TSI_SSL_TRANSPORT_SECURITY_TEST_COMMON_H_

#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/tsi/ssl/ssl_transport_security.h"
#include "src/core/tsi/transport_security.h"
#include "src/core/tsi/transport_security_interface.h"
#include "test/core/tsi/transport_security_test_lib.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

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

typedef struct tls_cred_reload_lib tls_cred_reload_lib;

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
  tls_cred_reload_lib* cred_reload_lib;
} ssl_tsi_test_fixture;

// Validate peer information obtained after SSL/TLS handshakes.
void ssl_tsi_test_check_handshaker_peers(tsi_test_fixture* fixture,
                                         bool expect_success);

// Populate various fields of an ssl_tsi_test_fixture instance.
void ssl_tsi_test_fixture_init(tsi_test_fixture* fixture);

// Clean up various fields of an ssl_tsi_test_fixture instance.
void ssl_tsi_test_fixture_cleanup(tsi_test_fixture* fixture);

// Return contents of a file at the specified path.
char* ssl_tsi_test_load_file(const char* dir_path, const char* file_name);

#endif  // GRPC_TEST_CORE_TSI_SSL_TRANSPORT_SECURITY_TEST_COMMON_H_
