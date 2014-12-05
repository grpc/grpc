/*
 *
 * Copyright 2014, Google Inc.
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

#include "src/core/endpoint/secure_endpoint.h"
#include "src/core/security/credentials.h"
#include "src/core/surface/lame_client.h"
#include "src/core/transport/chttp2/alpn.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice_buffer.h>
#include <grpc/support/string.h>

#include "src/core/tsi/fake_transport_security.h"
#include "src/core/tsi/ssl_transport_security.h"

/* -- Constants. -- */

/* Defines the cipher suites that we accept. All these cipher suites are
   compliant with TLS 1.2 and use an RSA public key. We prefer GCM over CBC
   and ECDHE-RSA over just RSA. */
#define GRPC_SSL_CIPHER_SUITES                                                 \
  "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:AES128-GCM-SHA256:" \
  "AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-"  \
  "SHA256:AES256-SHA256"

/* -- Common methods. -- */

grpc_security_status grpc_security_context_create_handshaker(
    grpc_security_context *ctx, tsi_handshaker **handshaker) {
  if (ctx == NULL || handshaker == NULL) return GRPC_SECURITY_ERROR;
  return ctx->vtable->create_handshaker(ctx, handshaker);
}

grpc_security_status grpc_security_context_check_peer(
    grpc_security_context *ctx, const tsi_peer *peer,
    grpc_security_check_peer_cb cb, void *user_data) {
  if (ctx == NULL) return GRPC_SECURITY_ERROR;
  return ctx->vtable->check_peer(ctx, peer, cb, user_data);
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

grpc_security_context *grpc_security_context_from_arg(
    const grpc_arg *arg) {
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

static void fake_channel_destroy(grpc_security_context *ctx) {
  grpc_channel_security_context *c = (grpc_channel_security_context *)ctx;
  grpc_credentials_unref(c->request_metadata_creds);
  gpr_free(ctx);
}

static void fake_server_destroy(grpc_security_context *ctx) {
  gpr_free(ctx);
}

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
                                            const tsi_peer *peer,
                                            grpc_security_check_peer_cb cb,
                                            void *user_data) {
  const char *prop_name;
  if (peer->property_count != 1) {
    gpr_log(GPR_ERROR, "Fake peers should only have 1 property.");
    return GRPC_SECURITY_ERROR;
  }
  prop_name = peer->properties[0].name;
  if (prop_name == NULL ||
      strcmp(prop_name, TSI_CERTIFICATE_TYPE_PEER_PROPERTY)) {
    gpr_log(GPR_ERROR, "Unexpected property in fake peer: %s.",
            prop_name == NULL ? "<EMPTY>" : prop_name);
    return GRPC_SECURITY_ERROR;
  }
  if (peer->properties[0].type != TSI_PEER_PROPERTY_TYPE_STRING) {
    gpr_log(GPR_ERROR, "Invalid type of cert type property.");
    return GRPC_SECURITY_ERROR;
  }
  if (strncmp(peer->properties[0].value.string.data, TSI_FAKE_CERTIFICATE_TYPE,
              peer->properties[0].value.string.length)) {
    gpr_log(GPR_ERROR, "Invalid value for cert type property.");
    return GRPC_SECURITY_ERROR;
  }
  return GRPC_SECURITY_OK;
}

static grpc_security_context_vtable fake_channel_vtable = {
    fake_channel_destroy, fake_channel_create_handshaker, fake_check_peer};

static grpc_security_context_vtable fake_server_vtable = {
    fake_server_destroy, fake_server_create_handshaker, fake_check_peer};

grpc_channel_security_context *grpc_fake_channel_security_context_create(
    grpc_credentials *request_metadata_creds) {
  grpc_channel_security_context *c =
      gpr_malloc(sizeof(grpc_channel_security_context));
  gpr_ref_init(&c->base.refcount, 1);
  c->base.is_client_side = 1;
  c->base.vtable = &fake_channel_vtable;
  GPR_ASSERT(check_request_metadata_creds(request_metadata_creds));
  c->request_metadata_creds = grpc_credentials_ref(request_metadata_creds);
  return c;
}

grpc_security_context *grpc_fake_server_security_context_create(void) {
  grpc_security_context *c = gpr_malloc(sizeof(grpc_security_context));
  gpr_ref_init(&c->refcount, 1);
  c->vtable = &fake_server_vtable;
  return c;
}

/* --- Ssl implementation. --- */

typedef struct {
  grpc_channel_security_context base;
  tsi_ssl_handshaker_factory *handshaker_factory;
  char *secure_peer_name;
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
  if (c->secure_peer_name != NULL) gpr_free(c->secure_peer_name);
  gpr_free(ctx);
}

static void ssl_server_destroy(grpc_security_context *ctx) {
  grpc_ssl_server_security_context *c =
      (grpc_ssl_server_security_context *)ctx;
  if (c->handshaker_factory != NULL) {
    tsi_ssl_handshaker_factory_destroy(c->handshaker_factory);
  }
  gpr_free(ctx);
}

static grpc_security_status ssl_create_handshaker(
    tsi_ssl_handshaker_factory *handshaker_factory, int is_client,
    const char *secure_peer_name, tsi_handshaker **handshaker) {
  tsi_result result = TSI_OK;
  if (handshaker_factory == NULL) return GRPC_SECURITY_ERROR;
  result = tsi_ssl_handshaker_factory_create_handshaker(
      handshaker_factory, is_client ? secure_peer_name : NULL, handshaker);
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
  return ssl_create_handshaker(c->handshaker_factory, 1, c->secure_peer_name,
                               handshaker);
}

static grpc_security_status ssl_server_create_handshaker(
    grpc_security_context *ctx, tsi_handshaker **handshaker) {
  grpc_ssl_server_security_context *c =
      (grpc_ssl_server_security_context *)ctx;
  return ssl_create_handshaker(c->handshaker_factory, 0, NULL, handshaker);
}

static grpc_security_status ssl_check_peer(const char *secure_peer_name,
                                           const tsi_peer *peer) {
  /* Check the ALPN. */
  const tsi_peer_property *p =
      tsi_peer_get_property_by_name(peer, TSI_SSL_ALPN_SELECTED_PROTOCOL);
  if (p == NULL || p->type != TSI_PEER_PROPERTY_TYPE_STRING) {
    gpr_log(GPR_ERROR, "Invalid or missing selected ALPN property.");
    return GRPC_SECURITY_ERROR;
  }
  if (!grpc_chttp2_is_alpn_version_supported(p->value.string.data,
                                             p->value.string.length)) {
    gpr_log(GPR_ERROR, "Invalid ALPN value.");
    return GRPC_SECURITY_ERROR;
  }

  /* Check the peer name if specified. */
  if (secure_peer_name != NULL &&
      !tsi_ssl_peer_matches_name(peer, secure_peer_name)) {
    gpr_log(GPR_ERROR, "Peer name %s is not in peer certificate",
            secure_peer_name);
    return GRPC_SECURITY_ERROR;
  }
  return GRPC_SECURITY_OK;
}

static grpc_security_status ssl_channel_check_peer(
    grpc_security_context *ctx, const tsi_peer *peer,
    grpc_security_check_peer_cb cb, void *user_data) {
  grpc_ssl_channel_security_context *c =
      (grpc_ssl_channel_security_context *)ctx;
  return ssl_check_peer(c->secure_peer_name, peer);
}

static grpc_security_status ssl_server_check_peer(
    grpc_security_context *ctx, const tsi_peer *peer,
    grpc_security_check_peer_cb cb, void *user_data) {
  /* TODO(jboeuf): Find a way to expose the peer to the authorization layer. */
  return ssl_check_peer(NULL, peer);
}

static grpc_security_context_vtable ssl_channel_vtable = {
    ssl_channel_destroy, ssl_channel_create_handshaker, ssl_channel_check_peer};

static grpc_security_context_vtable ssl_server_vtable = {
    ssl_server_destroy, ssl_server_create_handshaker, ssl_server_check_peer};

grpc_security_status grpc_ssl_channel_security_context_create(
    grpc_credentials *request_metadata_creds, const grpc_ssl_config *config,
    const char *secure_peer_name, grpc_channel_security_context **ctx) {
  const char *alpn_protocol_string = GRPC_CHTTP2_ALPN_VERSION;
  unsigned char alpn_protocol_string_len =
      (unsigned char)strlen(alpn_protocol_string);
  tsi_result result = TSI_OK;
  grpc_ssl_channel_security_context *c;

  if (config == NULL || secure_peer_name == NULL ||
      config->pem_root_certs == NULL) {
    gpr_log(GPR_ERROR, "An ssl channel needs a secure name and root certs.");
    return GRPC_SECURITY_ERROR;
  }
  if (!check_request_metadata_creds(request_metadata_creds)) {
    return GRPC_SECURITY_ERROR;
  }

  c = gpr_malloc(sizeof(grpc_ssl_channel_security_context));
  memset(c, 0, sizeof(grpc_ssl_channel_security_context));

  gpr_ref_init(&c->base.base.refcount, 1);
  c->base.base.vtable = &ssl_channel_vtable;
  c->base.base.is_client_side = 1;
  c->base.request_metadata_creds = grpc_credentials_ref(request_metadata_creds);
  if (secure_peer_name != NULL) {
    c->secure_peer_name = gpr_strdup(secure_peer_name);
  }
  result = tsi_create_ssl_client_handshaker_factory(
      config->pem_private_key, config->pem_private_key_size,
      config->pem_cert_chain, config->pem_cert_chain_size,
      config->pem_root_certs, config->pem_root_certs_size,
      GRPC_SSL_CIPHER_SUITES, (const unsigned char **)&alpn_protocol_string,
      &alpn_protocol_string_len, 1, &c->handshaker_factory);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
            tsi_result_to_string(result));
    ssl_channel_destroy(&c->base.base);
    *ctx = NULL;
    return GRPC_SECURITY_ERROR;
  }
  *ctx = &c->base;
  return GRPC_SECURITY_OK;
}

grpc_security_status grpc_ssl_server_security_context_create(
    const grpc_ssl_config *config, grpc_security_context **ctx) {
  const char *alpn_protocol_string = GRPC_CHTTP2_ALPN_VERSION;
  unsigned char alpn_protocol_string_len =
      (unsigned char)strlen(alpn_protocol_string);
  tsi_result result = TSI_OK;
  grpc_ssl_server_security_context *c;

  if (config == NULL || config->pem_private_key == NULL ||
      config->pem_cert_chain == NULL) {
    gpr_log(GPR_ERROR, "An SSL server needs a key and a cert.");
    return GRPC_SECURITY_ERROR;
  }
  c = gpr_malloc(sizeof(grpc_ssl_server_security_context));
  memset(c, 0, sizeof(grpc_ssl_server_security_context));

  gpr_ref_init(&c->base.refcount, 1);
  c->base.vtable = &ssl_server_vtable;
  result = tsi_create_ssl_server_handshaker_factory(
      (const unsigned char **)&config->pem_private_key,
      (const gpr_uint32 *)&config->pem_private_key_size,
      (const unsigned char **)&config->pem_cert_chain,
      (const gpr_uint32 *)&config->pem_cert_chain_size, 1,
      config->pem_root_certs, config->pem_root_certs_size,
      GRPC_SSL_CIPHER_SUITES, (const unsigned char **)&alpn_protocol_string,
      &alpn_protocol_string_len, 1, &c->handshaker_factory);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
            tsi_result_to_string(result));
    ssl_server_destroy(&c->base);
    *ctx = NULL;
    return GRPC_SECURITY_ERROR;
  }
  *ctx = &c->base;
  return GRPC_SECURITY_OK;
}



/* -- High level objects. -- */

static grpc_channel *grpc_ssl_channel_create(grpc_credentials *creds,
                                             const grpc_ssl_config *config,
                                             const char *target,
                                             const grpc_channel_args *args) {
  grpc_channel_security_context *ctx = NULL;
  grpc_channel *channel = NULL;
  grpc_security_status status = GRPC_SECURITY_OK;
  size_t i = 0;
  const char *secure_peer_name = target;
  for (i = 0; i < args->num_args; i++) {
    grpc_arg *arg = &args->args[i];
    if (!strcmp(arg->key, GRPC_SSL_TARGET_NAME_OVERRIDE_ARG) &&
        arg->type == GRPC_ARG_STRING) {
      secure_peer_name = arg->value.string;
      break;
    }
  }
  status = grpc_ssl_channel_security_context_create(creds, config,
                                                    secure_peer_name, &ctx);
  if (status != GRPC_SECURITY_OK) {
    return grpc_lame_client_channel_create();
  }
  channel = grpc_secure_channel_create_internal(target, args, ctx);
  grpc_security_context_unref(&ctx->base);
  return channel;
}


static grpc_credentials *get_creds_from_composite(
    grpc_credentials *composite_creds, const char *type) {
  size_t i;
  const grpc_credentials_array *inner_creds_array =
      grpc_composite_credentials_get_credentials(composite_creds);
  for (i = 0; i < inner_creds_array->num_creds; i++) {
    if (!strcmp(type, inner_creds_array->creds_array[i]->type)) {
      return inner_creds_array->creds_array[i];
    }
  }
  return NULL;
}

static grpc_channel *grpc_channel_create_from_composite_creds(
    grpc_credentials *composite_creds, const char *target,
    const grpc_channel_args *args) {
  grpc_credentials *creds =
      get_creds_from_composite(composite_creds, GRPC_CREDENTIALS_TYPE_SSL);
  if (creds != NULL) {
    return grpc_ssl_channel_create(
        composite_creds, grpc_ssl_credentials_get_config(creds), target, args);
  }
  return NULL; /* TODO(ctiller): return lame channel. */
}

grpc_channel *grpc_secure_channel_create(grpc_credentials *creds,
                                         const char *target,
                                         const grpc_channel_args *args) {
  if (grpc_credentials_has_request_metadata_only(creds)) {
    gpr_log(GPR_ERROR,
            "Credentials is insufficient to create a secure channel.");
    return grpc_lame_client_channel_create();
  }
  if (!strcmp(creds->type, GRPC_CREDENTIALS_TYPE_SSL)) {
    return grpc_ssl_channel_create(NULL, grpc_ssl_credentials_get_config(creds),
                                   target, args);
  } else if (!strcmp(creds->type,
                     GRPC_CREDENTIALS_TYPE_FAKE_TRANSPORT_SECURITY)) {
    grpc_channel_security_context *ctx =
        grpc_fake_channel_security_context_create(NULL);
    grpc_channel *channel =
        grpc_secure_channel_create_internal(target, args, ctx);
    grpc_security_context_unref(&ctx->base);
    return channel;
  } else if (!strcmp(creds->type, GRPC_CREDENTIALS_TYPE_COMPOSITE)) {
    return grpc_channel_create_from_composite_creds(creds, target, args);
  } else {
    gpr_log(GPR_ERROR,
            "Unknown credentials type %s for creating a secure channel.");
    return grpc_lame_client_channel_create();
  }
}

grpc_channel *grpc_default_secure_channel_create(
    const char *target, const grpc_channel_args *args) {
  return grpc_secure_channel_create(grpc_default_credentials_create(), target,
                                    args);
}

grpc_server *grpc_secure_server_create(grpc_server_credentials *creds,
                                       grpc_completion_queue *cq,
                                       const grpc_channel_args *args) {
  grpc_security_status status = GRPC_SECURITY_ERROR;
  grpc_security_context *ctx = NULL;
  grpc_server *server = NULL;
  if (creds == NULL) return NULL; /* TODO(ctiller): Return lame server. */
  if (!strcmp(creds->type, GRPC_CREDENTIALS_TYPE_SSL)) {
    status = grpc_ssl_server_security_context_create(
        grpc_ssl_server_credentials_get_config(creds), &ctx);
  } else if (!strcmp(creds->type,
                     GRPC_CREDENTIALS_TYPE_FAKE_TRANSPORT_SECURITY)) {
    ctx = grpc_fake_server_security_context_create();
    status = GRPC_SECURITY_OK;
  } else {
    gpr_log(GPR_ERROR,
            "Unable to create secure server with credentials of type %s.",
            creds->type);
  }
  if (status != GRPC_SECURITY_OK) {
    return NULL; /* TODO(ctiller): Return lame server. */
  }
  server = grpc_secure_server_create_internal(cq, args, ctx);
  grpc_security_context_unref(ctx);
  return server;
}
