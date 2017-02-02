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

#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/call.h"

#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

/* --- grpc_call --- */

grpc_call_error grpc_call_set_credentials(grpc_call *call,
                                          grpc_call_credentials *creds) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_client_security_context *ctx = NULL;
  GRPC_API_TRACE("grpc_call_set_credentials(call=%p, creds=%p)", 2,
                 (call, creds));
  if (!grpc_call_is_client(call)) {
    gpr_log(GPR_ERROR, "Method is client-side only.");
    return GRPC_CALL_ERROR_NOT_ON_SERVER;
  }
  ctx = (grpc_client_security_context *)grpc_call_context_get(
      call, GRPC_CONTEXT_SECURITY);
  if (ctx == NULL) {
    ctx = grpc_client_security_context_create();
    ctx->creds = grpc_call_credentials_ref(creds);
    grpc_call_context_set(call, GRPC_CONTEXT_SECURITY, ctx,
                          grpc_client_security_context_destroy);
  } else {
    grpc_call_credentials_unref(&exec_ctx, ctx->creds);
    ctx->creds = grpc_call_credentials_ref(creds);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  return GRPC_CALL_OK;
}

grpc_auth_context *grpc_call_auth_context(grpc_call *call) {
  void *sec_ctx = grpc_call_context_get(call, GRPC_CONTEXT_SECURITY);
  GRPC_API_TRACE("grpc_call_auth_context(call=%p)", 1, (call));
  if (sec_ctx == NULL) return NULL;
  return grpc_call_is_client(call)
             ? GRPC_AUTH_CONTEXT_REF(
                   ((grpc_client_security_context *)sec_ctx)->auth_context,
                   "grpc_call_auth_context client")
             : GRPC_AUTH_CONTEXT_REF(
                   ((grpc_server_security_context *)sec_ctx)->auth_context,
                   "grpc_call_auth_context server");
}

void grpc_auth_context_release(grpc_auth_context *context) {
  GRPC_API_TRACE("grpc_auth_context_release(context=%p)", 1, (context));
  GRPC_AUTH_CONTEXT_UNREF(context, "grpc_auth_context_unref");
}

/* --- grpc_client_security_context --- */

grpc_client_security_context *grpc_client_security_context_create(void) {
  grpc_client_security_context *ctx =
      gpr_malloc(sizeof(grpc_client_security_context));
  memset(ctx, 0, sizeof(grpc_client_security_context));
  return ctx;
}

void grpc_client_security_context_destroy(void *ctx) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_client_security_context *c = (grpc_client_security_context *)ctx;
  grpc_call_credentials_unref(&exec_ctx, c->creds);
  GRPC_AUTH_CONTEXT_UNREF(c->auth_context, "client_security_context");
  if (c->extension.instance != NULL && c->extension.destroy != NULL) {
    c->extension.destroy(c->extension.instance);
  }
  gpr_free(ctx);
  grpc_exec_ctx_finish(&exec_ctx);
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
  GRPC_AUTH_CONTEXT_UNREF(c->auth_context, "server_security_context");
  if (c->extension.instance != NULL && c->extension.destroy != NULL) {
    c->extension.destroy(c->extension.instance);
  }
  gpr_free(ctx);
}

/* --- grpc_auth_context --- */

static grpc_auth_property_iterator empty_iterator = {NULL, 0, NULL};

grpc_auth_context *grpc_auth_context_create(grpc_auth_context *chained) {
  grpc_auth_context *ctx = gpr_malloc(sizeof(grpc_auth_context));
  memset(ctx, 0, sizeof(grpc_auth_context));
  gpr_ref_init(&ctx->refcount, 1);
  if (chained != NULL) {
    ctx->chained = GRPC_AUTH_CONTEXT_REF(chained, "chained");
    ctx->peer_identity_property_name =
        ctx->chained->peer_identity_property_name;
  }
  return ctx;
}

#ifdef GRPC_AUTH_CONTEXT_REFCOUNT_DEBUG
grpc_auth_context *grpc_auth_context_ref(grpc_auth_context *ctx,
                                         const char *file, int line,
                                         const char *reason) {
  if (ctx == NULL) return NULL;
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
          "AUTH_CONTEXT:%p   ref %d -> %d %s", ctx, (int)ctx->refcount.count,
          (int)ctx->refcount.count + 1, reason);
#else
grpc_auth_context *grpc_auth_context_ref(grpc_auth_context *ctx) {
  if (ctx == NULL) return NULL;
#endif
  gpr_ref(&ctx->refcount);
  return ctx;
}

#ifdef GRPC_AUTH_CONTEXT_REFCOUNT_DEBUG
void grpc_auth_context_unref(grpc_auth_context *ctx, const char *file, int line,
                             const char *reason) {
  if (ctx == NULL) return;
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
          "AUTH_CONTEXT:%p unref %d -> %d %s", ctx, (int)ctx->refcount.count,
          (int)ctx->refcount.count - 1, reason);
#else
void grpc_auth_context_unref(grpc_auth_context *ctx) {
  if (ctx == NULL) return;
#endif
  if (gpr_unref(&ctx->refcount)) {
    size_t i;
    GRPC_AUTH_CONTEXT_UNREF(ctx->chained, "chained");
    if (ctx->properties.array != NULL) {
      for (i = 0; i < ctx->properties.count; i++) {
        grpc_auth_property_reset(&ctx->properties.array[i]);
      }
      gpr_free(ctx->properties.array);
    }
    gpr_free(ctx);
  }
}

const char *grpc_auth_context_peer_identity_property_name(
    const grpc_auth_context *ctx) {
  GRPC_API_TRACE("grpc_auth_context_peer_identity_property_name(ctx=%p)", 1,
                 (ctx));
  return ctx->peer_identity_property_name;
}

int grpc_auth_context_set_peer_identity_property_name(grpc_auth_context *ctx,
                                                      const char *name) {
  grpc_auth_property_iterator it =
      grpc_auth_context_find_properties_by_name(ctx, name);
  const grpc_auth_property *prop = grpc_auth_property_iterator_next(&it);
  GRPC_API_TRACE(
      "grpc_auth_context_set_peer_identity_property_name(ctx=%p, name=%s)", 2,
      (ctx, name));
  if (prop == NULL) {
    gpr_log(GPR_ERROR, "Property name %s not found in auth context.",
            name != NULL ? name : "NULL");
    return 0;
  }
  ctx->peer_identity_property_name = prop->name;
  return 1;
}

int grpc_auth_context_peer_is_authenticated(const grpc_auth_context *ctx) {
  GRPC_API_TRACE("grpc_auth_context_peer_is_authenticated(ctx=%p)", 1, (ctx));
  return ctx->peer_identity_property_name == NULL ? 0 : 1;
}

grpc_auth_property_iterator grpc_auth_context_property_iterator(
    const grpc_auth_context *ctx) {
  grpc_auth_property_iterator it = empty_iterator;
  GRPC_API_TRACE("grpc_auth_context_property_iterator(ctx=%p)", 1, (ctx));
  if (ctx == NULL) return it;
  it.ctx = ctx;
  return it;
}

const grpc_auth_property *grpc_auth_property_iterator_next(
    grpc_auth_property_iterator *it) {
  GRPC_API_TRACE("grpc_auth_property_iterator_next(it=%p)", 1, (it));
  if (it == NULL || it->ctx == NULL) return NULL;
  while (it->index == it->ctx->properties.count) {
    if (it->ctx->chained == NULL) return NULL;
    it->ctx = it->ctx->chained;
    it->index = 0;
  }
  if (it->name == NULL) {
    return &it->ctx->properties.array[it->index++];
  } else {
    while (it->index < it->ctx->properties.count) {
      const grpc_auth_property *prop = &it->ctx->properties.array[it->index++];
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
  GRPC_API_TRACE("grpc_auth_context_find_properties_by_name(ctx=%p, name=%s)",
                 2, (ctx, name));
  if (ctx == NULL || name == NULL) return empty_iterator;
  it.ctx = ctx;
  it.name = name;
  return it;
}

grpc_auth_property_iterator grpc_auth_context_peer_identity(
    const grpc_auth_context *ctx) {
  GRPC_API_TRACE("grpc_auth_context_peer_identity(ctx=%p)", 1, (ctx));
  if (ctx == NULL) return empty_iterator;
  return grpc_auth_context_find_properties_by_name(
      ctx, ctx->peer_identity_property_name);
}

static void ensure_auth_context_capacity(grpc_auth_context *ctx) {
  if (ctx->properties.count == ctx->properties.capacity) {
    ctx->properties.capacity =
        GPR_MAX(ctx->properties.capacity + 8, ctx->properties.capacity * 2);
    ctx->properties.array =
        gpr_realloc(ctx->properties.array,
                    ctx->properties.capacity * sizeof(grpc_auth_property));
  }
}

void grpc_auth_context_add_property(grpc_auth_context *ctx, const char *name,
                                    const char *value, size_t value_length) {
  grpc_auth_property *prop;
  GRPC_API_TRACE(
      "grpc_auth_context_add_property(ctx=%p, name=%s, value=%*.*s, "
      "value_length=%lu)",
      6, (ctx, name, (int)value_length, (int)value_length, value,
          (unsigned long)value_length));
  ensure_auth_context_capacity(ctx);
  prop = &ctx->properties.array[ctx->properties.count++];
  prop->name = gpr_strdup(name);
  prop->value = gpr_malloc(value_length + 1);
  memcpy(prop->value, value, value_length);
  prop->value[value_length] = '\0';
  prop->value_length = value_length;
}

void grpc_auth_context_add_cstring_property(grpc_auth_context *ctx,
                                            const char *name,
                                            const char *value) {
  grpc_auth_property *prop;
  GRPC_API_TRACE(
      "grpc_auth_context_add_cstring_property(ctx=%p, name=%s, value=%s)", 3,
      (ctx, name, value));
  ensure_auth_context_capacity(ctx);
  prop = &ctx->properties.array[ctx->properties.count++];
  prop->name = gpr_strdup(name);
  prop->value = gpr_strdup(value);
  prop->value_length = strlen(value);
}

void grpc_auth_property_reset(grpc_auth_property *property) {
  gpr_free(property->name);
  gpr_free(property->value);
  memset(property, 0, sizeof(grpc_auth_property));
}

static void auth_context_pointer_arg_destroy(grpc_exec_ctx *exec_ctx, void *p) {
  GRPC_AUTH_CONTEXT_UNREF(p, "auth_context_pointer_arg");
}

static void *auth_context_pointer_arg_copy(void *p) {
  return GRPC_AUTH_CONTEXT_REF(p, "auth_context_pointer_arg");
}

static int auth_context_pointer_cmp(void *a, void *b) { return GPR_ICMP(a, b); }

static const grpc_arg_pointer_vtable auth_context_pointer_vtable = {
    auth_context_pointer_arg_copy, auth_context_pointer_arg_destroy,
    auth_context_pointer_cmp};

grpc_arg grpc_auth_context_to_arg(grpc_auth_context *p) {
  grpc_arg arg;
  memset(&arg, 0, sizeof(grpc_arg));
  arg.type = GRPC_ARG_POINTER;
  arg.key = GRPC_AUTH_CONTEXT_ARG;
  arg.value.pointer.p = p;
  arg.value.pointer.vtable = &auth_context_pointer_vtable;
  return arg;
}

grpc_auth_context *grpc_auth_context_from_arg(const grpc_arg *arg) {
  if (strcmp(arg->key, GRPC_AUTH_CONTEXT_ARG) != 0) return NULL;
  if (arg->type != GRPC_ARG_POINTER) {
    gpr_log(GPR_ERROR, "Invalid type %d for arg %s", arg->type,
            GRPC_AUTH_CONTEXT_ARG);
    return NULL;
  }
  return arg->value.pointer.p;
}

grpc_auth_context *grpc_find_auth_context_in_args(
    const grpc_channel_args *args) {
  size_t i;
  if (args == NULL) return NULL;
  for (i = 0; i < args->num_args; i++) {
    grpc_auth_context *p = grpc_auth_context_from_arg(&args->args[i]);
    if (p != NULL) return p;
  }
  return NULL;
}
