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

#include "src/core/security/security_context.h"

#include <string.h>

#include "src/core/channel/channel_args.h"
#include "src/core/channel/http_client_filter.h"
#include "src/core/security/credentials.h"
#include "src/core/security/secure_endpoint.h"
#include "src/core/support/env.h"
#include "src/core/support/file.h"
#include "src/core/support/string.h"
#include "src/core/transport/chttp2/alpn.h"

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/slice_buffer.h>
#include "src/core/tsi/fake_transport_security.h"
#include "src/core/tsi/ssl_transport_security.h"

/* -- Constants. -- */

#ifndef INSTALL_PREFIX
static const char *installed_roots_path = "/usr/share/grpc/roots.pem";
#else
static const char *installed_roots_path = INSTALL_PREFIX "/share/grpc/roots.pem";
#endif

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

grpc_security_status grpc_security_context_create_handshaker(
    grpc_security_context *ctx, tsi_handshaker **handshaker) {
  if (ctx == NULL || handshaker == NULL) return GRPC_SECURITY_ERROR;
  return ctx->vtable->create_handshaker(ctx, handshaker);
}

grpc_security_status grpc_security_context_check_peer(
    grpc_security_context *ctx, tsi_peer peer, grpc_security_check_cb cb,
    void *user_data) {
  if (ctx == NULL) {
    tsi_peer_destruct(&peer);
    return GRPC_SECURITY_ERROR;
  }
  return ctx->vtable->check_peer(ctx, peer, cb, user_data);
}

grpc_security_status grpc_channel_security_context_check_call_host(
    grpc_channel_security_context *ctx, const char *host,
    grpc_security_check_cb cb, void *user_data) {
  if (ctx == NULL || ctx->check_call_host == NULL) return GRPC_SECURITY_ERROR;
  return ctx->check_call_host(ctx, host, cb, user_data);
}

void grpc_security_context_unref(grpc_security_context *ctx) {
  if (ctx == NULL) return;
  if (gpr_unref(&ctx->refcount)) ctx->vtable->destroy(ctx);
}

grpc_security_context *grpc_security_context_ref(grpc_security_context *ctx) {
  if (ctx == NULL) return NULL;
  gpr_ref(&ctx->refcount);
  return ctx;
}

static void context_pointer_arg_destroy(void *p) {
  grpc_security_context_unref(p);
}

static void *context_pointer_arg_copy(void *p) {
  return grpc_security_context_ref(p);
}

grpc_arg grpc_security_context_to_arg(grpc_security_context *ctx) {
  grpc_arg result;
  result.type = GRPC_ARG_POINTER;
  result.key = GRPC_SECURITY_CONTEXT_ARG;
  result.value.pointer.destroy = context_pointer_arg_destroy;
  result.value.pointer.copy = context_pointer_arg_copy;
  result.value.pointer.p = ctx;
  return result;
}

grpc_security_context *grpc_security_context_from_arg(const grpc_arg *arg) {
  if (strcmp(arg->key, GRPC_SECURITY_CONTEXT_ARG)) return NULL;
  if (arg->type != GRPC_ARG_POINTER) {
    gpr_log(GPR_ERROR, "Invalid type %d for arg %s", arg->type,
            GRPC_SECURITY_CONTEXT_ARG);
    return NULL;
  }
  return arg->value.pointer.p;
}

grpc_security_context *grpc_find_security_context_in_args(
    const grpc_channel_args *args) {
  size_t i;
  if (args == NULL) return NULL;
  for (i = 0; i < args->num_args; i++) {
    grpc_security_context *ctx = grpc_security_context_from_arg(&args->args[i]);
    if (ctx != NULL) return ctx;
  }
  return NULL;
}

static int check_request_metadata_creds(grpc_credentials *creds) {
  if (creds != NULL && !grpc_credentials_has_request_metadata(creds)) {
    gpr_log(GPR_ERROR,
            "Incompatible credentials for channel security context: needs to "
            "set request metadata.");
    return 0;
  }
  return 1;
}

/* -- Fake implementation. -- */

typedef struct {
  grpc_channel_security_context base;
  int call_host_check_is_async;
} grpc_fake_channel_security_context;

static void fake_channel_destroy(grpc_security_context *ctx) {
  grpc_channel_security_context *c = (grpc_channel_security_context *)ctx;
  grpc_credentials_unref(c->request_metadata_creds);
  gpr_free(ctx);
}

static void fake_server_destroy(grpc_security_context *ctx) { gpr_free(ctx); }

static grpc_security_status fake_channel_create_handshaker(
    grpc_security_context *ctx, tsi_handshaker **handshaker) {
  *handshaker = tsi_create_fake_handshaker(1);
  return GRPC_SECURITY_OK;
}

static grpc_security_status fake_server_create_handshaker(
    grpc_security_context *ctx, tsi_handshaker **handshaker) {
  *handshaker = tsi_create_fake_handshaker(0);
  return GRPC_SECURITY_OK;
}

static grpc_security_status fake_check_peer(grpc_security_context *ctx,
                                            tsi_peer peer,
                                            grpc_security_check_cb cb,
                                            void *user_data) {
  const char *prop_name;
  grpc_security_status status = GRPC_SECURITY_OK;
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
  if (peer.properties[0].type != TSI_PEER_PROPERTY_TYPE_STRING) {
    gpr_log(GPR_ERROR, "Invalid type of cert type property.");
    status = GRPC_SECURITY_ERROR;
    goto end;
  }
  if (strncmp(peer.properties[0].value.string.data, TSI_FAKE_CERTIFICATE_TYPE,
              peer.properties[0].value.string.length)) {
    gpr_log(GPR_ERROR, "Invalid value for cert type property.");
    status = GRPC_SECURITY_ERROR;
    goto end;
  }
end:
  tsi_peer_destruct(&peer);
  return status;
}

static grpc_security_status fake_channel_check_call_host(
    grpc_channel_security_context *ctx, const char *host,
    grpc_security_check_cb cb, void *user_data) {
  grpc_fake_channel_security_context *c =
      (grpc_fake_channel_security_context *)ctx;
  if (c->call_host_check_is_async) {
    cb(user_data, GRPC_SECURITY_OK);
    return GRPC_SECURITY_PENDING;
  } else {
    return GRPC_SECURITY_OK;
  }
}

static grpc_security_context_vtable fake_channel_vtable = {
    fake_channel_destroy, fake_channel_create_handshaker, fake_check_peer};

static grpc_security_context_vtable fake_server_vtable = {
    fake_server_destroy, fake_server_create_handshaker, fake_check_peer};

grpc_channel_security_context *grpc_fake_channel_security_context_create(
    grpc_credentials *request_metadata_creds, int call_host_check_is_async) {
  grpc_fake_channel_security_context *c =
      gpr_malloc(sizeof(grpc_fake_channel_security_context));
  gpr_ref_init(&c->base.base.refcount, 1);
  c->base.base.is_client_side = 1;
  c->base.base.url_scheme = GRPC_FAKE_SECURITY_URL_SCHEME;
  c->base.base.vtable = &fake_channel_vtable;
  GPR_ASSERT(check_request_metadata_creds(request_metadata_creds));
  c->base.request_metadata_creds = grpc_credentials_ref(request_metadata_creds);
  c->base.check_call_host = fake_channel_check_call_host;
  c->call_host_check_is_async = call_host_check_is_async;
  return &c->base;
}

grpc_security_context *grpc_fake_server_security_context_create(void) {
  grpc_security_context *c = gpr_malloc(sizeof(grpc_security_context));
  gpr_ref_init(&c->refcount, 1);
  c->vtable = &fake_server_vtable;
  c->url_scheme = GRPC_FAKE_SECURITY_URL_SCHEME;
  return c;
}

/* --- Ssl implementation. --- */

typedef struct {
  grpc_channel_security_context base;
  tsi_ssl_handshaker_factory *handshaker_factory;
  char *target_name;
  char *overridden_target_name;
  tsi_peer peer;
} grpc_ssl_channel_security_context;

typedef struct {
  grpc_security_context base;
  tsi_ssl_handshaker_factory *handshaker_factory;
} grpc_ssl_server_security_context;

static void ssl_channel_destroy(grpc_security_context *ctx) {
  grpc_ssl_channel_security_context *c =
      (grpc_ssl_channel_security_context *)ctx;
  grpc_credentials_unref(c->base.request_metadata_creds);
  if (c->handshaker_factory != NULL) {
    tsi_ssl_handshaker_factory_destroy(c->handshaker_factory);
  }
  if (c->target_name != NULL) gpr_free(c->target_name);
  if (c->overridden_target_name != NULL) gpr_free(c->overridden_target_name);
  tsi_peer_destruct(&c->peer);
  gpr_free(ctx);
}

static void ssl_server_destroy(grpc_security_context *ctx) {
  grpc_ssl_server_security_context *c = (grpc_ssl_server_security_context *)ctx;
  if (c->handshaker_factory != NULL) {
    tsi_ssl_handshaker_factory_destroy(c->handshaker_factory);
  }
  gpr_free(ctx);
}

static grpc_security_status ssl_create_handshaker(
    tsi_ssl_handshaker_factory *handshaker_factory, int is_client,
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

static grpc_security_status ssl_channel_create_handshaker(
    grpc_security_context *ctx, tsi_handshaker **handshaker) {
  grpc_ssl_channel_security_context *c =
      (grpc_ssl_channel_security_context *)ctx;
  return ssl_create_handshaker(c->handshaker_factory, 1,
                               c->overridden_target_name != NULL
                                   ? c->overridden_target_name
                                   : c->target_name,
                               handshaker);
}

static grpc_security_status ssl_server_create_handshaker(
    grpc_security_context *ctx, tsi_handshaker **handshaker) {
  grpc_ssl_server_security_context *c = (grpc_ssl_server_security_context *)ctx;
  return ssl_create_handshaker(c->handshaker_factory, 0, NULL, handshaker);
}

static int ssl_host_matches_name(const tsi_peer *peer,
                                 const char *peer_name) {
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

static grpc_security_status ssl_check_peer(const char *peer_name,
                                           const tsi_peer *peer) {
  /* Check the ALPN. */
  const tsi_peer_property *p =
      tsi_peer_get_property_by_name(peer, TSI_SSL_ALPN_SELECTED_PROTOCOL);
  if (p == NULL) {
    gpr_log(GPR_ERROR, "Missing selected ALPN property.");
    return GRPC_SECURITY_ERROR;
  }
  if (p->type != TSI_PEER_PROPERTY_TYPE_STRING) {
    gpr_log(GPR_ERROR, "Invalid selected ALPN property.");
    return GRPC_SECURITY_ERROR;
  }
  if (!grpc_chttp2_is_alpn_version_supported(p->value.string.data,
                                             p->value.string.length)) {
    gpr_log(GPR_ERROR, "Invalid ALPN value.");
    return GRPC_SECURITY_ERROR;
  }

  /* Check the peer name if specified. */
  if (peer_name != NULL &&
      !ssl_host_matches_name(peer, peer_name)) {
    gpr_log(GPR_ERROR, "Peer name %s is not in peer certificate", peer_name);
    return GRPC_SECURITY_ERROR;
  }

  return GRPC_SECURITY_OK;
}

static grpc_security_status ssl_channel_check_peer(grpc_security_context *ctx,
                                                   tsi_peer peer,
                                                   grpc_security_check_cb cb,
                                                   void *user_data) {
  grpc_ssl_channel_security_context *c =
      (grpc_ssl_channel_security_context *)ctx;
  grpc_security_status status;
  tsi_peer_destruct(&c->peer);
  c->peer = peer;
  status = ssl_check_peer(c->overridden_target_name != NULL
                              ? c->overridden_target_name
                              : c->target_name,
                          &peer);
  return status;
}

static grpc_security_status ssl_server_check_peer(grpc_security_context *ctx,
                                                  tsi_peer peer,
                                                  grpc_security_check_cb cb,
                                                  void *user_data) {
  /* TODO(jboeuf): Find a way to expose the peer to the authorization layer. */
  grpc_security_status status = ssl_check_peer(NULL, &peer);
  tsi_peer_destruct(&peer);
  return status;
}

static grpc_security_status ssl_channel_check_call_host(
    grpc_channel_security_context *ctx, const char *host,
    grpc_security_check_cb cb, void *user_data) {
  grpc_ssl_channel_security_context *c =
      (grpc_ssl_channel_security_context *)ctx;

  if (ssl_host_matches_name(&c->peer, host)) return GRPC_SECURITY_OK;

  /* If the target name was overridden, then the original target_name was
     'checked' transitively during the previous peer check at the end of the
     handshake. */
  if (c->overridden_target_name != NULL && strcmp(host, c->target_name) == 0) {
    return GRPC_SECURITY_OK;
  } else {
    return GRPC_SECURITY_ERROR;
  }
}

static grpc_security_context_vtable ssl_channel_vtable = {
    ssl_channel_destroy, ssl_channel_create_handshaker, ssl_channel_check_peer};

static grpc_security_context_vtable ssl_server_vtable = {
    ssl_server_destroy, ssl_server_create_handshaker, ssl_server_check_peer};

static gpr_slice default_pem_root_certs;

static void init_default_pem_root_certs(void) {
  /* First try to load the roots from the environment. */
  char *default_root_certs_path =
      gpr_getenv(GRPC_DEFAULT_SSL_ROOTS_FILE_PATH_ENV_VAR);
  if (default_root_certs_path == NULL) {
    default_pem_root_certs = gpr_empty_slice();
  } else {
    default_pem_root_certs = gpr_load_file(default_root_certs_path, NULL);
    gpr_free(default_root_certs_path);
  }

  /* Fall back to installed certs if needed. */
  if (GPR_SLICE_IS_EMPTY(default_pem_root_certs)) {
    default_pem_root_certs = gpr_load_file(installed_roots_path, NULL);
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

grpc_security_status grpc_ssl_channel_security_context_create(
    grpc_credentials *request_metadata_creds, const grpc_ssl_config *config,
    const char *target_name, const char *overridden_target_name,
    grpc_channel_security_context **ctx) {
  size_t num_alpn_protocols = grpc_chttp2_num_alpn_versions();
  const unsigned char **alpn_protocol_strings =
      gpr_malloc(sizeof(const char *) * num_alpn_protocols);
  unsigned char *alpn_protocol_string_lengths =
      gpr_malloc(sizeof(unsigned char) * num_alpn_protocols);
  tsi_result result = TSI_OK;
  grpc_ssl_channel_security_context *c;
  size_t i;
  const unsigned char *pem_root_certs;
  size_t pem_root_certs_size;
  char *port;

  for (i = 0; i < num_alpn_protocols; i++) {
    alpn_protocol_strings[i] =
        (const unsigned char *)grpc_chttp2_get_alpn_version_index(i);
    alpn_protocol_string_lengths[i] =
        strlen(grpc_chttp2_get_alpn_version_index(i));
  }

  if (config == NULL || target_name == NULL) {
    gpr_log(GPR_ERROR, "An ssl channel needs a config and a target name.");
    goto error;
  }
  if (!check_request_metadata_creds(request_metadata_creds)) {
    goto error;
  }

  c = gpr_malloc(sizeof(grpc_ssl_channel_security_context));
  memset(c, 0, sizeof(grpc_ssl_channel_security_context));

  gpr_ref_init(&c->base.base.refcount, 1);
  c->base.base.vtable = &ssl_channel_vtable;
  c->base.base.is_client_side = 1;
  c->base.base.url_scheme = GRPC_SSL_URL_SCHEME;
  c->base.request_metadata_creds = grpc_credentials_ref(request_metadata_creds);
  c->base.check_call_host = ssl_channel_check_call_host;
  gpr_split_host_port(target_name, &c->target_name, &port);
  gpr_free(port);
  if (overridden_target_name != NULL) {
    c->overridden_target_name = gpr_strdup(overridden_target_name);
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
  result = tsi_create_ssl_client_handshaker_factory(
      config->pem_private_key, config->pem_private_key_size,
      config->pem_cert_chain, config->pem_cert_chain_size, pem_root_certs,
      pem_root_certs_size, ssl_cipher_suites(), alpn_protocol_strings,
      alpn_protocol_string_lengths, num_alpn_protocols, &c->handshaker_factory);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
            tsi_result_to_string(result));
    ssl_channel_destroy(&c->base.base);
    *ctx = NULL;
    goto error;
  }
  *ctx = &c->base;
  gpr_free(alpn_protocol_strings);
  gpr_free(alpn_protocol_string_lengths);
  return GRPC_SECURITY_OK;

error:
  gpr_free(alpn_protocol_strings);
  gpr_free(alpn_protocol_string_lengths);
  return GRPC_SECURITY_ERROR;
}

grpc_security_status grpc_ssl_server_security_context_create(
    const grpc_ssl_server_config *config, grpc_security_context **ctx) {
  size_t num_alpn_protocols = grpc_chttp2_num_alpn_versions();
  const unsigned char **alpn_protocol_strings =
      gpr_malloc(sizeof(const char *) * num_alpn_protocols);
  unsigned char *alpn_protocol_string_lengths =
      gpr_malloc(sizeof(unsigned char) * num_alpn_protocols);
  tsi_result result = TSI_OK;
  grpc_ssl_server_security_context *c;
  size_t i;

  for (i = 0; i < num_alpn_protocols; i++) {
    alpn_protocol_strings[i] =
        (const unsigned char *)grpc_chttp2_get_alpn_version_index(i);
    alpn_protocol_string_lengths[i] =
        strlen(grpc_chttp2_get_alpn_version_index(i));
  }

  if (config == NULL || config->num_key_cert_pairs == 0) {
    gpr_log(GPR_ERROR, "An SSL server needs a key and a cert.");
    goto error;
  }
  c = gpr_malloc(sizeof(grpc_ssl_server_security_context));
  memset(c, 0, sizeof(grpc_ssl_server_security_context));

  gpr_ref_init(&c->base.refcount, 1);
  c->base.url_scheme = GRPC_SSL_URL_SCHEME;
  c->base.vtable = &ssl_server_vtable;
  result = tsi_create_ssl_server_handshaker_factory(
      (const unsigned char **)config->pem_private_keys,
      config->pem_private_keys_sizes,
      (const unsigned char **)config->pem_cert_chains,
      config->pem_cert_chains_sizes, config->num_key_cert_pairs,
      config->pem_root_certs, config->pem_root_certs_size,
      ssl_cipher_suites(), alpn_protocol_strings,
      alpn_protocol_string_lengths, num_alpn_protocols, &c->handshaker_factory);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
            tsi_result_to_string(result));
    ssl_server_destroy(&c->base);
    *ctx = NULL;
    goto error;
  }
  *ctx = &c->base;
  gpr_free(alpn_protocol_strings);
  gpr_free(alpn_protocol_string_lengths);
  return GRPC_SECURITY_OK;

error:
  gpr_free(alpn_protocol_strings);
  gpr_free(alpn_protocol_string_lengths);
  return GRPC_SECURITY_ERROR;
}

/* -- High level objects. -- */

grpc_channel *grpc_ssl_channel_create(grpc_credentials *ssl_creds,
                                      grpc_credentials *request_metadata_creds,
                                      const char *target,
                                      const grpc_channel_args *args) {
  grpc_channel_security_context *ctx = NULL;
  grpc_channel *channel = NULL;
  grpc_security_status status = GRPC_SECURITY_OK;
  size_t i = 0;
  const char *overridden_target_name = NULL;
  grpc_arg arg;
  grpc_channel_args *new_args;

  for (i = 0; args && i < args->num_args; i++) {
    grpc_arg *arg = &args->args[i];
    if (strcmp(arg->key, GRPC_SSL_TARGET_NAME_OVERRIDE_ARG) == 0 &&
        arg->type == GRPC_ARG_STRING) {
      overridden_target_name = arg->value.string;
      break;
    }
  }
  status = grpc_ssl_channel_security_context_create(
      request_metadata_creds, grpc_ssl_credentials_get_config(ssl_creds),
      target, overridden_target_name, &ctx);
  if (status != GRPC_SECURITY_OK) {
    return grpc_lame_client_channel_create();
  }
  arg.type = GRPC_ARG_STRING;
  arg.key = GRPC_ARG_HTTP2_SCHEME;
  arg.value.string = "https";
  new_args = grpc_channel_args_copy_and_add(args, &arg);
  channel = grpc_secure_channel_create_internal(target, new_args, ctx);
  grpc_security_context_unref(&ctx->base);
  grpc_channel_args_destroy(new_args);
  return channel;
}

grpc_channel *grpc_fake_transport_security_channel_create(
    grpc_credentials *fake_creds, grpc_credentials *request_metadata_creds,
    const char *target, const grpc_channel_args *args) {
  grpc_channel_security_context *ctx =
      grpc_fake_channel_security_context_create(request_metadata_creds, 1);
  grpc_channel *channel =
      grpc_secure_channel_create_internal(target, args, ctx);
  grpc_security_context_unref(&ctx->base);
  return channel;
}

grpc_channel *grpc_secure_channel_create_with_factories(
    const grpc_secure_channel_factory *factories, size_t num_factories,
    grpc_credentials *creds, const char *target,
    const grpc_channel_args *args) {
  size_t i;
  if (creds == NULL) {
    gpr_log(GPR_ERROR, "No credentials to create a secure channel.");
    return grpc_lame_client_channel_create();
  }
  if (grpc_credentials_has_request_metadata_only(creds)) {
    gpr_log(GPR_ERROR,
            "Credentials is insufficient to create a secure channel.");
    return grpc_lame_client_channel_create();
  }

  for (i = 0; i < num_factories; i++) {
    grpc_credentials *composite_creds = NULL;
    grpc_credentials *transport_security_creds = NULL;
    transport_security_creds = grpc_credentials_contains_type(
        creds, factories[i].creds_type, &composite_creds);
    if (transport_security_creds != NULL) {
      return factories[i].factory(transport_security_creds, composite_creds,
                                  target, args);
    }
  }

  gpr_log(GPR_ERROR,
          "Unknown credentials type %s for creating a secure channel.",
          creds->type);
  return grpc_lame_client_channel_create();
}
