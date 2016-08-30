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

#include "src/core/lib/http/httpcli.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include "src/core/lib/security/transport/handshake.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/tsi/ssl_transport_security.h"

typedef struct {
  grpc_channel_security_connector base;
  tsi_ssl_handshaker_factory *handshaker_factory;
  char *secure_peer_name;
} grpc_httpcli_ssl_channel_security_connector;

static void httpcli_ssl_destroy(grpc_security_connector *sc) {
  grpc_httpcli_ssl_channel_security_connector *c =
      (grpc_httpcli_ssl_channel_security_connector *)sc;
  if (c->handshaker_factory != NULL) {
    tsi_ssl_handshaker_factory_destroy(c->handshaker_factory);
  }
  if (c->secure_peer_name != NULL) gpr_free(c->secure_peer_name);
  gpr_free(sc);
}

static void httpcli_ssl_do_handshake(grpc_exec_ctx *exec_ctx,
                                     grpc_channel_security_connector *sc,
                                     grpc_endpoint *nonsecure_endpoint,
                                     gpr_timespec deadline,
                                     grpc_security_handshake_done_cb cb,
                                     void *user_data) {
  grpc_httpcli_ssl_channel_security_connector *c =
      (grpc_httpcli_ssl_channel_security_connector *)sc;
  tsi_result result = TSI_OK;
  tsi_handshaker *handshaker;
  if (c->handshaker_factory == NULL) {
    cb(exec_ctx, user_data, GRPC_SECURITY_ERROR, NULL, NULL);
    return;
  }
  result = tsi_ssl_handshaker_factory_create_handshaker(
      c->handshaker_factory, c->secure_peer_name, &handshaker);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
            tsi_result_to_string(result));
    cb(exec_ctx, user_data, GRPC_SECURITY_ERROR, NULL, NULL);
  } else {
    grpc_do_security_handshake(exec_ctx, handshaker, &sc->base, true,
                               nonsecure_endpoint, deadline, cb, user_data);
  }
}

static void httpcli_ssl_check_peer(grpc_exec_ctx *exec_ctx,
                                   grpc_security_connector *sc, tsi_peer peer,
                                   grpc_security_peer_check_cb cb,
                                   void *user_data) {
  grpc_httpcli_ssl_channel_security_connector *c =
      (grpc_httpcli_ssl_channel_security_connector *)sc;
  grpc_security_status status = GRPC_SECURITY_OK;

  /* Check the peer name. */
  if (c->secure_peer_name != NULL &&
      !tsi_ssl_peer_matches_name(&peer, c->secure_peer_name)) {
    gpr_log(GPR_ERROR, "Peer name %s is not in peer certificate",
            c->secure_peer_name);
    status = GRPC_SECURITY_ERROR;
  }
  cb(exec_ctx, user_data, status, NULL);
  tsi_peer_destruct(&peer);
}

static grpc_security_connector_vtable httpcli_ssl_vtable = {
    httpcli_ssl_destroy, httpcli_ssl_check_peer};

static grpc_security_status httpcli_ssl_channel_security_connector_create(
    const unsigned char *pem_root_certs, size_t pem_root_certs_size,
    const char *secure_peer_name, grpc_channel_security_connector **sc) {
  tsi_result result = TSI_OK;
  grpc_httpcli_ssl_channel_security_connector *c;

  if (secure_peer_name != NULL && pem_root_certs == NULL) {
    gpr_log(GPR_ERROR,
            "Cannot assert a secure peer name without a trust root.");
    return GRPC_SECURITY_ERROR;
  }

  c = gpr_malloc(sizeof(grpc_httpcli_ssl_channel_security_connector));
  memset(c, 0, sizeof(grpc_httpcli_ssl_channel_security_connector));

  gpr_ref_init(&c->base.base.refcount, 1);
  c->base.base.vtable = &httpcli_ssl_vtable;
  if (secure_peer_name != NULL) {
    c->secure_peer_name = gpr_strdup(secure_peer_name);
  }
  result = tsi_create_ssl_client_handshaker_factory(
      NULL, 0, NULL, 0, pem_root_certs, pem_root_certs_size, NULL, NULL, NULL,
      0, &c->handshaker_factory);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
            tsi_result_to_string(result));
    httpcli_ssl_destroy(&c->base.base);
    *sc = NULL;
    return GRPC_SECURITY_ERROR;
  }
  c->base.do_handshake = httpcli_ssl_do_handshake;
  *sc = &c->base;
  return GRPC_SECURITY_OK;
}

/* handshaker */

typedef struct {
  void (*func)(grpc_exec_ctx *exec_ctx, void *arg, grpc_endpoint *endpoint);
  void *arg;
} on_done_closure;

static void on_secure_transport_setup_done(grpc_exec_ctx *exec_ctx, void *rp,
                                           grpc_security_status status,
                                           grpc_endpoint *secure_endpoint,
                                           grpc_auth_context *auth_context) {
  on_done_closure *c = rp;
  if (status != GRPC_SECURITY_OK) {
    gpr_log(GPR_ERROR, "Secure transport setup failed with error %d.", status);
    c->func(exec_ctx, c->arg, NULL);
  } else {
    c->func(exec_ctx, c->arg, secure_endpoint);
  }
  gpr_free(c);
}

static void ssl_handshake(grpc_exec_ctx *exec_ctx, void *arg,
                          grpc_endpoint *tcp, const char *host,
                          gpr_timespec deadline,
                          void (*on_done)(grpc_exec_ctx *exec_ctx, void *arg,
                                          grpc_endpoint *endpoint)) {
  grpc_channel_security_connector *sc = NULL;
  const unsigned char *pem_root_certs = NULL;
  on_done_closure *c = gpr_malloc(sizeof(*c));
  size_t pem_root_certs_size = grpc_get_default_ssl_roots(&pem_root_certs);
  if (pem_root_certs == NULL || pem_root_certs_size == 0) {
    gpr_log(GPR_ERROR, "Could not get default pem root certs.");
    on_done(exec_ctx, arg, NULL);
    gpr_free(c);
    return;
  }
  c->func = on_done;
  c->arg = arg;
  GPR_ASSERT(httpcli_ssl_channel_security_connector_create(
                 pem_root_certs, pem_root_certs_size, host, &sc) ==
             GRPC_SECURITY_OK);
  grpc_channel_security_connector_do_handshake(
      exec_ctx, sc, tcp, deadline, on_secure_transport_setup_done, c);
  GRPC_SECURITY_CONNECTOR_UNREF(&sc->base, "httpcli");
}

const grpc_httpcli_handshaker grpc_httpcli_ssl = {"https", ssl_handshake};
