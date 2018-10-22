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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/security_connector/ssl/ssl_security_connector.h"

#include <stdbool.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/alpn/alpn.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/ssl/ssl_credentials.h"
#include "src/core/lib/security/security_connector/load_system_roots.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"
#include "src/core/lib/security/transport/security_handshaker.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security.h"

typedef struct {
  grpc_channel_security_connector base;
  tsi_ssl_client_handshaker_factory* client_handshaker_factory;
  char* target_name;
  char* overridden_target_name;
  const verify_peer_options* verify_options;
} grpc_ssl_channel_security_connector;

typedef struct {
  grpc_server_security_connector base;
  tsi_ssl_server_handshaker_factory* server_handshaker_factory;
} grpc_ssl_server_security_connector;

static bool server_connector_has_cert_config_fetcher(
    grpc_ssl_server_security_connector* c) {
  GPR_ASSERT(c != nullptr);
  grpc_ssl_server_credentials* server_creds =
      reinterpret_cast<grpc_ssl_server_credentials*>(c->base.server_creds);
  GPR_ASSERT(server_creds != nullptr);
  return server_creds->certificate_config_fetcher.cb != nullptr;
}

static void ssl_channel_destroy(grpc_security_connector* sc) {
  grpc_ssl_channel_security_connector* c =
      reinterpret_cast<grpc_ssl_channel_security_connector*>(sc);
  grpc_channel_credentials_unref(c->base.channel_creds);
  grpc_call_credentials_unref(c->base.request_metadata_creds);
  tsi_ssl_client_handshaker_factory_unref(c->client_handshaker_factory);
  c->client_handshaker_factory = nullptr;
  if (c->target_name != nullptr) gpr_free(c->target_name);
  if (c->overridden_target_name != nullptr) gpr_free(c->overridden_target_name);
  gpr_free(sc);
}

static void ssl_server_destroy(grpc_security_connector* sc) {
  grpc_ssl_server_security_connector* c =
      reinterpret_cast<grpc_ssl_server_security_connector*>(sc);
  grpc_server_credentials_unref(c->base.server_creds);
  tsi_ssl_server_handshaker_factory_unref(c->server_handshaker_factory);
  c->server_handshaker_factory = nullptr;
  gpr_free(sc);
}

static void ssl_channel_add_handshakers(grpc_channel_security_connector* sc,
                                        grpc_pollset_set* interested_parties,
                                        grpc_handshake_manager* handshake_mgr) {
  grpc_ssl_channel_security_connector* c =
      reinterpret_cast<grpc_ssl_channel_security_connector*>(sc);
  // Instantiate TSI handshaker.
  tsi_handshaker* tsi_hs = nullptr;
  tsi_result result = tsi_ssl_client_handshaker_factory_create_handshaker(
      c->client_handshaker_factory,
      c->overridden_target_name != nullptr ? c->overridden_target_name
                                           : c->target_name,
      &tsi_hs);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
            tsi_result_to_string(result));
    return;
  }
  // Create handshakers.
  grpc_handshake_manager_add(
      handshake_mgr, grpc_security_handshaker_create(tsi_hs, &sc->base));
}

/* Attempts to replace the server_handshaker_factory with a new factory using
 * the provided grpc_ssl_server_certificate_config. Should new factory creation
 * fail, the existing factory will not be replaced. Returns true on success (new
 * factory created). */
static bool try_replace_server_handshaker_factory(
    grpc_ssl_server_security_connector* sc,
    const grpc_ssl_server_certificate_config* config) {
  if (config == nullptr) {
    gpr_log(GPR_ERROR,
            "Server certificate config callback returned invalid (NULL) "
            "config.");
    return false;
  }
  gpr_log(GPR_DEBUG, "Using new server certificate config (%p).", config);

  size_t num_alpn_protocols = 0;
  const char** alpn_protocol_strings =
      grpc_fill_alpn_protocol_strings(&num_alpn_protocols);
  tsi_ssl_pem_key_cert_pair* cert_pairs = grpc_convert_grpc_to_tsi_cert_pairs(
      config->pem_key_cert_pairs, config->num_key_cert_pairs);
  tsi_ssl_server_handshaker_factory* new_handshaker_factory = nullptr;
  grpc_ssl_server_credentials* server_creds =
      reinterpret_cast<grpc_ssl_server_credentials*>(sc->base.server_creds);
  tsi_result result = tsi_create_ssl_server_handshaker_factory_ex(
      cert_pairs, config->num_key_cert_pairs, config->pem_root_certs,
      grpc_get_tsi_client_certificate_request_type(
          server_creds->config.client_certificate_request),
      grpc_get_ssl_cipher_suites(), alpn_protocol_strings,
      static_cast<uint16_t>(num_alpn_protocols), &new_handshaker_factory);
  gpr_free(cert_pairs);
  gpr_free((void*)alpn_protocol_strings);

  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
            tsi_result_to_string(result));
    return false;
  }
  tsi_ssl_server_handshaker_factory_unref(sc->server_handshaker_factory);
  sc->server_handshaker_factory = new_handshaker_factory;
  return true;
}

/* Attempts to fetch the server certificate config if a callback is available.
 * Current certificate config will continue to be used if the callback returns
 * an error. Returns true if new credentials were sucessfully loaded. */
static bool try_fetch_ssl_server_credentials(
    grpc_ssl_server_security_connector* sc) {
  grpc_ssl_server_certificate_config* certificate_config = nullptr;
  bool status;

  GPR_ASSERT(sc != nullptr);
  if (!server_connector_has_cert_config_fetcher(sc)) return false;

  grpc_ssl_server_credentials* server_creds =
      reinterpret_cast<grpc_ssl_server_credentials*>(sc->base.server_creds);
  grpc_ssl_certificate_config_reload_status cb_result =
      server_creds->certificate_config_fetcher.cb(
          server_creds->certificate_config_fetcher.user_data,
          &certificate_config);
  if (cb_result == GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED) {
    gpr_log(GPR_DEBUG, "No change in SSL server credentials.");
    status = false;
  } else if (cb_result == GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW) {
    status = try_replace_server_handshaker_factory(sc, certificate_config);
  } else {
    // Log error, continue using previously-loaded credentials.
    gpr_log(GPR_ERROR,
            "Failed fetching new server credentials, continuing to "
            "use previously-loaded credentials.");
    status = false;
  }

  if (certificate_config != nullptr) {
    grpc_ssl_server_certificate_config_destroy(certificate_config);
  }
  return status;
}

static void ssl_server_add_handshakers(grpc_server_security_connector* sc,
                                       grpc_pollset_set* interested_parties,
                                       grpc_handshake_manager* handshake_mgr) {
  grpc_ssl_server_security_connector* c =
      reinterpret_cast<grpc_ssl_server_security_connector*>(sc);
  // Instantiate TSI handshaker.
  try_fetch_ssl_server_credentials(c);
  tsi_handshaker* tsi_hs = nullptr;
  tsi_result result = tsi_ssl_server_handshaker_factory_create_handshaker(
      c->server_handshaker_factory, &tsi_hs);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
            tsi_result_to_string(result));
    return;
  }
  // Create handshakers.
  grpc_handshake_manager_add(
      handshake_mgr, grpc_security_handshaker_create(tsi_hs, &sc->base));
}

static grpc_error* ssl_check_peer(grpc_security_connector* sc,
                                  const char* peer_name, const tsi_peer* peer,
                                  grpc_auth_context** auth_context) {
#if TSI_OPENSSL_ALPN_SUPPORT
  /* Check the ALPN if ALPN is supported. */
  const tsi_peer_property* p =
      tsi_peer_get_property_by_name(peer, TSI_SSL_ALPN_SELECTED_PROTOCOL);
  if (p == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Cannot check peer: missing selected ALPN property.");
  }
  if (!grpc_chttp2_is_alpn_version_supported(p->value.data, p->value.length)) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Cannot check peer: invalid ALPN value.");
  }
#endif /* TSI_OPENSSL_ALPN_SUPPORT */
  /* Check the peer name if specified. */
  if (peer_name != nullptr && !grpc_ssl_host_matches_name(peer, peer_name)) {
    char* msg;
    gpr_asprintf(&msg, "Peer name %s is not in peer certificate", peer_name);
    grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    return error;
  }
  *auth_context = grpc_ssl_peer_to_auth_context(peer);
  return GRPC_ERROR_NONE;
}

static void ssl_channel_check_peer(grpc_security_connector* sc, tsi_peer peer,
                                   grpc_auth_context** auth_context,
                                   grpc_closure* on_peer_checked) {
  grpc_ssl_channel_security_connector* c =
      reinterpret_cast<grpc_ssl_channel_security_connector*>(sc);
  const char* target_name = c->overridden_target_name != nullptr
                                ? c->overridden_target_name
                                : c->target_name;
  grpc_error* error = ssl_check_peer(sc, target_name, &peer, auth_context);
  if (error == GRPC_ERROR_NONE &&
      c->verify_options->verify_peer_callback != nullptr) {
    const tsi_peer_property* p =
        tsi_peer_get_property_by_name(&peer, TSI_X509_PEM_CERT_PROPERTY);
    if (p == nullptr) {
      error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Cannot check peer: missing pem cert property.");
    } else {
      char* peer_pem = static_cast<char*>(gpr_malloc(p->value.length + 1));
      memcpy(peer_pem, p->value.data, p->value.length);
      peer_pem[p->value.length] = '\0';
      int callback_status = c->verify_options->verify_peer_callback(
          target_name, peer_pem,
          c->verify_options->verify_peer_callback_userdata);
      gpr_free(peer_pem);
      if (callback_status) {
        char* msg;
        gpr_asprintf(&msg, "Verify peer callback returned a failure (%d)",
                     callback_status);
        error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
        gpr_free(msg);
      }
    }
  }
  GRPC_CLOSURE_SCHED(on_peer_checked, error);
  tsi_peer_destruct(&peer);
}

static void ssl_server_check_peer(grpc_security_connector* sc, tsi_peer peer,
                                  grpc_auth_context** auth_context,
                                  grpc_closure* on_peer_checked) {
  grpc_error* error = ssl_check_peer(sc, nullptr, &peer, auth_context);
  tsi_peer_destruct(&peer);
  GRPC_CLOSURE_SCHED(on_peer_checked, error);
}

static int ssl_channel_cmp(grpc_security_connector* sc1,
                           grpc_security_connector* sc2) {
  grpc_ssl_channel_security_connector* c1 =
      reinterpret_cast<grpc_ssl_channel_security_connector*>(sc1);
  grpc_ssl_channel_security_connector* c2 =
      reinterpret_cast<grpc_ssl_channel_security_connector*>(sc2);
  int c = grpc_channel_security_connector_cmp(&c1->base, &c2->base);
  if (c != 0) return c;
  c = strcmp(c1->target_name, c2->target_name);
  if (c != 0) return c;
  return (c1->overridden_target_name == nullptr ||
          c2->overridden_target_name == nullptr)
             ? GPR_ICMP(c1->overridden_target_name, c2->overridden_target_name)
             : strcmp(c1->overridden_target_name, c2->overridden_target_name);
}

static int ssl_server_cmp(grpc_security_connector* sc1,
                          grpc_security_connector* sc2) {
  return grpc_server_security_connector_cmp(
      reinterpret_cast<grpc_server_security_connector*>(sc1),
      reinterpret_cast<grpc_server_security_connector*>(sc2));
}

static bool ssl_channel_check_call_host(grpc_channel_security_connector* sc,
                                        const char* host,
                                        grpc_auth_context* auth_context,
                                        grpc_closure* on_call_host_checked,
                                        grpc_error** error) {
  grpc_ssl_channel_security_connector* c =
      reinterpret_cast<grpc_ssl_channel_security_connector*>(sc);
  grpc_security_status status = GRPC_SECURITY_ERROR;
  tsi_peer peer = grpc_shallow_peer_from_ssl_auth_context(auth_context);
  if (grpc_ssl_host_matches_name(&peer, host)) status = GRPC_SECURITY_OK;
  /* If the target name was overridden, then the original target_name was
     'checked' transitively during the previous peer check at the end of the
     handshake. */
  if (c->overridden_target_name != nullptr &&
      strcmp(host, c->target_name) == 0) {
    status = GRPC_SECURITY_OK;
  }
  if (status != GRPC_SECURITY_OK) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "call host does not match SSL server name");
  }
  grpc_shallow_peer_destruct(&peer);
  return true;
}

static void ssl_channel_cancel_check_call_host(
    grpc_channel_security_connector* sc, grpc_closure* on_call_host_checked,
    grpc_error* error) {
  GRPC_ERROR_UNREF(error);
}

static grpc_security_connector_vtable ssl_channel_vtable = {
    ssl_channel_destroy, ssl_channel_check_peer, ssl_channel_cmp};

static grpc_security_connector_vtable ssl_server_vtable = {
    ssl_server_destroy, ssl_server_check_peer, ssl_server_cmp};

grpc_security_status grpc_ssl_channel_security_connector_create(
    grpc_channel_credentials* channel_creds,
    grpc_call_credentials* request_metadata_creds,
    const grpc_ssl_config* config, const char* target_name,
    const char* overridden_target_name,
    tsi_ssl_session_cache* ssl_session_cache,
    grpc_channel_security_connector** sc) {
  tsi_result result = TSI_OK;
  grpc_ssl_channel_security_connector* c;
  char* port;
  bool has_key_cert_pair;
  tsi_ssl_client_handshaker_options options;
  memset(&options, 0, sizeof(options));
  options.alpn_protocols =
      grpc_fill_alpn_protocol_strings(&options.num_alpn_protocols);

  if (config == nullptr || target_name == nullptr) {
    gpr_log(GPR_ERROR, "An ssl channel needs a config and a target name.");
    goto error;
  }
  if (config->pem_root_certs == nullptr) {
    // Use default root certificates.
    options.pem_root_certs = grpc_core::DefaultSslRootStore::GetPemRootCerts();
    options.root_store = grpc_core::DefaultSslRootStore::GetRootStore();
    if (options.pem_root_certs == nullptr) {
      gpr_log(GPR_ERROR, "Could not get default pem root certs.");
      goto error;
    }
  } else {
    options.pem_root_certs = config->pem_root_certs;
  }
  c = static_cast<grpc_ssl_channel_security_connector*>(
      gpr_zalloc(sizeof(grpc_ssl_channel_security_connector)));

  gpr_ref_init(&c->base.base.refcount, 1);
  c->base.base.vtable = &ssl_channel_vtable;
  c->base.base.url_scheme = GRPC_SSL_URL_SCHEME;
  c->base.channel_creds = grpc_channel_credentials_ref(channel_creds);
  c->base.request_metadata_creds =
      grpc_call_credentials_ref(request_metadata_creds);
  c->base.check_call_host = ssl_channel_check_call_host;
  c->base.cancel_check_call_host = ssl_channel_cancel_check_call_host;
  c->base.add_handshakers = ssl_channel_add_handshakers;
  gpr_split_host_port(target_name, &c->target_name, &port);
  gpr_free(port);
  if (overridden_target_name != nullptr) {
    c->overridden_target_name = gpr_strdup(overridden_target_name);
  }
  c->verify_options = &config->verify_options;

  has_key_cert_pair = config->pem_key_cert_pair != nullptr &&
                      config->pem_key_cert_pair->private_key != nullptr &&
                      config->pem_key_cert_pair->cert_chain != nullptr;
  if (has_key_cert_pair) {
    options.pem_key_cert_pair = config->pem_key_cert_pair;
  }
  options.cipher_suites = grpc_get_ssl_cipher_suites();
  options.session_cache = ssl_session_cache;
  result = tsi_create_ssl_client_handshaker_factory_with_options(
      &options, &c->client_handshaker_factory);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
            tsi_result_to_string(result));
    ssl_channel_destroy(&c->base.base);
    *sc = nullptr;
    goto error;
  }
  *sc = &c->base;
  gpr_free((void*)options.alpn_protocols);
  return GRPC_SECURITY_OK;

error:
  gpr_free((void*)options.alpn_protocols);
  return GRPC_SECURITY_ERROR;
}

static grpc_ssl_server_security_connector*
grpc_ssl_server_security_connector_initialize(
    grpc_server_credentials* server_creds) {
  grpc_ssl_server_security_connector* c =
      static_cast<grpc_ssl_server_security_connector*>(
          gpr_zalloc(sizeof(grpc_ssl_server_security_connector)));
  gpr_ref_init(&c->base.base.refcount, 1);
  c->base.base.url_scheme = GRPC_SSL_URL_SCHEME;
  c->base.base.vtable = &ssl_server_vtable;
  c->base.add_handshakers = ssl_server_add_handshakers;
  c->base.server_creds = grpc_server_credentials_ref(server_creds);
  return c;
}

grpc_security_status grpc_ssl_server_security_connector_create(
    grpc_server_credentials* gsc, grpc_server_security_connector** sc) {
  tsi_result result = TSI_OK;
  grpc_ssl_server_credentials* server_credentials =
      reinterpret_cast<grpc_ssl_server_credentials*>(gsc);
  grpc_security_status retval = GRPC_SECURITY_OK;

  GPR_ASSERT(server_credentials != nullptr);
  GPR_ASSERT(sc != nullptr);

  grpc_ssl_server_security_connector* c =
      grpc_ssl_server_security_connector_initialize(gsc);
  if (server_connector_has_cert_config_fetcher(c)) {
    // Load initial credentials from certificate_config_fetcher:
    if (!try_fetch_ssl_server_credentials(c)) {
      gpr_log(GPR_ERROR, "Failed loading SSL server credentials from fetcher.");
      retval = GRPC_SECURITY_ERROR;
    }
  } else {
    size_t num_alpn_protocols = 0;
    const char** alpn_protocol_strings =
        grpc_fill_alpn_protocol_strings(&num_alpn_protocols);
    result = tsi_create_ssl_server_handshaker_factory_ex(
        server_credentials->config.pem_key_cert_pairs,
        server_credentials->config.num_key_cert_pairs,
        server_credentials->config.pem_root_certs,
        grpc_get_tsi_client_certificate_request_type(
            server_credentials->config.client_certificate_request),
        grpc_get_ssl_cipher_suites(), alpn_protocol_strings,
        static_cast<uint16_t>(num_alpn_protocols),
        &c->server_handshaker_factory);
    gpr_free((void*)alpn_protocol_strings);
    if (result != TSI_OK) {
      gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
              tsi_result_to_string(result));
      retval = GRPC_SECURITY_ERROR;
    }
  }

  if (retval == GRPC_SECURITY_OK) {
    *sc = &c->base;
  } else {
    if (c != nullptr) ssl_server_destroy(&c->base.base);
    if (sc != nullptr) *sc = nullptr;
  }
  return retval;
}
