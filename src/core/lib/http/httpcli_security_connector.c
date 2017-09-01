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

#include "src/core/lib/http/httpcli.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker_registry.h"
#include "src/core/lib/security/transport/security_handshaker.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/support/string.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security_adapter.h"

typedef struct {
  grpc_channel_security_connector base;
  tsi_ssl_client_handshaker_factory *handshaker_factory;
  char *secure_peer_name;
} grpc_httpcli_ssl_channel_security_connector;

static void httpcli_ssl_destroy(grpc_exec_ctx *exec_ctx,
                                grpc_security_connector *sc) {
  grpc_httpcli_ssl_channel_security_connector *c =
      (grpc_httpcli_ssl_channel_security_connector *)sc;
  if (c->handshaker_factory != NULL) {
    tsi_ssl_client_handshaker_factory_unref(c->handshaker_factory);
    c->handshaker_factory = NULL;
  }
  if (c->secure_peer_name != NULL) gpr_free(c->secure_peer_name);
  gpr_free(sc);
}

static void httpcli_ssl_add_handshakers(grpc_exec_ctx *exec_ctx,
                                        grpc_channel_security_connector *sc,
                                        grpc_handshake_manager *handshake_mgr) {
  grpc_httpcli_ssl_channel_security_connector *c =
      (grpc_httpcli_ssl_channel_security_connector *)sc;
  tsi_handshaker *handshaker = NULL;
  if (c->handshaker_factory != NULL) {
    tsi_result result = tsi_ssl_client_handshaker_factory_create_handshaker(
        c->handshaker_factory, c->secure_peer_name, &handshaker);
    if (result != TSI_OK) {
      gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
              tsi_result_to_string(result));
    }
  }
  grpc_handshake_manager_add(
      handshake_mgr,
      grpc_security_handshaker_create(
          exec_ctx, tsi_create_adapter_handshaker(handshaker), &sc->base));
}

static void httpcli_ssl_check_peer(grpc_exec_ctx *exec_ctx,
                                   grpc_security_connector *sc, tsi_peer peer,
                                   grpc_auth_context **auth_context,
                                   grpc_closure *on_peer_checked) {
  grpc_httpcli_ssl_channel_security_connector *c =
      (grpc_httpcli_ssl_channel_security_connector *)sc;
  grpc_error *error = GRPC_ERROR_NONE;

  /* Check the peer name. */
  if (c->secure_peer_name != NULL &&
      !tsi_ssl_peer_matches_name(&peer, c->secure_peer_name)) {
    char *msg;
    gpr_asprintf(&msg, "Peer name %s is not in peer certificate",
                 c->secure_peer_name);
    error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
  }
  GRPC_CLOSURE_SCHED(exec_ctx, on_peer_checked, error);
  tsi_peer_destruct(&peer);
}

static grpc_security_connector_vtable httpcli_ssl_vtable = {
    httpcli_ssl_destroy, httpcli_ssl_check_peer};

static grpc_security_status httpcli_ssl_channel_security_connector_create(
    grpc_exec_ctx *exec_ctx, const char *pem_root_certs,
    const char *secure_peer_name, grpc_channel_security_connector **sc) {
  tsi_result result = TSI_OK;
  grpc_httpcli_ssl_channel_security_connector *c;

  if (secure_peer_name != NULL && pem_root_certs == NULL) {
    gpr_log(GPR_ERROR,
            "Cannot assert a secure peer name without a trust root.");
    return GRPC_SECURITY_ERROR;
  }

  c = gpr_zalloc(sizeof(grpc_httpcli_ssl_channel_security_connector));

  gpr_ref_init(&c->base.base.refcount, 1);
  c->base.base.vtable = &httpcli_ssl_vtable;
  if (secure_peer_name != NULL) {
    c->secure_peer_name = gpr_strdup(secure_peer_name);
  }
  result = tsi_create_ssl_client_handshaker_factory(
      NULL, pem_root_certs, NULL, NULL, 0, &c->handshaker_factory);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
            tsi_result_to_string(result));
    httpcli_ssl_destroy(exec_ctx, &c->base.base);
    *sc = NULL;
    return GRPC_SECURITY_ERROR;
  }
  c->base.add_handshakers = httpcli_ssl_add_handshakers;
  *sc = &c->base;
  return GRPC_SECURITY_OK;
}

/* handshaker */

typedef struct {
  void (*func)(grpc_exec_ctx *exec_ctx, void *arg, grpc_endpoint *endpoint);
  void *arg;
  grpc_handshake_manager *handshake_mgr;
} on_done_closure;

static void on_handshake_done(grpc_exec_ctx *exec_ctx, void *arg,
                              grpc_error *error) {
  grpc_handshaker_args *args = arg;
  on_done_closure *c = args->user_data;
  if (error != GRPC_ERROR_NONE) {
    const char *msg = grpc_error_string(error);
    gpr_log(GPR_ERROR, "Secure transport setup failed: %s", msg);

    c->func(exec_ctx, c->arg, NULL);
  } else {
    grpc_channel_args_destroy(exec_ctx, args->args);
    grpc_slice_buffer_destroy_internal(exec_ctx, args->read_buffer);
    gpr_free(args->read_buffer);
    c->func(exec_ctx, c->arg, args->endpoint);
  }
  grpc_handshake_manager_destroy(exec_ctx, c->handshake_mgr);
  gpr_free(c);
}

static void ssl_handshake(grpc_exec_ctx *exec_ctx, void *arg,
                          grpc_endpoint *tcp, const char *host,
                          gpr_timespec deadline,
                          void (*on_done)(grpc_exec_ctx *exec_ctx, void *arg,
                                          grpc_endpoint *endpoint)) {
  on_done_closure *c = gpr_malloc(sizeof(*c));
  const char *pem_root_certs = grpc_get_default_ssl_roots();
  if (pem_root_certs == NULL) {
    gpr_log(GPR_ERROR, "Could not get default pem root certs.");
    on_done(exec_ctx, arg, NULL);
    gpr_free(c);
    return;
  }
  c->func = on_done;
  c->arg = arg;
  grpc_channel_security_connector *sc = NULL;
  GPR_ASSERT(httpcli_ssl_channel_security_connector_create(
                 exec_ctx, pem_root_certs, host, &sc) == GRPC_SECURITY_OK);
  grpc_arg channel_arg = grpc_security_connector_to_arg(&sc->base);
  grpc_channel_args args = {1, &channel_arg};
  c->handshake_mgr = grpc_handshake_manager_create();
  grpc_handshakers_add(exec_ctx, HANDSHAKER_CLIENT, &args, c->handshake_mgr);
  grpc_handshake_manager_do_handshake(
      exec_ctx, c->handshake_mgr, tcp, NULL /* channel_args */, deadline,
      NULL /* acceptor */, on_handshake_done, c /* user_data */);
  GRPC_SECURITY_CONNECTOR_UNREF(exec_ctx, &sc->base, "httpcli");
}

const grpc_httpcli_handshaker grpc_httpcli_ssl = {"https", ssl_handshake};
