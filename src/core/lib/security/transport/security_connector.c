/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/lib/security/transport/security_connector.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/alpn/alpn.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/security/transport/lb_targets_info.h"
#include "src/core/lib/security/transport/secure_endpoint.h"
#include "src/core/lib/security/transport/security_handshaker.h"
#include "src/core/lib/support/env.h"
#include "src/core/lib/support/string.h"
#include "src/core/tsi/fake_transport_security.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security_adapter.h"

#ifndef NDEBUG
grpc_tracer_flag grpc_trace_security_connector_refcount =
    GRPC_TRACER_INITIALIZER(false, "security_connector_refcount");
#endif

/* -- Constants. -- */

#ifndef INSTALL_PREFIX
static const char *installed_roots_path = "/usr/share/grpc/roots.pem";
#else
static const char *installed_roots_path =
    INSTALL_PREFIX "/share/grpc/roots.pem";
#endif

/* -- Overridden default roots. -- */

static grpc_ssl_roots_override_callback ssl_roots_override_cb = NULL;

void grpc_set_ssl_roots_override_callback(grpc_ssl_roots_override_callback cb) {
  ssl_roots_override_cb = cb;
}

/* -- Cipher suites. -- */

/* Defines the cipher suites that we accept by default. All these cipher suites
   are compliant with HTTP2. */
#define GRPC_SSL_CIPHER_SUITES \
  "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384"

static gpr_once cipher_suites_once = GPR_ONCE_INIT;
static const char *cipher_suites = NULL;

static void init_cipher_suites(void) {
  char *overridden = gpr_getenv("GRPC_SSL_CIPHER_SUITES");
  cipher_suites = overridden != NULL ? overridden : GRPC_SSL_CIPHER_SUITES;
}

static const char *ssl_cipher_suites(void) {
  gpr_once_init(&cipher_suites_once, init_cipher_suites);
  return cipher_suites;
}

/* -- Common methods. -- */

/* Returns the first property with that name. */
const tsi_peer_property *tsi_peer_get_property_by_name(const tsi_peer *peer,
                                                       const char *name) {
  size_t i;
  if (peer == NULL) return NULL;
  for (i = 0; i < peer->property_count; i++) {
    const tsi_peer_property *property = &peer->properties[i];
    if (name == NULL && property->name == NULL) {
      return property;
    }
    if (name != NULL && property->name != NULL &&
        strcmp(property->name, name) == 0) {
      return property;
    }
  }
  return NULL;
}

void grpc_channel_security_connector_add_handshakers(
    grpc_exec_ctx *exec_ctx, grpc_channel_security_connector *connector,
    grpc_handshake_manager *handshake_mgr) {
  if (connector != NULL) {
    connector->add_handshakers(exec_ctx, connector, handshake_mgr);
  }
}

void grpc_server_security_connector_add_handshakers(
    grpc_exec_ctx *exec_ctx, grpc_server_security_connector *connector,
    grpc_handshake_manager *handshake_mgr) {
  if (connector != NULL) {
    connector->add_handshakers(exec_ctx, connector, handshake_mgr);
  }
}

void grpc_security_connector_check_peer(grpc_exec_ctx *exec_ctx,
                                        grpc_security_connector *sc,
                                        tsi_peer peer,
                                        grpc_auth_context **auth_context,
                                        grpc_closure *on_peer_checked) {
  if (sc == NULL) {
    GRPC_CLOSURE_SCHED(exec_ctx, on_peer_checked,
                       GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                           "cannot check peer -- no security connector"));
    tsi_peer_destruct(&peer);
  } else {
    sc->vtable->check_peer(exec_ctx, sc, peer, auth_context, on_peer_checked);
  }
}

bool grpc_channel_security_connector_check_call_host(
    grpc_exec_ctx *exec_ctx, grpc_channel_security_connector *sc,
    const char *host, grpc_auth_context *auth_context,
    grpc_closure *on_call_host_checked, grpc_error **error) {
  if (sc == NULL || sc->check_call_host == NULL) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "cannot check call host -- no security connector");
    return true;
  }
  return sc->check_call_host(exec_ctx, sc, host, auth_context,
                             on_call_host_checked, error);
}

void grpc_channel_security_connector_cancel_check_call_host(
    grpc_exec_ctx *exec_ctx, grpc_channel_security_connector *sc,
    grpc_closure *on_call_host_checked, grpc_error *error) {
  if (sc == NULL || sc->cancel_check_call_host == NULL) {
    GRPC_ERROR_UNREF(error);
    return;
  }
  sc->cancel_check_call_host(exec_ctx, sc, on_call_host_checked, error);
}

#ifndef NDEBUG
grpc_security_connector *grpc_security_connector_ref(
    grpc_security_connector *sc, const char *file, int line,
    const char *reason) {
  if (sc == NULL) return NULL;
  if (GRPC_TRACER_ON(grpc_trace_security_connector_refcount)) {
    gpr_atm val = gpr_atm_no_barrier_load(&sc->refcount.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "SECURITY_CONNECTOR:%p   ref %" PRIdPTR " -> %" PRIdPTR " %s", sc,
            val, val + 1, reason);
  }
#else
grpc_security_connector *grpc_security_connector_ref(
    grpc_security_connector *sc) {
  if (sc == NULL) return NULL;
#endif
  gpr_ref(&sc->refcount);
  return sc;
}

#ifndef NDEBUG
void grpc_security_connector_unref(grpc_exec_ctx *exec_ctx,
                                   grpc_security_connector *sc,
                                   const char *file, int line,
                                   const char *reason) {
  if (sc == NULL) return;
  if (GRPC_TRACER_ON(grpc_trace_security_connector_refcount)) {
    gpr_atm val = gpr_atm_no_barrier_load(&sc->refcount.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "SECURITY_CONNECTOR:%p unref %" PRIdPTR " -> %" PRIdPTR " %s", sc,
            val, val - 1, reason);
  }
#else
void grpc_security_connector_unref(grpc_exec_ctx *exec_ctx,
                                   grpc_security_connector *sc) {
  if (sc == NULL) return;
#endif
  if (gpr_unref(&sc->refcount)) sc->vtable->destroy(exec_ctx, sc);
}

static void connector_pointer_arg_destroy(grpc_exec_ctx *exec_ctx, void *p) {
  GRPC_SECURITY_CONNECTOR_UNREF(exec_ctx, p, "connector_pointer_arg_destroy");
}

static void *connector_pointer_arg_copy(void *p) {
  return GRPC_SECURITY_CONNECTOR_REF(p, "connector_pointer_arg_copy");
}

static int connector_pointer_cmp(void *a, void *b) { return GPR_ICMP(a, b); }

static const grpc_arg_pointer_vtable connector_pointer_vtable = {
    connector_pointer_arg_copy, connector_pointer_arg_destroy,
    connector_pointer_cmp};

grpc_arg grpc_security_connector_to_arg(grpc_security_connector *sc) {
  return grpc_channel_arg_pointer_create(GRPC_ARG_SECURITY_CONNECTOR, sc,
                                         &connector_pointer_vtable);
}

grpc_security_connector *grpc_security_connector_from_arg(const grpc_arg *arg) {
  if (strcmp(arg->key, GRPC_ARG_SECURITY_CONNECTOR)) return NULL;
  if (arg->type != GRPC_ARG_POINTER) {
    gpr_log(GPR_ERROR, "Invalid type %d for arg %s", arg->type,
            GRPC_ARG_SECURITY_CONNECTOR);
    return NULL;
  }
  return arg->value.pointer.p;
}

grpc_security_connector *grpc_security_connector_find_in_args(
    const grpc_channel_args *args) {
  size_t i;
  if (args == NULL) return NULL;
  for (i = 0; i < args->num_args; i++) {
    grpc_security_connector *sc =
        grpc_security_connector_from_arg(&args->args[i]);
    if (sc != NULL) return sc;
  }
  return NULL;
}

/* -- Fake implementation. -- */

typedef struct {
  grpc_channel_security_connector base;
  char *target;
  char *expected_targets;
  bool is_lb_channel;
} grpc_fake_channel_security_connector;

static void fake_channel_destroy(grpc_exec_ctx *exec_ctx,
                                 grpc_security_connector *sc) {
  grpc_fake_channel_security_connector *c =
      (grpc_fake_channel_security_connector *)sc;
  grpc_call_credentials_unref(exec_ctx, c->base.request_metadata_creds);
  gpr_free(c->target);
  gpr_free(c->expected_targets);
  gpr_free(c);
}

static void fake_server_destroy(grpc_exec_ctx *exec_ctx,
                                grpc_security_connector *sc) {
  gpr_free(sc);
}

static bool fake_check_target(const char *target_type, const char *target,
                              const char *set_str) {
  GPR_ASSERT(target_type != NULL);
  GPR_ASSERT(target != NULL);
  char **set = NULL;
  size_t set_size = 0;
  gpr_string_split(set_str, ",", &set, &set_size);
  bool found = false;
  for (size_t i = 0; i < set_size; ++i) {
    if (set[i] != NULL && strcmp(target, set[i]) == 0) found = true;
  }
  for (size_t i = 0; i < set_size; ++i) {
    gpr_free(set[i]);
  }
  gpr_free(set);
  return found;
}

static void fake_secure_name_check(const char *target,
                                   const char *expected_targets,
                                   bool is_lb_channel) {
  if (expected_targets == NULL) return;
  char **lbs_and_backends = NULL;
  size_t lbs_and_backends_size = 0;
  bool success = false;
  gpr_string_split(expected_targets, ";", &lbs_and_backends,
                   &lbs_and_backends_size);
  if (lbs_and_backends_size > 2 || lbs_and_backends_size == 0) {
    gpr_log(GPR_ERROR, "Invalid expected targets arg value: '%s'",
            expected_targets);
    goto done;
  }
  if (is_lb_channel) {
    if (lbs_and_backends_size != 2) {
      gpr_log(GPR_ERROR,
              "Invalid expected targets arg value: '%s'. Expectations for LB "
              "channels must be of the form 'be1,be2,be3,...;lb1,lb2,...",
              expected_targets);
      goto done;
    }
    if (!fake_check_target("LB", target, lbs_and_backends[1])) {
      gpr_log(GPR_ERROR, "LB target '%s' not found in expected set '%s'",
              target, lbs_and_backends[1]);
      goto done;
    }
    success = true;
  } else {
    if (!fake_check_target("Backend", target, lbs_and_backends[0])) {
      gpr_log(GPR_ERROR, "Backend target '%s' not found in expected set '%s'",
              target, lbs_and_backends[0]);
      goto done;
    }
    success = true;
  }
done:
  for (size_t i = 0; i < lbs_and_backends_size; ++i) {
    gpr_free(lbs_and_backends[i]);
  }
  gpr_free(lbs_and_backends);
  if (!success) abort();
}

static void fake_check_peer(grpc_exec_ctx *exec_ctx,
                            grpc_security_connector *sc, tsi_peer peer,
                            grpc_auth_context **auth_context,
                            grpc_closure *on_peer_checked) {
  const char *prop_name;
  grpc_error *error = GRPC_ERROR_NONE;
  *auth_context = NULL;
  if (peer.property_count != 1) {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Fake peers should only have 1 property.");
    goto end;
  }
  prop_name = peer.properties[0].name;
  if (prop_name == NULL ||
      strcmp(prop_name, TSI_CERTIFICATE_TYPE_PEER_PROPERTY)) {
    char *msg;
    gpr_asprintf(&msg, "Unexpected property in fake peer: %s.",
                 prop_name == NULL ? "<EMPTY>" : prop_name);
    error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    goto end;
  }
  if (strncmp(peer.properties[0].value.data, TSI_FAKE_CERTIFICATE_TYPE,
              peer.properties[0].value.length)) {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Invalid value for cert type property.");
    goto end;
  }
  *auth_context = grpc_auth_context_create(NULL);
  grpc_auth_context_add_cstring_property(
      *auth_context, GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
      GRPC_FAKE_TRANSPORT_SECURITY_TYPE);
end:
  GRPC_CLOSURE_SCHED(exec_ctx, on_peer_checked, error);
  tsi_peer_destruct(&peer);
}

static void fake_channel_check_peer(grpc_exec_ctx *exec_ctx,
                                    grpc_security_connector *sc, tsi_peer peer,
                                    grpc_auth_context **auth_context,
                                    grpc_closure *on_peer_checked) {
  fake_check_peer(exec_ctx, sc, peer, auth_context, on_peer_checked);
  grpc_fake_channel_security_connector *c =
      (grpc_fake_channel_security_connector *)sc;
  fake_secure_name_check(c->target, c->expected_targets, c->is_lb_channel);
}

static void fake_server_check_peer(grpc_exec_ctx *exec_ctx,
                                   grpc_security_connector *sc, tsi_peer peer,
                                   grpc_auth_context **auth_context,
                                   grpc_closure *on_peer_checked) {
  fake_check_peer(exec_ctx, sc, peer, auth_context, on_peer_checked);
}

static bool fake_channel_check_call_host(grpc_exec_ctx *exec_ctx,
                                         grpc_channel_security_connector *sc,
                                         const char *host,
                                         grpc_auth_context *auth_context,
                                         grpc_closure *on_call_host_checked,
                                         grpc_error **error) {
  return true;
}

static void fake_channel_cancel_check_call_host(
    grpc_exec_ctx *exec_ctx, grpc_channel_security_connector *sc,
    grpc_closure *on_call_host_checked, grpc_error *error) {
  GRPC_ERROR_UNREF(error);
}

static void fake_channel_add_handshakers(
    grpc_exec_ctx *exec_ctx, grpc_channel_security_connector *sc,
    grpc_handshake_manager *handshake_mgr) {
  grpc_handshake_manager_add(
      handshake_mgr,
      grpc_security_handshaker_create(
          exec_ctx, tsi_create_fake_handshaker(true /* is_client */),
          &sc->base));
}

static void fake_server_add_handshakers(grpc_exec_ctx *exec_ctx,
                                        grpc_server_security_connector *sc,
                                        grpc_handshake_manager *handshake_mgr) {
  grpc_handshake_manager_add(
      handshake_mgr,
      grpc_security_handshaker_create(
          exec_ctx, tsi_create_fake_handshaker(false /* is_client */),
          &sc->base));
}

static grpc_security_connector_vtable fake_channel_vtable = {
    fake_channel_destroy, fake_channel_check_peer};

static grpc_security_connector_vtable fake_server_vtable = {
    fake_server_destroy, fake_server_check_peer};

grpc_channel_security_connector *grpc_fake_channel_security_connector_create(
    grpc_call_credentials *request_metadata_creds, const char *target,
    const grpc_channel_args *args) {
  grpc_fake_channel_security_connector *c = gpr_zalloc(sizeof(*c));
  gpr_ref_init(&c->base.base.refcount, 1);
  c->base.base.url_scheme = GRPC_FAKE_SECURITY_URL_SCHEME;
  c->base.base.vtable = &fake_channel_vtable;
  c->base.request_metadata_creds =
      grpc_call_credentials_ref(request_metadata_creds);
  c->base.check_call_host = fake_channel_check_call_host;
  c->base.cancel_check_call_host = fake_channel_cancel_check_call_host;
  c->base.add_handshakers = fake_channel_add_handshakers;
  c->target = gpr_strdup(target);
  const char *expected_targets = grpc_fake_transport_get_expected_targets(args);
  c->expected_targets = gpr_strdup(expected_targets);
  c->is_lb_channel = (grpc_lb_targets_info_find_in_args(args) != NULL);
  return &c->base;
}

grpc_server_security_connector *grpc_fake_server_security_connector_create(
    void) {
  grpc_server_security_connector *c =
      gpr_zalloc(sizeof(grpc_server_security_connector));
  gpr_ref_init(&c->base.refcount, 1);
  c->base.vtable = &fake_server_vtable;
  c->base.url_scheme = GRPC_FAKE_SECURITY_URL_SCHEME;
  c->add_handshakers = fake_server_add_handshakers;
  return c;
}

/* --- Ssl implementation. --- */

typedef struct {
  grpc_channel_security_connector base;
  tsi_ssl_client_handshaker_factory *handshaker_factory;
  char *target_name;
  char *overridden_target_name;
} grpc_ssl_channel_security_connector;

typedef struct {
  grpc_server_security_connector base;
  tsi_ssl_server_handshaker_factory *handshaker_factory;
} grpc_ssl_server_security_connector;

static void ssl_channel_destroy(grpc_exec_ctx *exec_ctx,
                                grpc_security_connector *sc) {
  grpc_ssl_channel_security_connector *c =
      (grpc_ssl_channel_security_connector *)sc;
  grpc_call_credentials_unref(exec_ctx, c->base.request_metadata_creds);
  if (c->handshaker_factory != NULL) {
    tsi_ssl_client_handshaker_factory_destroy(c->handshaker_factory);
  }
  if (c->target_name != NULL) gpr_free(c->target_name);
  if (c->overridden_target_name != NULL) gpr_free(c->overridden_target_name);
  gpr_free(sc);
}

static void ssl_server_destroy(grpc_exec_ctx *exec_ctx,
                               grpc_security_connector *sc) {
  grpc_ssl_server_security_connector *c =
      (grpc_ssl_server_security_connector *)sc;
  if (c->handshaker_factory != NULL) {
    tsi_ssl_server_handshaker_factory_destroy(c->handshaker_factory);
  }
  gpr_free(sc);
}

static void ssl_channel_add_handshakers(grpc_exec_ctx *exec_ctx,
                                        grpc_channel_security_connector *sc,
                                        grpc_handshake_manager *handshake_mgr) {
  grpc_ssl_channel_security_connector *c =
      (grpc_ssl_channel_security_connector *)sc;
  // Instantiate TSI handshaker.
  tsi_handshaker *tsi_hs = NULL;
  tsi_result result = tsi_ssl_client_handshaker_factory_create_handshaker(
      c->handshaker_factory,
      c->overridden_target_name != NULL ? c->overridden_target_name
                                        : c->target_name,
      &tsi_hs);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
            tsi_result_to_string(result));
    return;
  }

  // Create handshakers.
  grpc_handshake_manager_add(
      handshake_mgr,
      grpc_security_handshaker_create(
          exec_ctx, tsi_create_adapter_handshaker(tsi_hs), &sc->base));
}

static void ssl_server_add_handshakers(grpc_exec_ctx *exec_ctx,
                                       grpc_server_security_connector *sc,
                                       grpc_handshake_manager *handshake_mgr) {
  grpc_ssl_server_security_connector *c =
      (grpc_ssl_server_security_connector *)sc;
  // Instantiate TSI handshaker.
  tsi_handshaker *tsi_hs = NULL;
  tsi_result result = tsi_ssl_server_handshaker_factory_create_handshaker(
      c->handshaker_factory, &tsi_hs);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
            tsi_result_to_string(result));
    return;
  }

  // Create handshakers.
  grpc_handshake_manager_add(
      handshake_mgr,
      grpc_security_handshaker_create(
          exec_ctx, tsi_create_adapter_handshaker(tsi_hs), &sc->base));
}

static int ssl_host_matches_name(const tsi_peer *peer, const char *peer_name) {
  char *allocated_name = NULL;
  int r;

  if (strchr(peer_name, ':') != NULL) {
    char *ignored_port;
    gpr_split_host_port(peer_name, &allocated_name, &ignored_port);
    gpr_free(ignored_port);
    peer_name = allocated_name;
    if (!peer_name) return 0;
  }
  r = tsi_ssl_peer_matches_name(peer, peer_name);
  gpr_free(allocated_name);
  return r;
}

grpc_auth_context *tsi_ssl_peer_to_auth_context(const tsi_peer *peer) {
  size_t i;
  grpc_auth_context *ctx = NULL;
  const char *peer_identity_property_name = NULL;

  /* The caller has checked the certificate type property. */
  GPR_ASSERT(peer->property_count >= 1);
  ctx = grpc_auth_context_create(NULL);
  grpc_auth_context_add_cstring_property(
      ctx, GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
      GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  for (i = 0; i < peer->property_count; i++) {
    const tsi_peer_property *prop = &peer->properties[i];
    if (prop->name == NULL) continue;
    if (strcmp(prop->name, TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY) == 0) {
      /* If there is no subject alt name, have the CN as the identity. */
      if (peer_identity_property_name == NULL) {
        peer_identity_property_name = GRPC_X509_CN_PROPERTY_NAME;
      }
      grpc_auth_context_add_property(ctx, GRPC_X509_CN_PROPERTY_NAME,
                                     prop->value.data, prop->value.length);
    } else if (strcmp(prop->name,
                      TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY) == 0) {
      peer_identity_property_name = GRPC_X509_SAN_PROPERTY_NAME;
      grpc_auth_context_add_property(ctx, GRPC_X509_SAN_PROPERTY_NAME,
                                     prop->value.data, prop->value.length);
    } else if (strcmp(prop->name, TSI_X509_PEM_CERT_PROPERTY) == 0) {
      grpc_auth_context_add_property(ctx, GRPC_X509_PEM_CERT_PROPERTY_NAME,
                                     prop->value.data, prop->value.length);
    }
  }
  if (peer_identity_property_name != NULL) {
    GPR_ASSERT(grpc_auth_context_set_peer_identity_property_name(
                   ctx, peer_identity_property_name) == 1);
  }
  return ctx;
}

static grpc_error *ssl_check_peer(grpc_security_connector *sc,
                                  const char *peer_name, const tsi_peer *peer,
                                  grpc_auth_context **auth_context) {
  /* Check the ALPN. */
  const tsi_peer_property *p =
      tsi_peer_get_property_by_name(peer, TSI_SSL_ALPN_SELECTED_PROTOCOL);
  if (p == NULL) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Cannot check peer: missing selected ALPN property.");
  }
  if (!grpc_chttp2_is_alpn_version_supported(p->value.data, p->value.length)) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Cannot check peer: invalid ALPN value.");
  }

  /* Check the peer name if specified. */
  if (peer_name != NULL && !ssl_host_matches_name(peer, peer_name)) {
    char *msg;
    gpr_asprintf(&msg, "Peer name %s is not in peer certificate", peer_name);
    grpc_error *error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    return error;
  }
  *auth_context = tsi_ssl_peer_to_auth_context(peer);
  return GRPC_ERROR_NONE;
}

static void ssl_channel_check_peer(grpc_exec_ctx *exec_ctx,
                                   grpc_security_connector *sc, tsi_peer peer,
                                   grpc_auth_context **auth_context,
                                   grpc_closure *on_peer_checked) {
  grpc_ssl_channel_security_connector *c =
      (grpc_ssl_channel_security_connector *)sc;
  grpc_error *error = ssl_check_peer(sc, c->overridden_target_name != NULL
                                             ? c->overridden_target_name
                                             : c->target_name,
                                     &peer, auth_context);
  GRPC_CLOSURE_SCHED(exec_ctx, on_peer_checked, error);
  tsi_peer_destruct(&peer);
}

static void ssl_server_check_peer(grpc_exec_ctx *exec_ctx,
                                  grpc_security_connector *sc, tsi_peer peer,
                                  grpc_auth_context **auth_context,
                                  grpc_closure *on_peer_checked) {
  grpc_error *error = ssl_check_peer(sc, NULL, &peer, auth_context);
  tsi_peer_destruct(&peer);
  GRPC_CLOSURE_SCHED(exec_ctx, on_peer_checked, error);
}

static void add_shallow_auth_property_to_peer(tsi_peer *peer,
                                              const grpc_auth_property *prop,
                                              const char *tsi_prop_name) {
  tsi_peer_property *tsi_prop = &peer->properties[peer->property_count++];
  tsi_prop->name = (char *)tsi_prop_name;
  tsi_prop->value.data = prop->value;
  tsi_prop->value.length = prop->value_length;
}

tsi_peer tsi_shallow_peer_from_ssl_auth_context(
    const grpc_auth_context *auth_context) {
  size_t max_num_props = 0;
  grpc_auth_property_iterator it;
  const grpc_auth_property *prop;
  tsi_peer peer;
  memset(&peer, 0, sizeof(peer));

  it = grpc_auth_context_property_iterator(auth_context);
  while (grpc_auth_property_iterator_next(&it) != NULL) max_num_props++;

  if (max_num_props > 0) {
    peer.properties = gpr_malloc(max_num_props * sizeof(tsi_peer_property));
    it = grpc_auth_context_property_iterator(auth_context);
    while ((prop = grpc_auth_property_iterator_next(&it)) != NULL) {
      if (strcmp(prop->name, GRPC_X509_SAN_PROPERTY_NAME) == 0) {
        add_shallow_auth_property_to_peer(
            &peer, prop, TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY);
      } else if (strcmp(prop->name, GRPC_X509_CN_PROPERTY_NAME) == 0) {
        add_shallow_auth_property_to_peer(
            &peer, prop, TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY);
      } else if (strcmp(prop->name, GRPC_X509_PEM_CERT_PROPERTY_NAME) == 0) {
        add_shallow_auth_property_to_peer(&peer, prop,
                                          TSI_X509_PEM_CERT_PROPERTY);
      }
    }
  }
  return peer;
}

void tsi_shallow_peer_destruct(tsi_peer *peer) {
  if (peer->properties != NULL) gpr_free(peer->properties);
}

static bool ssl_channel_check_call_host(grpc_exec_ctx *exec_ctx,
                                        grpc_channel_security_connector *sc,
                                        const char *host,
                                        grpc_auth_context *auth_context,
                                        grpc_closure *on_call_host_checked,
                                        grpc_error **error) {
  grpc_ssl_channel_security_connector *c =
      (grpc_ssl_channel_security_connector *)sc;
  grpc_security_status status = GRPC_SECURITY_ERROR;
  tsi_peer peer = tsi_shallow_peer_from_ssl_auth_context(auth_context);
  if (ssl_host_matches_name(&peer, host)) status = GRPC_SECURITY_OK;
  /* If the target name was overridden, then the original target_name was
     'checked' transitively during the previous peer check at the end of the
     handshake. */
  if (c->overridden_target_name != NULL && strcmp(host, c->target_name) == 0) {
    status = GRPC_SECURITY_OK;
  }
  if (status != GRPC_SECURITY_OK) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "call host does not match SSL server name");
  }
  tsi_shallow_peer_destruct(&peer);
  return true;
}

static void ssl_channel_cancel_check_call_host(
    grpc_exec_ctx *exec_ctx, grpc_channel_security_connector *sc,
    grpc_closure *on_call_host_checked, grpc_error *error) {
  GRPC_ERROR_UNREF(error);
}

static grpc_security_connector_vtable ssl_channel_vtable = {
    ssl_channel_destroy, ssl_channel_check_peer};

static grpc_security_connector_vtable ssl_server_vtable = {
    ssl_server_destroy, ssl_server_check_peer};

/* returns a NULL terminated slice. */
static grpc_slice compute_default_pem_root_certs_once(void) {
  grpc_slice result = grpc_empty_slice();

  /* First try to load the roots from the environment. */
  char *default_root_certs_path =
      gpr_getenv(GRPC_DEFAULT_SSL_ROOTS_FILE_PATH_ENV_VAR);
  if (default_root_certs_path != NULL) {
    GRPC_LOG_IF_ERROR("load_file",
                      grpc_load_file(default_root_certs_path, 1, &result));
    gpr_free(default_root_certs_path);
  }

  /* Try overridden roots if needed. */
  grpc_ssl_roots_override_result ovrd_res = GRPC_SSL_ROOTS_OVERRIDE_FAIL;
  if (GRPC_SLICE_IS_EMPTY(result) && ssl_roots_override_cb != NULL) {
    char *pem_root_certs = NULL;
    ovrd_res = ssl_roots_override_cb(&pem_root_certs);
    if (ovrd_res == GRPC_SSL_ROOTS_OVERRIDE_OK) {
      GPR_ASSERT(pem_root_certs != NULL);
      result = grpc_slice_from_copied_buffer(
          pem_root_certs,
          strlen(pem_root_certs) + 1);  // NULL terminator.
    }
    gpr_free(pem_root_certs);
  }

  /* Fall back to installed certs if needed. */
  if (GRPC_SLICE_IS_EMPTY(result) &&
      ovrd_res != GRPC_SSL_ROOTS_OVERRIDE_FAIL_PERMANENTLY) {
    GRPC_LOG_IF_ERROR("load_file",
                      grpc_load_file(installed_roots_path, 1, &result));
  }
  return result;
}

static grpc_slice default_pem_root_certs;

static void init_default_pem_root_certs(void) {
  default_pem_root_certs = compute_default_pem_root_certs_once();
}

grpc_slice grpc_get_default_ssl_roots_for_testing(void) {
  return compute_default_pem_root_certs_once();
}

static tsi_client_certificate_request_type
get_tsi_client_certificate_request_type(
    grpc_ssl_client_certificate_request_type grpc_request_type) {
  switch (grpc_request_type) {
    case GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE:
      return TSI_DONT_REQUEST_CLIENT_CERTIFICATE;

    case GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_BUT_DONT_VERIFY:
      return TSI_REQUEST_CLIENT_CERTIFICATE_BUT_DONT_VERIFY;

    case GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY:
      return TSI_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY;

    case GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY:
      return TSI_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY;

    case GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY:
      return TSI_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;

    default:
      // Is this a sane default
      return TSI_DONT_REQUEST_CLIENT_CERTIFICATE;
  }
}

const char *grpc_get_default_ssl_roots(void) {
  /* TODO(jboeuf@google.com): Maybe revisit the approach which consists in
     loading all the roots once for the lifetime of the process. */
  static gpr_once once = GPR_ONCE_INIT;
  gpr_once_init(&once, init_default_pem_root_certs);
  return GRPC_SLICE_IS_EMPTY(default_pem_root_certs)
             ? NULL
             : (const char *)GRPC_SLICE_START_PTR(default_pem_root_certs);
}

grpc_security_status grpc_ssl_channel_security_connector_create(
    grpc_exec_ctx *exec_ctx, grpc_call_credentials *request_metadata_creds,
    const grpc_ssl_config *config, const char *target_name,
    const char *overridden_target_name, grpc_channel_security_connector **sc) {
  size_t num_alpn_protocols = grpc_chttp2_num_alpn_versions();
  const char **alpn_protocol_strings =
      gpr_malloc(sizeof(const char *) * num_alpn_protocols);
  tsi_result result = TSI_OK;
  grpc_ssl_channel_security_connector *c;
  size_t i;
  const char *pem_root_certs;
  char *port;

  for (i = 0; i < num_alpn_protocols; i++) {
    alpn_protocol_strings[i] = grpc_chttp2_get_alpn_version_index(i);
  }

  if (config == NULL || target_name == NULL) {
    gpr_log(GPR_ERROR, "An ssl channel needs a config and a target name.");
    goto error;
  }
  if (config->pem_root_certs == NULL) {
    pem_root_certs = grpc_get_default_ssl_roots();
    if (pem_root_certs == NULL) {
      gpr_log(GPR_ERROR, "Could not get default pem root certs.");
      goto error;
    }
  } else {
    pem_root_certs = config->pem_root_certs;
  }

  c = gpr_zalloc(sizeof(grpc_ssl_channel_security_connector));

  gpr_ref_init(&c->base.base.refcount, 1);
  c->base.base.vtable = &ssl_channel_vtable;
  c->base.base.url_scheme = GRPC_SSL_URL_SCHEME;
  c->base.request_metadata_creds =
      grpc_call_credentials_ref(request_metadata_creds);
  c->base.check_call_host = ssl_channel_check_call_host;
  c->base.cancel_check_call_host = ssl_channel_cancel_check_call_host;
  c->base.add_handshakers = ssl_channel_add_handshakers;
  gpr_split_host_port(target_name, &c->target_name, &port);
  gpr_free(port);
  if (overridden_target_name != NULL) {
    c->overridden_target_name = gpr_strdup(overridden_target_name);
  }

  bool has_key_cert_pair = config->pem_key_cert_pair.private_key != NULL &&
                           config->pem_key_cert_pair.cert_chain != NULL;
  result = tsi_create_ssl_client_handshaker_factory(
      has_key_cert_pair ? &config->pem_key_cert_pair : NULL, pem_root_certs,
      ssl_cipher_suites(), alpn_protocol_strings, (uint16_t)num_alpn_protocols,
      &c->handshaker_factory);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
            tsi_result_to_string(result));
    ssl_channel_destroy(exec_ctx, &c->base.base);
    *sc = NULL;
    goto error;
  }
  *sc = &c->base;
  gpr_free((void *)alpn_protocol_strings);
  return GRPC_SECURITY_OK;

error:
  gpr_free((void *)alpn_protocol_strings);
  return GRPC_SECURITY_ERROR;
}

grpc_security_status grpc_ssl_server_security_connector_create(
    grpc_exec_ctx *exec_ctx, const grpc_ssl_server_config *config,
    grpc_server_security_connector **sc) {
  size_t num_alpn_protocols = grpc_chttp2_num_alpn_versions();
  const char **alpn_protocol_strings =
      gpr_malloc(sizeof(const char *) * num_alpn_protocols);
  tsi_result result = TSI_OK;
  grpc_ssl_server_security_connector *c;
  size_t i;

  for (i = 0; i < num_alpn_protocols; i++) {
    alpn_protocol_strings[i] = grpc_chttp2_get_alpn_version_index(i);
  }

  if (config == NULL || config->num_key_cert_pairs == 0) {
    gpr_log(GPR_ERROR, "An SSL server needs a key and a cert.");
    goto error;
  }
  c = gpr_zalloc(sizeof(grpc_ssl_server_security_connector));

  gpr_ref_init(&c->base.base.refcount, 1);
  c->base.base.url_scheme = GRPC_SSL_URL_SCHEME;
  c->base.base.vtable = &ssl_server_vtable;
  result = tsi_create_ssl_server_handshaker_factory_ex(
      config->pem_key_cert_pairs, config->num_key_cert_pairs,
      config->pem_root_certs, get_tsi_client_certificate_request_type(
                                  config->client_certificate_request),
      ssl_cipher_suites(), alpn_protocol_strings, (uint16_t)num_alpn_protocols,
      &c->handshaker_factory);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
            tsi_result_to_string(result));
    ssl_server_destroy(exec_ctx, &c->base.base);
    *sc = NULL;
    goto error;
  }
  c->base.add_handshakers = ssl_server_add_handshakers;
  *sc = &c->base;
  gpr_free((void *)alpn_protocol_strings);
  return GRPC_SECURITY_OK;

error:
  gpr_free((void *)alpn_protocol_strings);
  return GRPC_SECURITY_ERROR;
}
