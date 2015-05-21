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

#include <string.h>

#include "src/core/security/security_context.h"
#include "src/core/surface/call.h"
#include "src/core/support/string.h"

#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

/* --- grpc_call --- */

grpc_call_error grpc_call_set_credentials(grpc_call *call,
                                          grpc_credentials *creds) {
  grpc_client_security_context *ctx = NULL;
  if (!grpc_call_is_client(call)) {
    gpr_log(GPR_ERROR, "Method is client-side only.");
    return GRPC_CALL_ERROR_NOT_ON_SERVER;
  }
  if (creds != NULL && !grpc_credentials_has_request_metadata_only(creds)) {
    gpr_log(GPR_ERROR, "Incompatible credentials to set on a call.");
    return GRPC_CALL_ERROR;
  }
  ctx = (grpc_client_security_context *)grpc_call_context_get(
      call, GRPC_CONTEXT_SECURITY);
  if (ctx == NULL) {
    ctx = grpc_client_security_context_create();
    ctx->creds = grpc_credentials_ref(creds);
    grpc_call_context_set(call, GRPC_CONTEXT_SECURITY, ctx,
                          grpc_client_security_context_destroy);
  } else {
    grpc_credentials_unref(ctx->creds);
    ctx->creds = grpc_credentials_ref(creds);
  }
  return GRPC_CALL_OK;
}

const grpc_auth_context *grpc_call_auth_context(grpc_call *call) {
  void *sec_ctx = grpc_call_context_get(call, GRPC_CONTEXT_SECURITY);
  if (sec_ctx == NULL) return NULL;
  return grpc_call_is_client(call)
             ? ((grpc_client_security_context *)sec_ctx)->auth_context
             : ((grpc_server_security_context *)sec_ctx)->auth_context;
}

/* --- grpc_client_security_context --- */

grpc_client_security_context *grpc_client_security_context_create(void) {
  grpc_client_security_context *ctx =
      gpr_malloc(sizeof(grpc_client_security_context));
  memset(ctx, 0, sizeof(grpc_client_security_context));
  return ctx;
}

void grpc_client_security_context_destroy(void *ctx) {
  grpc_client_security_context *c = (grpc_client_security_context *)ctx;
  grpc_credentials_unref(c->creds);
  grpc_auth_context_unref(c->auth_context);
  gpr_free(ctx);
}

/* --- grpc_server_security_context --- */

grpc_server_security_context *grpc_server_security_context_create(void) {
  grpc_server_security_context *ctx =
      gpr_malloc(sizeof(grpc_server_security_context));
  memset(ctx, 0, sizeof(grpc_server_security_context));
  return ctx;
}

void grpc_server_security_context_destroy(void *ctx) {
  grpc_server_security_context *c = (grpc_server_security_context *)ctx;
  grpc_auth_context_unref(c->auth_context);
  gpr_free(ctx);
}

/* --- grpc_auth_context --- */

static grpc_auth_property_iterator empty_iterator = {NULL, 0, NULL};

grpc_auth_context *grpc_auth_context_create(grpc_auth_context *chained,
                                            size_t property_count) {
  grpc_auth_context *ctx = gpr_malloc(sizeof(grpc_auth_context));
  memset(ctx, 0, sizeof(grpc_auth_context));
  ctx->properties = gpr_malloc(property_count * sizeof(grpc_auth_property));
  memset(ctx->properties, 0, property_count * sizeof(grpc_auth_property));
  ctx->property_count = property_count;
  gpr_ref_init(&ctx->refcount, 1);
  if (chained != NULL) ctx->chained = grpc_auth_context_ref(chained);
  return ctx;
}

grpc_auth_context *grpc_auth_context_ref(grpc_auth_context *ctx) {
  if (ctx == NULL) return NULL;
  gpr_ref(&ctx->refcount);
  return ctx;
}

void grpc_auth_context_unref(grpc_auth_context *ctx) {
  if (ctx == NULL) return;
  if (gpr_unref(&ctx->refcount)) {
    size_t i;
    grpc_auth_context_unref(ctx->chained);
    if (ctx->properties != NULL) {
      for (i = 0; i < ctx->property_count; i++) {
        grpc_auth_property_reset(&ctx->properties[i]);
      }
      gpr_free(ctx->properties);
    }
    gpr_free(ctx);
  }
}

const char *grpc_auth_context_peer_identity_property_name(
    const grpc_auth_context *ctx) {
  return ctx->peer_identity_property_name;
}

int grpc_auth_context_peer_is_authenticated(
    const grpc_auth_context *ctx) {
  return ctx->peer_identity_property_name == NULL ? 0 : 1;
}

grpc_auth_property_iterator grpc_auth_context_property_iterator(
    const grpc_auth_context *ctx) {
  grpc_auth_property_iterator it = empty_iterator;
  if (ctx == NULL) return it;
  it.ctx = ctx;
  return it;
}

const grpc_auth_property *grpc_auth_property_iterator_next(
    grpc_auth_property_iterator *it) {
  if (it == NULL || it->ctx == NULL) return NULL;
  while (it->index == it->ctx->property_count) {
    if (it->ctx->chained == NULL) return NULL;
    it->ctx = it->ctx->chained;
    it->index = 0;
  }
  if (it->name == NULL) {
    return &it->ctx->properties[it->index++];
  } else {
    while (it->index < it->ctx->property_count) {
      const grpc_auth_property *prop = &it->ctx->properties[it->index++];
      GPR_ASSERT(prop->name != NULL);
      if (strcmp(it->name, prop->name) == 0) {
        return prop;
      }
    }
    /* We could not find the name, try another round. */
    return grpc_auth_property_iterator_next(it);
  }
}

grpc_auth_property_iterator grpc_auth_context_find_properties_by_name(
    const grpc_auth_context *ctx, const char *name) {
  grpc_auth_property_iterator it = empty_iterator;
  if (ctx == NULL || name == NULL) return empty_iterator;
  it.ctx = ctx;
  it.name = name;
  return it;
}

grpc_auth_property_iterator grpc_auth_context_peer_identity(
    const grpc_auth_context *ctx) {
  if (ctx == NULL) return empty_iterator;
  return grpc_auth_context_find_properties_by_name(
      ctx, ctx->peer_identity_property_name);
}

grpc_auth_property grpc_auth_property_init_from_cstring(const char *name,
                                                        const char *value) {
  grpc_auth_property prop;
  prop.name = gpr_strdup(name);
  prop.value = gpr_strdup(value);
  prop.value_length = strlen(value);
  return prop;
}

grpc_auth_property grpc_auth_property_init(const char *name, const char *value,
                                           size_t value_length) {
  grpc_auth_property prop;
  prop.name = gpr_strdup(name);
  prop.value = gpr_malloc(value_length + 1);
  memcpy(prop.value, value, value_length);
  prop.value[value_length] = '\0';
  prop.value_length = value_length;
  return prop;
}

void grpc_auth_property_reset(grpc_auth_property *property) {
  if (property->name != NULL) gpr_free(property->name);
  if (property->value != NULL) gpr_free(property->value);
  memset(property, 0, sizeof(grpc_auth_property));
}

