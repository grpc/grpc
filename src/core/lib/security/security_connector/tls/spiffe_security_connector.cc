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

#include "src/core/lib/security/security_connector/tls/spiffe_security_connector.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/security/credentials/ssl/ssl_credentials.h"
#include "src/core/lib/security/credentials/tls/spiffe_credentials.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"
#include "src/core/lib/security/transport/security_handshaker.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/tsi/ssl/ssl_transport_security.h"
#include "src/core/tsi/transport_security.h"

typedef struct {
  grpc_channel_security_connector base;
  char* target_name;
  char* overridden_target_name;
  tsi_ssl_session_cache* session_cache;
  /* Ownership of credetnial reload and server authorization check arg instances
   * will not be transferred. */
  grpc_tls_credential_reload_arg* reload_arg;
  grpc_tls_server_authorization_check_arg* check_arg;
  /* a gRPC closure handling the result of server authorization check. */
  grpc_closure* on_peer_checked;
} grpc_tls_spiffe_channel_security_connector;

typedef struct {
  grpc_server_security_connector base;
  /* credential reload arg instance whose ownership will not be transferred. */
  grpc_tls_credential_reload_arg* reload_arg;
} grpc_tls_spiffe_server_security_connector;

/* Process the result of server authorization check. */
static grpc_error* process_server_authorization_check_result(
    grpc_tls_server_authorization_check_arg* arg) {
  grpc_error* error = GRPC_ERROR_NONE;
  char* msg = nullptr;
  /* Server authorization check is cancelled by caller. */
  if (arg->status == GRPC_STATUS_CANCELLED) {
    gpr_asprintf(
        &msg,
        "Server authorization check is cancelled by the caller with error: %s",
        arg->error_details);
    error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
  } else if (arg->status == GRPC_STATUS_OK) {
    /* Server authorization check completed successfully but returned check
     * failure. */
    if (!arg->result) {
      gpr_asprintf(&msg, "Server authorization check failed with error: %s",
                   arg->error_details);
      error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    }
    /* Server authorization check did not complete correctly. */
  } else {
    gpr_asprintf(
        &msg,
        "Server authorization check did not finish correctly with error: %s",
        arg->error_details);
    error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
  }
  gpr_free(msg);
  return error;
}

/* gRPC-provided callback executed by application, which servers to bring the
 * control back to gRPC core. */
static void server_authorization_check_done_cb(
    grpc_tls_server_authorization_check_arg* arg) {
  GPR_ASSERT(arg != nullptr);
  grpc_core::ExecCtx exec_ctx;
  grpc_error* error = process_server_authorization_check_result(arg);
  grpc_tls_spiffe_channel_security_connector* sc =
      static_cast<grpc_tls_spiffe_channel_security_connector*>(
          arg->cb_user_data);
  GRPC_CLOSURE_SCHED(sc->on_peer_checked, error);
}

/** -- Util functions to create/destroy arg instances. -- */
static grpc_tls_credential_reload_arg* credential_reload_arg_create(
    grpc_tls_key_materials_config* config) {
  grpc_tls_credential_reload_arg* arg =
      static_cast<grpc_tls_credential_reload_arg*>(gpr_zalloc(sizeof(*arg)));
  arg->status = GRPC_STATUS_OK;
  arg->key_materials_config = grpc_tls_key_materials_config_create();
  if (config != nullptr) {
    grpc_tls_key_materials_config_set_key_materials(
        arg->key_materials_config, config->pem_key_cert_pairs,
        config->pem_root_certs, config->num_key_cert_pairs);
  }
  return arg;
}

static grpc_tls_server_authorization_check_arg*
server_authorization_check_arg_create(void* user_data) {
  grpc_tls_server_authorization_check_arg* arg =
      static_cast<grpc_tls_server_authorization_check_arg*>(
          gpr_zalloc(sizeof(*arg)));
  arg->cb = server_authorization_check_done_cb;
  arg->cb_user_data = user_data;
  arg->status = GRPC_STATUS_OK;
  return arg;
}

static void credential_reload_arg_destroy(grpc_tls_credential_reload_arg* arg) {
  if (arg == nullptr) {
    return;
  }
  gpr_free((void*)arg->error_details);
  grpc_tls_key_materials_config_destroy(arg->key_materials_config);
  gpr_free(arg);
}

static void server_authorization_check_arg_destroy(
    grpc_tls_server_authorization_check_arg* arg) {
  if (arg == nullptr) {
    return;
  }
  gpr_free((void*)arg->target_name);
  gpr_free((void*)arg->peer_cert);
  gpr_free((void*)arg->error_details);
  gpr_free(arg);
}

static void spiffe_channel_destroy(grpc_security_connector* sc) {
  if (sc == nullptr) {
    return;
  }
  auto c = reinterpret_cast<grpc_tls_spiffe_channel_security_connector*>(sc);
  grpc_call_credentials_unref(c->base.request_metadata_creds);
  grpc_channel_credentials_unref(c->base.channel_creds);
  gpr_free(c->target_name);
  gpr_free(c->overridden_target_name);
  credential_reload_arg_destroy(c->reload_arg);
  server_authorization_check_arg_destroy(c->check_arg);
  gpr_free(sc);
}

static void spiffe_server_destroy(grpc_security_connector* sc) {
  if (sc == nullptr) {
    return;
  }
  auto c = reinterpret_cast<grpc_tls_spiffe_server_security_connector*>(sc);
  grpc_server_credentials_unref(c->base.server_creds);
  credential_reload_arg_destroy(c->reload_arg);
  gpr_free(sc);
}

static void spiffe_channel_add_handshakers(
    grpc_channel_security_connector* sc, grpc_pollset_set* interested_parties,
    grpc_handshake_manager* handshake_mgr) {
  grpc_tls_spiffe_channel_security_connector* c =
      reinterpret_cast<grpc_tls_spiffe_channel_security_connector*>(sc);
  grpc_tls_spiffe_credentials* creds =
      reinterpret_cast<grpc_tls_spiffe_credentials*>(c->base.channel_creds);
  /* Create a TLS tsi handshaker for client. */
  tsi_handshaker* tsi_hs = nullptr;
  tsi_result result = tls_tsi_handshaker_create(
      c->overridden_target_name != nullptr ? c->overridden_target_name
                                           : c->target_name,
      c->session_cache, creds->options, c->reload_arg, true /* is_client */,
      &tsi_hs);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
            tsi_result_to_string(result));
    return;
  }
  grpc_handshake_manager_add(
      handshake_mgr, grpc_security_handshaker_create(tsi_hs, &sc->base));
}

static void spiffe_server_add_handshakers(
    grpc_server_security_connector* sc, grpc_pollset_set* interested_parties,
    grpc_handshake_manager* handshake_mgr) {
  grpc_tls_spiffe_server_security_connector* c =
      reinterpret_cast<grpc_tls_spiffe_server_security_connector*>(sc);
  grpc_tls_spiffe_server_credentials* creds =
      reinterpret_cast<grpc_tls_spiffe_server_credentials*>(
          c->base.server_creds);
  /* Create a TLS tsi handshaker for server. */
  tsi_handshaker* tsi_hs = nullptr;
  tsi_result result =
      tls_tsi_handshaker_create(nullptr, nullptr, creds->options, c->reload_arg,
                                false /* is_client */, &tsi_hs);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
            tsi_result_to_string(result));
    return;
  }
  grpc_handshake_manager_add(
      handshake_mgr, grpc_security_handshaker_create(tsi_hs, &sc->base));
}

static void spiffe_channel_check_peer(grpc_security_connector* sc,
                                      tsi_peer peer,
                                      grpc_auth_context** auth_context,
                                      grpc_closure* on_peer_checked) {
  grpc_tls_spiffe_channel_security_connector* c =
      reinterpret_cast<grpc_tls_spiffe_channel_security_connector*>(sc);
  const char* target_name = c->overridden_target_name != nullptr
                                ? c->overridden_target_name
                                : c->target_name;
  grpc_error* error = grpc_ssl_check_peer(sc, target_name, &peer, auth_context);
  grpc_tls_spiffe_credentials* creds =
      reinterpret_cast<grpc_tls_spiffe_credentials*>(c->base.channel_creds);
  GPR_ASSERT(creds->options != nullptr);
  grpc_tls_server_authorization_check_config* config =
      creds->options->server_authorization_check_config;
  if (error == GRPC_ERROR_NONE && config != nullptr) {
    /* Peer property will contain a complete certificate chain. */
    const tsi_peer_property* p =
        tsi_peer_get_property_by_name(&peer, TSI_X509_PEM_CERT_PROPERTY);
    if (p == nullptr) {
      error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Cannot check peer: missing pem cert property.");
    } else {
      char* peer_pem = static_cast<char*>(gpr_malloc(p->value.length + 1));
      memcpy(peer_pem, p->value.data, p->value.length);
      peer_pem[p->value.length] = '\0';
      GPR_ASSERT(c->check_arg != nullptr);
      c->check_arg->peer_cert = gpr_strdup(peer_pem);
      c->check_arg->target_name = gpr_strdup(target_name);
      c->on_peer_checked = on_peer_checked;
      gpr_free(peer_pem);
      int callback_status =
          config->schedule((void*)config->config_user_data, c->check_arg);
      /* Server authorization check is handled asynchronously. */
      if (callback_status) {
        tsi_peer_destruct(&peer);
        return;
      }
      /* Server authorization check is handled synchronously. */
      error = process_server_authorization_check_result(c->check_arg);
    }
  }
  GRPC_CLOSURE_SCHED(on_peer_checked, error);
  tsi_peer_destruct(&peer);
}

static void spiffe_server_check_peer(grpc_security_connector* sc, tsi_peer peer,
                                     grpc_auth_context** auth_context,
                                     grpc_closure* on_peer_checked) {
  grpc_error* error = grpc_ssl_check_peer(sc, nullptr, &peer, auth_context);
  tsi_peer_destruct(&peer);
  GRPC_CLOSURE_SCHED(on_peer_checked, error);
}

static int spiffe_channel_cmp(grpc_security_connector* sc1,
                              grpc_security_connector* sc2) {
  grpc_tls_spiffe_channel_security_connector* c1 =
      reinterpret_cast<grpc_tls_spiffe_channel_security_connector*>(sc1);
  grpc_tls_spiffe_channel_security_connector* c2 =
      reinterpret_cast<grpc_tls_spiffe_channel_security_connector*>(sc2);
  int c = grpc_channel_security_connector_cmp(&c1->base, &c2->base);
  if (c != 0) return c;
  c = strcmp(c1->target_name, c2->target_name);
  if (c != 0) return c;
  return (c1->overridden_target_name == nullptr ||
          c2->overridden_target_name == nullptr)
             ? GPR_ICMP(c1->overridden_target_name, c2->overridden_target_name)
             : strcmp(c1->overridden_target_name, c2->overridden_target_name);
}

static int spiffe_server_cmp(grpc_security_connector* sc1,
                             grpc_security_connector* sc2) {
  return grpc_server_security_connector_cmp(
      reinterpret_cast<grpc_server_security_connector*>(sc1),
      reinterpret_cast<grpc_server_security_connector*>(sc2));
}

static bool spiffe_channel_check_call_host(grpc_channel_security_connector* sc,
                                           const char* host,
                                           grpc_auth_context* auth_context,
                                           grpc_closure* on_call_host_checked,
                                           grpc_error** error) {
  grpc_tls_spiffe_channel_security_connector* c =
      reinterpret_cast<grpc_tls_spiffe_channel_security_connector*>(sc);
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

static void spiffe_channel_cancel_check_call_host(
    grpc_channel_security_connector* sc, grpc_closure* on_call_host_checked,
    grpc_error* error) {
  GRPC_ERROR_UNREF(error);
}

/* Cancel the server authorization check that is performed asynchronously. */
static void spiffe_channel_cancel_server_authorization_check(
    grpc_channel_security_connector* sc) {
  grpc_tls_spiffe_channel_security_connector* c =
      reinterpret_cast<grpc_tls_spiffe_channel_security_connector*>(sc);
  grpc_tls_spiffe_credentials* channel_creds =
      reinterpret_cast<grpc_tls_spiffe_credentials*>(c->base.channel_creds);
  GPR_ASSERT(channel_creds != nullptr && channel_creds->options != nullptr);
  if (channel_creds->options->server_authorization_check_config != nullptr) {
    grpc_tls_server_authorization_check_config* config =
        channel_creds->options->server_authorization_check_config;
    config->cancel((void*)config->config_user_data, c->check_arg);
  }
}

static grpc_security_connector_vtable spiffe_channel_vtable = {
    spiffe_channel_destroy, spiffe_channel_check_peer, spiffe_channel_cmp};

static grpc_security_connector_vtable spiffe_server_vtable = {
    spiffe_server_destroy, spiffe_server_check_peer, spiffe_server_cmp};

/** Create a gRPC TLS SPIFFE channel security connector. */
grpc_security_status grpc_tls_spiffe_channel_security_connector_create(
    grpc_channel_credentials* channel_creds,
    grpc_call_credentials* request_metadata_creds, const char* target_name,
    const char* overridden_target_name,
    tsi_ssl_session_cache* ssl_session_cache,
    grpc_channel_security_connector** sc) {
  grpc_tls_spiffe_credentials* creds =
      reinterpret_cast<grpc_tls_spiffe_credentials*>(channel_creds);
  grpc_tls_spiffe_channel_security_connector* c;
  char* port;
  if (creds->options == nullptr || target_name == nullptr) {
    gpr_log(GPR_ERROR,
            "A TLS channel needs a credential option and a target name.");
    return GRPC_SECURITY_ERROR;
  }
  /* Populate SPIFFE channel security connector. */
  c = static_cast<grpc_tls_spiffe_channel_security_connector*>(
      gpr_zalloc(sizeof(grpc_tls_spiffe_channel_security_connector)));
  gpr_ref_init(&c->base.base.refcount, 1);
  c->base.base.vtable = &spiffe_channel_vtable;
  c->base.base.url_scheme = GRPC_SSL_URL_SCHEME;
  c->base.channel_creds = grpc_channel_credentials_ref(channel_creds);
  c->base.request_metadata_creds =
      grpc_call_credentials_ref(request_metadata_creds);
  c->base.check_call_host = spiffe_channel_check_call_host;
  c->base.cancel_check_call_host = spiffe_channel_cancel_check_call_host;
  c->base.cancel_server_authorization_check =
      spiffe_channel_cancel_server_authorization_check;
  c->base.add_handshakers = spiffe_channel_add_handshakers;
  c->reload_arg =
      credential_reload_arg_create(creds->options->key_materials_config);
  c->check_arg = server_authorization_check_arg_create((void*)c);
  c->session_cache = ssl_session_cache;
  if (overridden_target_name != nullptr) {
    c->overridden_target_name = gpr_strdup(overridden_target_name);
  }
  gpr_split_host_port(target_name, &c->target_name, &port);
  gpr_free(port);
  *sc = &c->base;
  return GRPC_SECURITY_OK;
}

/** Create a gRPC TLS SPIFFE server security connector. */
grpc_security_status grpc_tls_spiffe_server_security_connector_create(
    grpc_server_credentials* gsc, grpc_server_security_connector** sc) {
  grpc_tls_spiffe_server_credentials* creds =
      reinterpret_cast<grpc_tls_spiffe_server_credentials*>(gsc);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(sc != nullptr);
  if (creds->options == nullptr) {
    gpr_log(GPR_ERROR, "A TLS server credential needs a credential option.");
    return GRPC_SECURITY_ERROR;
  }
  grpc_tls_spiffe_server_security_connector* c =
      static_cast<grpc_tls_spiffe_server_security_connector*>(
          gpr_zalloc(sizeof(*c)));
  gpr_ref_init(&c->base.base.refcount, 1);
  c->base.base.url_scheme = GRPC_SSL_URL_SCHEME;
  c->base.base.vtable = &spiffe_server_vtable;
  c->base.add_handshakers = spiffe_server_add_handshakers;
  c->base.server_creds = grpc_server_credentials_ref(gsc);
  c->reload_arg =
      credential_reload_arg_create(creds->options->key_materials_config);
  *sc = &c->base;
  return GRPC_SECURITY_OK;
}
