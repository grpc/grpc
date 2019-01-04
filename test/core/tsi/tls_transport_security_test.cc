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

#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"
#include "src/core/tsi/ssl/ssl_transport_security.h"
#include "test/core/tsi/ssl_transport_security_test_common.h"
#include "test/core/util/test_config.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

extern "C" {
#include <openssl/crypto.h>
}

using grpc_core::internal::
    tls_tsi_handshaker_get_credential_reload_arg_for_testing;
using grpc_core::internal::tls_tsi_handshaker_set_alpn_protocols_for_testing;
using grpc_core::internal::tls_tsi_handshaker_set_pem_root_for_testing;
using grpc_core::internal::tls_tsi_handshaker_set_session_cache_for_testing;
using grpc_core::internal::
    tls_tsi_handshaker_set_session_ticket_key_for_testing;

#define TLS_CRED_RELOAD_MODE_NUM 3
#define TLS_CRED_RELOAD_SUCCESS_NUM 2

typedef enum CredReloadMode {
  NO_RELOAD = 0,
  SYNC,
  ASYNC,
} CredReloadMode;

struct tls_cred_reload_lib {
  grpc_tls_credentials_options* client_creds_options;
  grpc_tls_credentials_options* server_creds_options;
  grpc_core::Thread client_thd;
  grpc_core::Thread server_thd;
  bool client_thd_started;
  bool server_thd_started;
  bool client_reload_succeeded;
  bool server_reload_succeeded;
  CredReloadMode client_reload_mode;
  CredReloadMode server_reload_mode;
};

static void populate_tls_key_materials_config(
    grpc_tls_key_materials_config* config,
    tsi_ssl_pem_key_cert_pair* pem_key_cert_pairs, size_t num_key_cert_pairs) {
  GPR_ASSERT(config != nullptr);
  if (num_key_cert_pairs == 0) {
    return;
  }
  grpc_ssl_pem_key_cert_pair* key_cert_pairs =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_zalloc(num_key_cert_pairs * sizeof(grpc_ssl_pem_key_cert_pair)));
  for (size_t i = 0; i < num_key_cert_pairs; i++) {
    key_cert_pairs[i].cert_chain = gpr_strdup(pem_key_cert_pairs[i].cert_chain);
    key_cert_pairs[i].private_key =
        gpr_strdup(pem_key_cert_pairs[i].private_key);
  }
  config->set_key_materials(key_cert_pairs, nullptr, num_key_cert_pairs);
  // free memeory
  for (size_t i = 0; i < num_key_cert_pairs; i++) {
    gpr_free((void*)key_cert_pairs[i].private_key);
    gpr_free((void*)key_cert_pairs[i].cert_chain);
  }
  gpr_free(key_cert_pairs);
}

static void client_options_set_key_materials_config(
    ssl_tsi_test_fixture* ssl_fixture) {
  tsi_ssl_pem_key_cert_pair* client_pem_key_cert_pairs = nullptr;
  GPR_ASSERT(ssl_fixture != nullptr);
  GPR_ASSERT(ssl_fixture->key_cert_lib != nullptr);
  ssl_key_cert_lib* key_cert_lib = ssl_fixture->key_cert_lib;
  client_pem_key_cert_pairs = key_cert_lib->use_bad_client_cert
                                  ? &key_cert_lib->bad_client_pem_key_cert_pair
                                  : &key_cert_lib->client_pem_key_cert_pair;
  tls_cred_reload_lib* reload_lib = ssl_fixture->cred_reload_lib;
  populate_tls_key_materials_config(
      reload_lib->client_creds_options->mutable_key_materials_config(),
      client_pem_key_cert_pairs, 1);
}

static void server_options_set_key_materials_config(
    ssl_tsi_test_fixture* ssl_fixture) {
  GPR_ASSERT(ssl_fixture != nullptr);
  GPR_ASSERT(ssl_fixture->key_cert_lib != nullptr);
  ssl_key_cert_lib* key_cert_lib = ssl_fixture->key_cert_lib;
  tsi_ssl_pem_key_cert_pair* server_pem_key_cert_pairs =
      key_cert_lib->use_bad_server_cert
          ? key_cert_lib->bad_server_pem_key_cert_pairs
          : key_cert_lib->server_pem_key_cert_pairs;
  size_t num_server_key_cert_pairs =
      key_cert_lib->use_bad_server_cert
          ? key_cert_lib->bad_server_num_key_cert_pairs
          : key_cert_lib->server_num_key_cert_pairs;
  tls_cred_reload_lib* reload_lib = ssl_fixture->cred_reload_lib;
  populate_tls_key_materials_config(
      reload_lib->server_creds_options->mutable_key_materials_config(),
      server_pem_key_cert_pairs, num_server_key_cert_pairs);
}

static int client_schedule_sync(void* config_user_data,
                                grpc_tls_credential_reload_arg* arg) {
  ssl_tsi_test_fixture* ssl_fixture =
      static_cast<ssl_tsi_test_fixture*>(config_user_data);
  GPR_ASSERT(ssl_fixture != nullptr);
  GPR_ASSERT(ssl_fixture->cred_reload_lib != nullptr);
  tls_cred_reload_lib* cred_reload_lib = ssl_fixture->cred_reload_lib;
  if (!cred_reload_lib->client_reload_succeeded) {
    arg->status = GRPC_STATUS_INTERNAL;
  }
  return 0;
}

static int server_schedule_sync(void* config_user_data,
                                grpc_tls_credential_reload_arg* arg) {
  ssl_tsi_test_fixture* ssl_fixture =
      static_cast<ssl_tsi_test_fixture*>(config_user_data);
  GPR_ASSERT(ssl_fixture != nullptr);
  GPR_ASSERT(ssl_fixture->cred_reload_lib != nullptr);
  tls_cred_reload_lib* cred_reload_lib = ssl_fixture->cred_reload_lib;
  if (!cred_reload_lib->server_reload_succeeded) {
    arg->status = GRPC_STATUS_INTERNAL;
  }
  return 0;
}

static void client_credential_reload_cb(void* user_data) {
  ssl_tsi_test_fixture* ssl_fixture =
      static_cast<ssl_tsi_test_fixture*>(user_data);
  GPR_ASSERT(ssl_fixture != nullptr);
  grpc_tls_credential_reload_arg* reload_arg =
      tls_tsi_handshaker_get_credential_reload_arg_for_testing(
          ssl_fixture->base.client_handshaker);
  GPR_ASSERT(reload_arg != nullptr);
  client_schedule_sync(ssl_fixture, reload_arg);
  reload_arg->cb(reload_arg);
}

static void server_credential_reload_cb(void* user_data) {
  ssl_tsi_test_fixture* ssl_fixture =
      static_cast<ssl_tsi_test_fixture*>(user_data);
  GPR_ASSERT(ssl_fixture != nullptr);
  grpc_tls_credential_reload_arg* reload_arg =
      tls_tsi_handshaker_get_credential_reload_arg_for_testing(
          ssl_fixture->base.server_handshaker);
  GPR_ASSERT(reload_arg != nullptr);
  server_schedule_sync(ssl_fixture, reload_arg);
  reload_arg->cb(reload_arg);
}

static int server_schedule_async(void* config_user_data,
                                 grpc_tls_credential_reload_arg* arg) {
  ssl_tsi_test_fixture* ssl_fixture =
      static_cast<ssl_tsi_test_fixture*>(config_user_data);
  GPR_ASSERT(ssl_fixture != nullptr);
  GPR_ASSERT(ssl_fixture->cred_reload_lib != nullptr);
  ssl_fixture->cred_reload_lib->server_thd_started = true;
  ssl_fixture->cred_reload_lib->server_thd.Start();
  return 1;
}

static int client_schedule_async(void* config_user_data,
                                 grpc_tls_credential_reload_arg* arg) {
  ssl_tsi_test_fixture* ssl_fixture =
      static_cast<ssl_tsi_test_fixture*>(config_user_data);
  GPR_ASSERT(ssl_fixture != nullptr);
  GPR_ASSERT(ssl_fixture->cred_reload_lib != nullptr);
  ssl_fixture->cred_reload_lib->client_thd_started = true;
  ssl_fixture->cred_reload_lib->client_thd.Start();
  return 1;
}

static void populate_tls_credential_reload_config(
    ssl_tsi_test_fixture* ssl_fixture) {
  GPR_ASSERT(ssl_fixture != nullptr);
  GPR_ASSERT(ssl_fixture->cred_reload_lib != nullptr);
  tls_cred_reload_lib* reload_lib = ssl_fixture->cred_reload_lib;
  grpc_tls_credentials_options* c = reload_lib->client_creds_options;
  grpc_tls_credentials_options* s = reload_lib->server_creds_options;
  client_options_set_key_materials_config(ssl_fixture);
  server_options_set_key_materials_config(ssl_fixture);
  GPR_ASSERT(c != nullptr);
  GPR_ASSERT(s != nullptr);
  if (reload_lib->client_reload_mode == NO_RELOAD) {
    c->set_credential_reload_config(nullptr);
  } else if (reload_lib->client_reload_mode == SYNC) {
    c->set_credential_reload_config(grpc_tls_credential_reload_config_create(
        ssl_fixture, client_schedule_sync, nullptr, nullptr));
  } else if (reload_lib->client_reload_mode == ASYNC) {
    c->set_credential_reload_config(grpc_tls_credential_reload_config_create(
        ssl_fixture, client_schedule_async, nullptr, nullptr));
  }
  if (reload_lib->server_reload_mode == NO_RELOAD) {
    s->set_credential_reload_config(nullptr);
  } else if (reload_lib->server_reload_mode == SYNC) {
    s->set_credential_reload_config(grpc_tls_credential_reload_config_create(
        ssl_fixture, server_schedule_sync, nullptr, nullptr));
  } else if (reload_lib->server_reload_mode == ASYNC) {
    s->set_credential_reload_config(grpc_tls_credential_reload_config_create(
        ssl_fixture, server_schedule_async, nullptr, nullptr));
  }
}

static void tls_test_setup_handshakers(tsi_test_fixture* fixture) {
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  GPR_ASSERT(ssl_fixture != nullptr);
  GPR_ASSERT(ssl_fixture->key_cert_lib != nullptr);
  GPR_ASSERT(ssl_fixture->alpn_lib != nullptr);
  GPR_ASSERT(ssl_fixture->cred_reload_lib != nullptr);
  ssl_key_cert_lib* key_cert_lib = ssl_fixture->key_cert_lib;
  ssl_alpn_lib* alpn_lib = ssl_fixture->alpn_lib;
  tls_cred_reload_lib* reload_lib = ssl_fixture->cred_reload_lib;

  /* Create client TSI handshaker. */
  GPR_ASSERT(reload_lib->client_creds_options != nullptr);
  populate_tls_credential_reload_config(ssl_fixture);
  GPR_ASSERT(tls_tsi_handshaker_create(
                 ssl_fixture->server_name_indication,
                 ssl_fixture->session_cache, reload_lib->client_creds_options,
                 true, &ssl_fixture->base.client_handshaker) == TSI_OK);
  tls_tsi_handshaker_set_alpn_protocols_for_testing(
      ssl_fixture->base.client_handshaker, nullptr, 0);
  if (alpn_lib->alpn_mode == ALPN_CLIENT_NO_SERVER ||
      alpn_lib->alpn_mode == ALPN_CLIENT_SERVER_OK ||
      alpn_lib->alpn_mode == ALPN_CLIENT_SERVER_MISMATCH) {
    tls_tsi_handshaker_set_alpn_protocols_for_testing(
        ssl_fixture->base.client_handshaker, alpn_lib->client_alpn_protocols,
        alpn_lib->num_client_alpn_protocols);
  }
  tls_tsi_handshaker_set_pem_root_for_testing(
      ssl_fixture->base.client_handshaker, key_cert_lib->root_cert,
      key_cert_lib->use_root_store ? key_cert_lib->root_store : nullptr);
  if (ssl_fixture->session_cache != nullptr) {
    tls_tsi_handshaker_set_session_cache_for_testing(
        ssl_fixture->base.client_handshaker, ssl_fixture->session_cache);
  }
  /* Create server TSI handshaker. */
  GPR_ASSERT(reload_lib->server_creds_options != nullptr);
  GPR_ASSERT(tls_tsi_handshaker_create(
                 nullptr, nullptr, reload_lib->server_creds_options, false,
                 &ssl_fixture->base.server_handshaker) == TSI_OK);
  tls_tsi_handshaker_set_alpn_protocols_for_testing(
      ssl_fixture->base.server_handshaker, nullptr, 0);
  if (alpn_lib->alpn_mode == ALPN_SERVER_NO_CLIENT ||
      alpn_lib->alpn_mode == ALPN_CLIENT_SERVER_OK ||
      alpn_lib->alpn_mode == ALPN_CLIENT_SERVER_MISMATCH) {
    size_t num_alpn_protocols =
        alpn_lib->alpn_mode == ALPN_CLIENT_SERVER_MISMATCH
            ? alpn_lib->num_server_alpn_protocols - 1
            : alpn_lib->num_server_alpn_protocols;
    tls_tsi_handshaker_set_alpn_protocols_for_testing(
        ssl_fixture->base.server_handshaker, alpn_lib->server_alpn_protocols,
        num_alpn_protocols);
  }
  tls_tsi_handshaker_set_pem_root_for_testing(
      ssl_fixture->base.server_handshaker, key_cert_lib->root_cert, nullptr);
  if (ssl_fixture->force_client_auth) {
    grpc_tls_credentials_options_set_cert_request_type(
        reload_lib->server_creds_options,
        GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  } else {
    grpc_tls_credentials_options_set_cert_request_type(
        reload_lib->server_creds_options,
        GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
  }
  tls_tsi_handshaker_set_session_ticket_key_for_testing(
      ssl_fixture->base.server_handshaker, ssl_fixture->session_ticket_key,
      ssl_fixture->session_ticket_key_size);
}

static void tls_test_check_handshaker_peers(tsi_test_fixture* fixture) {
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  GPR_ASSERT(ssl_fixture != nullptr);
  GPR_ASSERT(ssl_fixture->key_cert_lib != nullptr);
  ssl_key_cert_lib* key_cert_lib = ssl_fixture->key_cert_lib;
  bool expect_success =
      !(key_cert_lib->use_bad_server_cert ||
        (key_cert_lib->use_bad_client_cert && ssl_fixture->force_client_auth));
  GPR_ASSERT(ssl_fixture->cred_reload_lib);
  tls_cred_reload_lib* cred_reload_lib = ssl_fixture->cred_reload_lib;
  if (cred_reload_lib->client_reload_mode != NO_RELOAD &&
      !cred_reload_lib->client_reload_succeeded) {
    expect_success = false;
  }
  if (cred_reload_lib->server_reload_mode != NO_RELOAD &&
      !cred_reload_lib->server_reload_succeeded) {
    expect_success = false;
  }
  ssl_tsi_test_check_handshaker_peers(fixture, expect_success);
}

static void tls_test_destruct(tsi_test_fixture* fixture) {
  ssl_tsi_test_fixture_cleanup(fixture);
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  tls_cred_reload_lib* lib = ssl_fixture->cred_reload_lib;
  if (lib == nullptr) {
    return;
  }
  if (lib->client_thd_started) {
    lib->client_thd.Join();
  }
  if (lib->server_thd_started) {
    lib->server_thd.Join();
  }
  lib->client_creds_options->Unref();
  lib->server_creds_options->Unref();
  gpr_free(lib);
}

static const struct tsi_test_fixture_vtable vtable = {
    tls_test_setup_handshakers, tls_test_check_handshaker_peers,
    tls_test_destruct};

static tsi_test_fixture* tls_tsi_test_fixture_create(CredReloadMode client_mode,
                                                     CredReloadMode server_mode,
                                                     bool c_success,
                                                     bool s_success) {
  ssl_tsi_test_fixture* ssl_fixture =
      static_cast<ssl_tsi_test_fixture*>(gpr_zalloc(sizeof(*ssl_fixture)));
  ssl_fixture->base.vtable = &vtable;
  ssl_tsi_test_fixture_init(&ssl_fixture->base);
  ssl_fixture->cred_reload_lib = static_cast<tls_cred_reload_lib*>(
      gpr_zalloc(sizeof(tls_cred_reload_lib)));
  tls_cred_reload_lib* reload_lib = ssl_fixture->cred_reload_lib;
  reload_lib->client_creds_options = grpc_tls_credentials_options_create();
  reload_lib->server_creds_options = grpc_tls_credentials_options_create();
  reload_lib->client_creds_options->set_key_materials_config(
      grpc_tls_key_materials_config_create());
  reload_lib->server_creds_options->set_key_materials_config(
      grpc_tls_key_materials_config_create());
  if (client_mode == ASYNC) {
    reload_lib->client_thd =
        grpc_core::Thread("tls_transport_security_test_client",
                          &client_credential_reload_cb, ssl_fixture);
  }
  if (server_mode == ASYNC) {
    reload_lib->server_thd =
        grpc_core::Thread("tls_transport_security_test_client",
                          &server_credential_reload_cb, ssl_fixture);
  }
  reload_lib->client_thd_started = false;
  reload_lib->server_thd_started = false;
  reload_lib->client_reload_succeeded = c_success;
  reload_lib->server_reload_succeeded = s_success;
  reload_lib->client_reload_mode = client_mode;
  reload_lib->server_reload_mode = server_mode;
  return &ssl_fixture->base;
}

void ssl_tsi_test_do_handshake_tiny_handshake_buffer(CredReloadMode client_mode,
                                                     CredReloadMode server_mode,
                                                     bool c_success,
                                                     bool s_success) {
  tsi_test_fixture* fixture = tls_tsi_test_fixture_create(
      client_mode, server_mode, c_success, s_success);
  fixture->handshake_buffer_size = TSI_TEST_TINY_HANDSHAKE_BUFFER_SIZE;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_small_handshake_buffer(
    CredReloadMode client_mode, CredReloadMode server_mode, bool c_success,
    bool s_success) {
  tsi_test_fixture* fixture = tls_tsi_test_fixture_create(
      client_mode, server_mode, c_success, s_success);
  fixture->handshake_buffer_size = TSI_TEST_SMALL_HANDSHAKE_BUFFER_SIZE;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake(CredReloadMode client_mode,
                               CredReloadMode server_mode, bool c_success,
                               bool s_success) {
  tsi_test_fixture* fixture = tls_tsi_test_fixture_create(
      client_mode, server_mode, c_success, s_success);
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_root_store(CredReloadMode client_mode,
                                               CredReloadMode server_mode,
                                               bool c_success, bool s_success) {
  tsi_test_fixture* fixture = tls_tsi_test_fixture_create(
      client_mode, server_mode, c_success, s_success);
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->key_cert_lib->use_root_store = true;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_client_authentication(
    CredReloadMode client_mode, CredReloadMode server_mode, bool c_success,
    bool s_success) {
  tsi_test_fixture* fixture = tls_tsi_test_fixture_create(
      client_mode, server_mode, c_success, s_success);
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->force_client_auth = true;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_client_authentication_and_root_store(
    CredReloadMode client_mode, CredReloadMode server_mode, bool c_success,
    bool s_success) {
  tsi_test_fixture* fixture = tls_tsi_test_fixture_create(
      client_mode, server_mode, c_success, s_success);
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->force_client_auth = true;
  ssl_fixture->key_cert_lib->use_root_store = true;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_server_name_indication_exact_domain(
    CredReloadMode client_mode, CredReloadMode server_mode, bool c_success,
    bool s_success) {
  /* server1 cert contains "waterzooi.test.google.be" in SAN. */
  tsi_test_fixture* fixture = tls_tsi_test_fixture_create(
      client_mode, server_mode, c_success, s_success);
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->server_name_indication =
      const_cast<char*>("waterzooi.test.google.be");
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_server_name_indication_wild_star_domain(
    CredReloadMode client_mode, CredReloadMode server_mode, bool c_success,
    bool s_success) {
  /* server1 cert contains "*.test.google.fr" in SAN. */
  tsi_test_fixture* fixture = tls_tsi_test_fixture_create(
      client_mode, server_mode, c_success, s_success);
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->server_name_indication =
      const_cast<char*>("juju.test.google.fr");
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_bad_server_cert(CredReloadMode client_mode,
                                                    CredReloadMode server_mode,
                                                    bool c_success,
                                                    bool s_success) {
  tsi_test_fixture* fixture = tls_tsi_test_fixture_create(
      client_mode, server_mode, c_success, s_success);
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->key_cert_lib->use_bad_server_cert = true;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_with_bad_client_cert(CredReloadMode client_mode,
                                                    CredReloadMode server_mode,
                                                    bool c_success,
                                                    bool s_success) {
  tsi_test_fixture* fixture = tls_tsi_test_fixture_create(
      client_mode, server_mode, c_success, s_success);
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->key_cert_lib->use_bad_client_cert = true;
  ssl_fixture->force_client_auth = true;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_alpn_client_no_server(CredReloadMode client_mode,
                                                     CredReloadMode server_mode,
                                                     bool c_success,
                                                     bool s_success) {
  tsi_test_fixture* fixture = tls_tsi_test_fixture_create(
      client_mode, server_mode, c_success, s_success);
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->alpn_lib->alpn_mode = ALPN_CLIENT_NO_SERVER;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_alpn_server_no_client(CredReloadMode client_mode,
                                                     CredReloadMode server_mode,
                                                     bool c_success,
                                                     bool s_success) {
  tsi_test_fixture* fixture = tls_tsi_test_fixture_create(
      client_mode, server_mode, c_success, s_success);
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->alpn_lib->alpn_mode = ALPN_SERVER_NO_CLIENT;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_alpn_client_server_mismatch(
    CredReloadMode client_mode, CredReloadMode server_mode, bool c_success,
    bool s_success) {
  tsi_test_fixture* fixture = tls_tsi_test_fixture_create(
      client_mode, server_mode, c_success, s_success);
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->alpn_lib->alpn_mode = ALPN_CLIENT_SERVER_MISMATCH;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_handshake_alpn_client_server_ok(CredReloadMode client_mode,
                                                     CredReloadMode server_mode,
                                                     bool c_success,
                                                     bool s_success) {
  tsi_test_fixture* fixture = tls_tsi_test_fixture_create(
      client_mode, server_mode, c_success, s_success);
  ssl_tsi_test_fixture* ssl_fixture =
      reinterpret_cast<ssl_tsi_test_fixture*>(fixture);
  ssl_fixture->alpn_lib->alpn_mode = ALPN_CLIENT_SERVER_OK;
  tsi_test_do_handshake(fixture);
  tsi_test_fixture_destroy(fixture);
}

void ssl_tsi_test_do_round_trip_for_all_configs(CredReloadMode client_mode,
                                                CredReloadMode server_mode,
                                                bool c_success,
                                                bool s_success) {
  unsigned int* bit_array = static_cast<unsigned int*>(
      gpr_zalloc(sizeof(unsigned int) * TSI_TEST_NUM_OF_ARGUMENTS));
  const unsigned int mask = 1U << (TSI_TEST_NUM_OF_ARGUMENTS - 1);
  for (unsigned int val = 0; val < TSI_TEST_NUM_OF_COMBINATIONS; val++) {
    unsigned int v = val;
    for (unsigned int ind = 0; ind < TSI_TEST_NUM_OF_ARGUMENTS; ind++) {
      bit_array[ind] = (v & mask) ? 1 : 0;
      v <<= 1;
    }
    tsi_test_fixture* fixture = tls_tsi_test_fixture_create(
        client_mode, server_mode, c_success, s_success);
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

void ssl_tsi_test_do_round_trip_odd_buffer_size(CredReloadMode client_mode,
                                                CredReloadMode server_mode,
                                                bool c_success,
                                                bool s_success) {
  const size_t odd_sizes[] = {1025, 2051, 4103, 8207, 16409};
  const size_t size = sizeof(odd_sizes) / sizeof(size_t);
  for (size_t ind1 = 0; ind1 < size; ind1++) {
    for (size_t ind2 = 0; ind2 < size; ind2++) {
      for (size_t ind3 = 0; ind3 < size; ind3++) {
        for (size_t ind4 = 0; ind4 < size; ind4++) {
          for (size_t ind5 = 0; ind5 < size; ind5++) {
            tsi_test_fixture* fixture = tls_tsi_test_fixture_create(
                client_mode, server_mode, c_success, s_success);
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

void ssl_tsi_test_do_handshake_session_cache(CredReloadMode client_mode,
                                             CredReloadMode server_mode,
                                             bool c_success, bool s_success) {
  tsi_ssl_session_cache* session_cache = tsi_ssl_session_cache_create_lru(16);
  char session_ticket_key[kSessionTicketEncryptionKeySize];
  auto do_handshake = [&session_ticket_key, &session_cache](
                          bool session_reused, CredReloadMode client_mode,
                          CredReloadMode server_mode, bool c_success,
                          bool s_success) {
    tsi_test_fixture* fixture = tls_tsi_test_fixture_create(
        client_mode, server_mode, c_success, s_success);
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
  do_handshake(false, client_mode, server_mode, c_success, s_success);
  do_handshake(true, client_mode, server_mode, c_success, s_success);
  do_handshake(true, client_mode, server_mode, c_success, s_success);
  // Changing session_ticket_key on server invalidates ticket.
  memset(session_ticket_key, 'b', sizeof(session_ticket_key));
  do_handshake(false, client_mode, server_mode, c_success, s_success);
  do_handshake(true, client_mode, server_mode, c_success, s_success);
  memset(session_ticket_key, 'c', sizeof(session_ticket_key));
  do_handshake(false, client_mode, server_mode, c_success, s_success);
  do_handshake(true, client_mode, server_mode, c_success, s_success);
  tsi_ssl_session_cache_unref(session_cache);
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  for (int c = 2; c < TLS_CRED_RELOAD_MODE_NUM; c++) {
    for (int s = 2; s < TLS_CRED_RELOAD_MODE_NUM; s++) {
      for (int c_success = 1; c_success < TLS_CRED_RELOAD_SUCCESS_NUM;
           c_success++) {
        for (int s_success = 1; s_success < TLS_CRED_RELOAD_SUCCESS_NUM;
             s_success++) {
          gpr_log(GPR_ERROR,
                  "c_mode: %d, s_mode: %d, c_success: %d, s_success: %d", c, s,
                  c_success, s_success);
          CredReloadMode c_mode = static_cast<CredReloadMode>(c);
          CredReloadMode s_mode = static_cast<CredReloadMode>(s);
          ssl_tsi_test_do_handshake_tiny_handshake_buffer(c_mode, s_mode,
                                                          c_success, s_success);
          ssl_tsi_test_do_handshake_small_handshake_buffer(
              c_mode, s_mode, c_success, s_success);
          ssl_tsi_test_do_handshake(c_mode, s_mode, c_success, s_success);
          ssl_tsi_test_do_handshake_with_root_store(c_mode, s_mode, c_success,
                                                    s_success);
          ssl_tsi_test_do_handshake_with_client_authentication(
              c_mode, s_mode, c_success, s_success);
          ssl_tsi_test_do_handshake_with_client_authentication_and_root_store(
              c_mode, s_mode, c_success, s_success);
          ssl_tsi_test_do_handshake_with_server_name_indication_exact_domain(
              c_mode, s_mode, c_success, s_success);
          ssl_tsi_test_do_handshake_with_server_name_indication_wild_star_domain(
              c_mode, s_mode, c_success, s_success);
          ssl_tsi_test_do_handshake_with_bad_server_cert(c_mode, s_mode,
                                                         c_success, s_success);
          ssl_tsi_test_do_handshake_with_bad_client_cert(c_mode, s_mode,
                                                         c_success, s_success);
#ifdef OPENSSL_IS_BORINGSSL
          // BoringSSL and OpenSSL have different behaviors on mismatched ALPN.
          ssl_tsi_test_do_handshake_alpn_client_no_server(c_mode, s_mode,
                                                          c_success, s_success);
          ssl_tsi_test_do_handshake_alpn_client_server_mismatch(
              c_mode, s_mode, c_success, s_success);
#endif
          ssl_tsi_test_do_handshake_alpn_server_no_client(c_mode, s_mode,
                                                          c_success, s_success);
          ssl_tsi_test_do_handshake_alpn_client_server_ok(c_mode, s_mode,
                                                          c_success, s_success);
          ssl_tsi_test_do_handshake_session_cache(c_mode, s_mode, c_success,
                                                  s_success);
          ssl_tsi_test_do_round_trip_for_all_configs(c_mode, s_mode, c_success,
                                                     s_success);
          ssl_tsi_test_do_round_trip_odd_buffer_size(c_mode, s_mode, c_success,
                                                     s_success);
        }
      }
    }
  }
  grpc_shutdown();
  return 0;
}
