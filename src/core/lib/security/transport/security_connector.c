/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/core/lib/security/transport/security_connector.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/slice_buffer.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/alpn/alpn.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/transport/handshake.h"
#include "src/core/lib/security/transport/secure_endpoint.h"
#include "src/core/lib/support/env.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/tsi/fake_transport_security.h"
#include "src/core/lib/tsi/ssl_transport_security.h"

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
#define GRPC_SSL_CIPHER_SUITES                                            \
  "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-" \
  "SHA384:ECDHE-RSA-AES256-GCM-SHA384"

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

void grpc_server_security_connector_shutdown(
    grpc_exec_ctx *exec_ctx, grpc_server_security_connector *connector) {
  grpc_security_connector_handshake_list *tmp;
  gpr_mu_lock(&connector->mu);
  while (connector->handshaking_handshakes) {
    tmp = connector->handshaking_handshakes;
    grpc_security_handshake_shutdown(
        exec_ctx, connector->handshaking_handshakes->handshake);
    connector->handshaking_handshakes = tmp->next;
    gpr_free(tmp);
  }
  gpr_mu_unlock(&connector->mu);
}

void grpc_channel_security_connector_do_handshake(
    grpc_exec_ctx *exec_ctx, grpc_channel_security_connector *sc,
    grpc_endpoint *nonsecure_endpoint, gpr_timespec deadline,
    grpc_security_handshake_done_cb cb, void *user_data) {
  if (sc == NULL || nonsecure_endpoint == NULL) {
    cb(exec_ctx, user_data, GRPC_SECURITY_ERROR, NULL, NULL);
  } else {
    sc->do_handshake(exec_ctx, sc, nonsecure_endpoint, deadline, cb, user_data);
  }
}

void grpc_server_security_connector_do_handshake(
    grpc_exec_ctx *exec_ctx, grpc_server_security_connector *sc,
    grpc_tcp_server_acceptor *acceptor, grpc_endpoint *nonsecure_endpoint,
    gpr_timespec deadline, grpc_security_handshake_done_cb cb,
    void *user_data) {
  if (sc == NULL || nonsecure_endpoint == NULL) {
    cb(exec_ctx, user_data, GRPC_SECURITY_ERROR, NULL, NULL);
  } else {
    sc->do_handshake(exec_ctx, sc, acceptor, nonsecure_endpoint, deadline, cb,
                     user_data);
  }
}

void grpc_security_connector_check_peer(grpc_exec_ctx *exec_ctx,
                                        grpc_security_connector *sc,
                                        tsi_peer peer,
                                        grpc_security_peer_check_cb cb,
                                        void *user_data) {
  if (sc == NULL) {
    cb(exec_ctx, user_data, GRPC_SECURITY_ERROR, NULL);
    tsi_peer_destruct(&peer);
  } else {
    sc->vtable->check_peer(exec_ctx, sc, peer, cb, user_data);
  }
}

void grpc_channel_security_connector_check_call_host(
    grpc_exec_ctx *exec_ctx, grpc_channel_security_connector *sc,
    const char *host, grpc_auth_context *auth_context,
    grpc_security_call_host_check_cb cb, void *user_data) {
  if (sc == NULL || sc->check_call_host == NULL) {
    cb(exec_ctx, user_data, GRPC_SECURITY_ERROR);
  } else {
    sc->check_call_host(exec_ctx, sc, host, auth_context, cb, user_data);
  }
}

#ifdef GRPC_SECURITY_CONNECTOR_REFCOUNT_DEBUG
grpc_security_connector *grpc_security_connector_ref(
    grpc_security_connector *sc, const char *file, int line,
    const char *reason) {
  if (sc == NULL) return NULL;
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
          "SECURITY_CONNECTOR:%p   ref %d -> %d %s", sc,
          (int)sc->refcount.count, (int)sc->refcount.count + 1, reason);
#else
grpc_security_connector *grpc_security_connector_ref(
    grpc_security_connector *sc) {
  if (sc == NULL) return NULL;
#endif
  gpr_ref(&sc->refcount);
  return sc;
}

#ifdef GRPC_SECURITY_CONNECTOR_REFCOUNT_DEBUG
void grpc_security_connector_unref(grpc_security_connector *sc,
                                   const char *file, int line,
                                   const char *reason) {
  if (sc == NULL) return;
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
          "SECURITY_CONNECTOR:%p unref %d -> %d %s", sc,
          (int)sc->refcount.count, (int)sc->refcount.count - 1, reason);
#else
void grpc_security_connector_unref(grpc_security_connector *sc) {
  if (sc == NULL) return;
#endif
  if (gpr_unref(&sc->refcount)) sc->vtable->destroy(sc);
}

static void connector_pointer_arg_destroy(void *p) {
  GRPC_SECURITY_CONNECTOR_UNREF(p, "connector_pointer_arg");
}

static void *connector_pointer_arg_copy(void *p) {
  return GRPC_SECURITY_CONNECTOR_REF(p, "connector_pointer_arg");
}

static int connector_pointer_cmp(void *a, void *b) { return GPR_ICMP(a, b); }

static const grpc_arg_pointer_vtable connector_pointer_vtable = {
    connector_pointer_arg_copy, connector_pointer_arg_destroy,
    connector_pointer_cmp};

grpc_arg grpc_security_connector_to_arg(grpc_security_connector *sc) {
  grpc_arg result;
  result.type = GRPC_ARG_POINTER;
  result.key = GRPC_SECURITY_CONNECTOR_ARG;
  result.value.pointer.vtable = &connector_pointer_vtable;
  result.value.pointer.p = sc;
  return result;
}

grpc_security_connector *grpc_security_connector_from_arg(const grpc_arg *arg) {
  if (strcmp(arg->key, GRPC_SECURITY_CONNECTOR_ARG)) return NULL;
  if (arg->type != GRPC_ARG_POINTER) {
    gpr_log(GPR_ERROR, "Invalid type %d for arg %s", arg->type,
            GRPC_SECURITY_CONNECTOR_ARG);
    return NULL;
  }
  return arg->value.pointer.p;
}

grpc_security_connector *grpc_find_security_connector_in_args(
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

static void fake_channel_destroy(grpc_security_connector *sc) {
  grpc_channel_security_connector *c = (grpc_channel_security_connector *)sc;
  grpc_call_credentials_unref(c->request_metadata_creds);
  gpr_free(sc);
}

static void fake_server_destroy(grpc_security_connector *sc) {
  grpc_server_security_connector *c = (grpc_server_security_connector *)sc;
  gpr_mu_destroy(&c->mu);
  gpr_free(sc);
}

static void fake_check_peer(grpc_exec_ctx *exec_ctx,
                            grpc_security_connector *sc, tsi_peer peer,
                            grpc_security_peer_check_cb cb, void *user_data) {
  const char *prop_name;
  grpc_security_status status = GRPC_SECURITY_OK;
  grpc_auth_context *auth_context = NULL;
  if (peer.property_count != 1) {
    gpr_log(GPR_ERROR, "Fake peers should only have 1 property.");
    status = GRPC_SECURITY_ERROR;
    goto end;
  }
  prop_name = peer.properties[0].name;
  if (prop_name == NULL ||
      strcmp(prop_name, TSI_CERTIFICATE_TYPE_PEER_PROPERTY)) {
    gpr_log(GPR_ERROR, "Unexpected property in fake peer: %s.",
            prop_name == NULL ? "<EMPTY>" : prop_name);
    status = GRPC_SECURITY_ERROR;
    goto end;
  }
  if (strncmp(peer.properties[0].value.data, TSI_FAKE_CERTIFICATE_TYPE,
              peer.properties[0].value.length)) {
    gpr_log(GPR_ERROR, "Invalid value for cert type property.");
    status = GRPC_SECURITY_ERROR;
    goto end;
  }
  auth_context = grpc_auth_context_create(NULL);
  grpc_auth_context_add_cstring_property(
      auth_context, GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
      GRPC_FAKE_TRANSPORT_SECURITY_TYPE);

end:
  cb(exec_ctx, user_data, status, auth_context);
  grpc_auth_context_unref(auth_context);
  tsi_peer_destruct(&peer);
}

static void fake_channel_check_call_host(grpc_exec_ctx *exec_ctx,
                                         grpc_channel_security_connector *sc,
                                         const char *host,
                                         grpc_auth_context *auth_context,
                                         grpc_security_call_host_check_cb cb,
                                         void *user_data) {
  cb(exec_ctx, user_data, GRPC_SECURITY_OK);
}

static void fake_channel_do_handshake(grpc_exec_ctx *exec_ctx,
                                      grpc_channel_security_connector *sc,
                                      grpc_endpoint *nonsecure_endpoint,
                                      gpr_timespec deadline,
                                      grpc_security_handshake_done_cb cb,
                                      void *user_data) {
  grpc_do_security_handshake(exec_ctx, tsi_create_fake_handshaker(1), &sc->base,
                             true, nonsecure_endpoint, deadline, cb, user_data);
}

static void fake_server_do_handshake(grpc_exec_ctx *exec_ctx,
                                     grpc_server_security_connector *sc,
                                     grpc_tcp_server_acceptor *acceptor,
                                     grpc_endpoint *nonsecure_endpoint,
                                     gpr_timespec deadline,
                                     grpc_security_handshake_done_cb cb,
                                     void *user_data) {
  grpc_do_security_handshake(exec_ctx, tsi_create_fake_handshaker(0), &sc->base,
                             false, nonsecure_endpoint, deadline, cb,
                             user_data);
}

static grpc_security_connector_vtable fake_channel_vtable = {
    fake_channel_destroy, fake_check_peer};

static grpc_security_connector_vtable fake_server_vtable = {fake_server_destroy,
                                                            fake_check_peer};

grpc_channel_security_connector *grpc_fake_channel_security_connector_create(
    grpc_call_credentials *request_metadata_creds) {
  grpc_channel_security_connector *c = gpr_malloc(sizeof(*c));
  memset(c, 0, sizeof(*c));
  gpr_ref_init(&c->base.refcount, 1);
  c->base.url_scheme = GRPC_FAKE_SECURITY_URL_SCHEME;
  c->base.vtable = &fake_channel_vtable;
  c->request_metadata_creds = grpc_call_credentials_ref(request_metadata_creds);
  c->check_call_host = fake_channel_check_call_host;
  c->do_handshake = fake_channel_do_handshake;
  return c;
}

grpc_server_security_connector *grpc_fake_server_security_connector_create(
    void) {
  grpc_server_security_connector *c =
      gpr_malloc(sizeof(grpc_server_security_connector));
  memset(c, 0, sizeof(*c));
  gpr_ref_init(&c->base.refcount, 1);
  c->base.vtable = &fake_server_vtable;
  c->base.url_scheme = GRPC_FAKE_SECURITY_URL_SCHEME;
  c->do_handshake = fake_server_do_handshake;
  gpr_mu_init(&c->mu);
  return c;
}

/* --- Ssl implementation. --- */

typedef struct {
  grpc_channel_security_connector base;
  tsi_ssl_handshaker_factory *handshaker_factory;
  char *target_name;
  char *overridden_target_name;
} grpc_ssl_channel_security_connector;

typedef struct {
  grpc_server_security_connector base;
  tsi_ssl_handshaker_factory *handshaker_factory;
} grpc_ssl_server_security_connector;

static void ssl_channel_destroy(grpc_security_connector *sc) {
  grpc_ssl_channel_security_connector *c =
      (grpc_ssl_channel_security_connector *)sc;
  grpc_call_credentials_unref(c->base.request_metadata_creds);
  if (c->handshaker_factory != NULL) {
    tsi_ssl_handshaker_factory_destroy(c->handshaker_factory);
  }
  if (c->target_name != NULL) gpr_free(c->target_name);
  if (c->overridden_target_name != NULL) gpr_free(c->overridden_target_name);
  gpr_free(sc);
}

static void ssl_server_destroy(grpc_security_connector *sc) {
  grpc_ssl_server_security_connector *c =
      (grpc_ssl_server_security_connector *)sc;

  if (c->handshaker_factory != NULL) {
    tsi_ssl_handshaker_factory_destroy(c->handshaker_factory);
  }
  gpr_mu_destroy(&c->base.mu);
  gpr_free(sc);
}

static grpc_security_status ssl_create_handshaker(
    tsi_ssl_handshaker_factory *handshaker_factory, bool is_client,
    const char *peer_name, tsi_handshaker **handshaker) {
  tsi_result result = TSI_OK;
  if (handshaker_factory == NULL) return GRPC_SECURITY_ERROR;
  result = tsi_ssl_handshaker_factory_create_handshaker(
      handshaker_factory, is_client ? peer_name : NULL, handshaker);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
            tsi_result_to_string(result));
    return GRPC_SECURITY_ERROR;
  }
  return GRPC_SECURITY_OK;
}

static void ssl_channel_do_handshake(grpc_exec_ctx *exec_ctx,
                                     grpc_channel_security_connector *sc,
                                     grpc_endpoint *nonsecure_endpoint,
                                     gpr_timespec deadline,
                                     grpc_security_handshake_done_cb cb,
                                     void *user_data) {
  grpc_ssl_channel_security_connector *c =
      (grpc_ssl_channel_security_connector *)sc;
  tsi_handshaker *handshaker;
  grpc_security_status status = ssl_create_handshaker(
      c->handshaker_factory, true,
      c->overridden_target_name != NULL ? c->overridden_target_name
                                        : c->target_name,
      &handshaker);
  if (status != GRPC_SECURITY_OK) {
    cb(exec_ctx, user_data, status, NULL, NULL);
  } else {
    grpc_do_security_handshake(exec_ctx, handshaker, &sc->base, true,
                               nonsecure_endpoint, deadline, cb, user_data);
  }
}

static void ssl_server_do_handshake(grpc_exec_ctx *exec_ctx,
                                    grpc_server_security_connector *sc,
                                    grpc_tcp_server_acceptor *acceptor,
                                    grpc_endpoint *nonsecure_endpoint,
                                    gpr_timespec deadline,
                                    grpc_security_handshake_done_cb cb,
                                    void *user_data) {
  grpc_ssl_server_security_connector *c =
      (grpc_ssl_server_security_connector *)sc;
  tsi_handshaker *handshaker;
  grpc_security_status status =
      ssl_create_handshaker(c->handshaker_factory, false, NULL, &handshaker);
  if (status != GRPC_SECURITY_OK) {
    cb(exec_ctx, user_data, status, NULL, NULL);
  } else {
    grpc_do_security_handshake(exec_ctx, handshaker, &sc->base, false,
                               nonsecure_endpoint, deadline, cb, user_data);
  }
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

static grpc_security_status ssl_check_peer(grpc_security_connector *sc,
                                           const char *peer_name,
                                           const tsi_peer *peer,
                                           grpc_auth_context **auth_context) {
  /* Check the ALPN. */
  const tsi_peer_property *p =
      tsi_peer_get_property_by_name(peer, TSI_SSL_ALPN_SELECTED_PROTOCOL);
  if (p == NULL) {
    gpr_log(GPR_ERROR, "Missing selected ALPN property.");
    return GRPC_SECURITY_ERROR;
  }
  if (!grpc_chttp2_is_alpn_version_supported(p->value.data, p->value.length)) {
    gpr_log(GPR_ERROR, "Invalid ALPN value.");
    return GRPC_SECURITY_ERROR;
  }

  /* Check the peer name if specified. */
  if (peer_name != NULL && !ssl_host_matches_name(peer, peer_name)) {
    gpr_log(GPR_ERROR, "Peer name %s is not in peer certificate", peer_name);
    return GRPC_SECURITY_ERROR;
  }
  *auth_context = tsi_ssl_peer_to_auth_context(peer);
  return GRPC_SECURITY_OK;
}

static void ssl_channel_check_peer(grpc_exec_ctx *exec_ctx,
                                   grpc_security_connector *sc, tsi_peer peer,
                                   grpc_security_peer_check_cb cb,
                                   void *user_data) {
  grpc_ssl_channel_security_connector *c =
      (grpc_ssl_channel_security_connector *)sc;
  grpc_security_status status;
  grpc_auth_context *auth_context = NULL;
  status = ssl_check_peer(sc, c->overridden_target_name != NULL
                                  ? c->overridden_target_name
                                  : c->target_name,
                          &peer, &auth_context);
  cb(exec_ctx, user_data, status, auth_context);
  grpc_auth_context_unref(auth_context);
  tsi_peer_destruct(&peer);
}

static void ssl_server_check_peer(grpc_exec_ctx *exec_ctx,
                                  grpc_security_connector *sc, tsi_peer peer,
                                  grpc_security_peer_check_cb cb,
                                  void *user_data) {
  grpc_auth_context *auth_context = NULL;
  grpc_security_status status = ssl_check_peer(sc, NULL, &peer, &auth_context);
  tsi_peer_destruct(&peer);
  cb(exec_ctx, user_data, status, auth_context);
  grpc_auth_context_unref(auth_context);
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

static void ssl_channel_check_call_host(grpc_exec_ctx *exec_ctx,
                                        grpc_channel_security_connector *sc,
                                        const char *host,
                                        grpc_auth_context *auth_context,
                                        grpc_security_call_host_check_cb cb,
                                        void *user_data) {
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
  cb(exec_ctx, user_data, status);
  tsi_shallow_peer_destruct(&peer);
}

static grpc_security_connector_vtable ssl_channel_vtable = {
    ssl_channel_destroy, ssl_channel_check_peer};

static grpc_security_connector_vtable ssl_server_vtable = {
    ssl_server_destroy, ssl_server_check_peer};

static gpr_slice compute_default_pem_root_certs_once(void) {
  gpr_slice result = gpr_empty_slice();

  /* First try to load the roots from the environment. */
  char *default_root_certs_path =
      gpr_getenv(GRPC_DEFAULT_SSL_ROOTS_FILE_PATH_ENV_VAR);
  if (default_root_certs_path != NULL) {
    GRPC_LOG_IF_ERROR("load_file",
                      grpc_load_file(default_root_certs_path, 0, &result));
    gpr_free(default_root_certs_path);
  }

  /* Try overridden roots if needed. */
  grpc_ssl_roots_override_result ovrd_res = GRPC_SSL_ROOTS_OVERRIDE_FAIL;
  if (GPR_SLICE_IS_EMPTY(result) && ssl_roots_override_cb != NULL) {
    char *pem_root_certs = NULL;
    ovrd_res = ssl_roots_override_cb(&pem_root_certs);
    if (ovrd_res == GRPC_SSL_ROOTS_OVERRIDE_OK) {
      GPR_ASSERT(pem_root_certs != NULL);
      result = gpr_slice_new(pem_root_certs, strlen(pem_root_certs), gpr_free);
    }
  }

  /* Fall back to installed certs if needed. */
  if (GPR_SLICE_IS_EMPTY(result) &&
      ovrd_res != GRPC_SSL_ROOTS_OVERRIDE_FAIL_PERMANENTLY) {
    GRPC_LOG_IF_ERROR("load_file",
                      grpc_load_file(installed_roots_path, 0, &result));
  }
  return result;
}

static gpr_slice default_pem_root_certs;

static void init_default_pem_root_certs(void) {
  default_pem_root_certs = compute_default_pem_root_certs_once();
}

gpr_slice grpc_get_default_ssl_roots_for_testing(void) {
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

size_t grpc_get_default_ssl_roots(const unsigned char **pem_root_certs) {
  /* TODO(jboeuf@google.com): Maybe revisit the approach which consists in
     loading all the roots once for the lifetime of the process. */
  static gpr_once once = GPR_ONCE_INIT;
  gpr_once_init(&once, init_default_pem_root_certs);
  *pem_root_certs = GPR_SLICE_START_PTR(default_pem_root_certs);
  return GPR_SLICE_LENGTH(default_pem_root_certs);
}

grpc_security_status grpc_ssl_channel_security_connector_create(
    grpc_call_credentials *request_metadata_creds,
    const grpc_ssl_config *config, const char *target_name,
    const char *overridden_target_name, grpc_channel_security_connector **sc) {
  size_t num_alpn_protocols = grpc_chttp2_num_alpn_versions();
  const unsigned char **alpn_protocol_strings =
      gpr_malloc(sizeof(const char *) * num_alpn_protocols);
  unsigned char *alpn_protocol_string_lengths =
      gpr_malloc(sizeof(unsigned char) * num_alpn_protocols);
  tsi_result result = TSI_OK;
  grpc_ssl_channel_security_connector *c;
  size_t i;
  const unsigned char *pem_root_certs;
  size_t pem_root_certs_size;
  char *port;

  for (i = 0; i < num_alpn_protocols; i++) {
    alpn_protocol_strings[i] =
        (const unsigned char *)grpc_chttp2_get_alpn_version_index(i);
    alpn_protocol_string_lengths[i] =
        (unsigned char)strlen(grpc_chttp2_get_alpn_version_index(i));
  }

  if (config == NULL || target_name == NULL) {
    gpr_log(GPR_ERROR, "An ssl channel needs a config and a target name.");
    goto error;
  }
  if (config->pem_root_certs == NULL) {
    pem_root_certs_size = grpc_get_default_ssl_roots(&pem_root_certs);
    if (pem_root_certs == NULL || pem_root_certs_size == 0) {
      gpr_log(GPR_ERROR, "Could not get default pem root certs.");
      goto error;
    }
  } else {
    pem_root_certs = config->pem_root_certs;
    pem_root_certs_size = config->pem_root_certs_size;
  }

  c = gpr_malloc(sizeof(grpc_ssl_channel_security_connector));
  memset(c, 0, sizeof(grpc_ssl_channel_security_connector));

  gpr_ref_init(&c->base.base.refcount, 1);
  c->base.base.vtable = &ssl_channel_vtable;
  c->base.base.url_scheme = GRPC_SSL_URL_SCHEME;
  c->base.request_metadata_creds =
      grpc_call_credentials_ref(request_metadata_creds);
  c->base.check_call_host = ssl_channel_check_call_host;
  c->base.do_handshake = ssl_channel_do_handshake;
  gpr_split_host_port(target_name, &c->target_name, &port);
  gpr_free(port);
  if (overridden_target_name != NULL) {
    c->overridden_target_name = gpr_strdup(overridden_target_name);
  }
  result = tsi_create_ssl_client_handshaker_factory(
      config->pem_private_key, config->pem_private_key_size,
      config->pem_cert_chain, config->pem_cert_chain_size, pem_root_certs,
      pem_root_certs_size, ssl_cipher_suites(), alpn_protocol_strings,
      alpn_protocol_string_lengths, (uint16_t)num_alpn_protocols,
      &c->handshaker_factory);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
            tsi_result_to_string(result));
    ssl_channel_destroy(&c->base.base);
    *sc = NULL;
    goto error;
  }
  *sc = &c->base;
  gpr_free((void *)alpn_protocol_strings);
  gpr_free(alpn_protocol_string_lengths);
  return GRPC_SECURITY_OK;

error:
  gpr_free((void *)alpn_protocol_strings);
  gpr_free(alpn_protocol_string_lengths);
  return GRPC_SECURITY_ERROR;
}

grpc_security_status grpc_ssl_server_security_connector_create(
    const grpc_ssl_server_config *config, grpc_server_security_connector **sc) {
  size_t num_alpn_protocols = grpc_chttp2_num_alpn_versions();
  const unsigned char **alpn_protocol_strings =
      gpr_malloc(sizeof(const char *) * num_alpn_protocols);
  unsigned char *alpn_protocol_string_lengths =
      gpr_malloc(sizeof(unsigned char) * num_alpn_protocols);
  tsi_result result = TSI_OK;
  grpc_ssl_server_security_connector *c;
  size_t i;

  for (i = 0; i < num_alpn_protocols; i++) {
    alpn_protocol_strings[i] =
        (const unsigned char *)grpc_chttp2_get_alpn_version_index(i);
    alpn_protocol_string_lengths[i] =
        (unsigned char)strlen(grpc_chttp2_get_alpn_version_index(i));
  }

  if (config == NULL || config->num_key_cert_pairs == 0) {
    gpr_log(GPR_ERROR, "An SSL server needs a key and a cert.");
    goto error;
  }
  c = gpr_malloc(sizeof(grpc_ssl_server_security_connector));
  memset(c, 0, sizeof(grpc_ssl_server_security_connector));

  gpr_ref_init(&c->base.base.refcount, 1);
  c->base.base.url_scheme = GRPC_SSL_URL_SCHEME;
  c->base.base.vtable = &ssl_server_vtable;
  result = tsi_create_ssl_server_handshaker_factory_ex(
      (const unsigned char **)config->pem_private_keys,
      config->pem_private_keys_sizes,
      (const unsigned char **)config->pem_cert_chains,
      config->pem_cert_chains_sizes, config->num_key_cert_pairs,
      config->pem_root_certs, config->pem_root_certs_size,
      get_tsi_client_certificate_request_type(
          config->client_certificate_request),
      ssl_cipher_suites(), alpn_protocol_strings, alpn_protocol_string_lengths,
      (uint16_t)num_alpn_protocols, &c->handshaker_factory);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
            tsi_result_to_string(result));
    ssl_server_destroy(&c->base.base);
    *sc = NULL;
    goto error;
  }
  gpr_mu_init(&c->base.mu);
  c->base.do_handshake = ssl_server_do_handshake;
  *sc = &c->base;
  gpr_free((void *)alpn_protocol_strings);
  gpr_free(alpn_protocol_string_lengths);
  return GRPC_SECURITY_OK;

error:
  gpr_free((void *)alpn_protocol_strings);
  gpr_free(alpn_protocol_string_lengths);
  return GRPC_SECURITY_ERROR;
}
