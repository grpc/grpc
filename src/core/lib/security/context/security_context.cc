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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/context/security_context.h"

#include <string.h>

#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/call.h"

grpc_core::DebugOnlyTraceFlag grpc_trace_auth_context_refcount(
    false, "auth_context_refcount");

/* --- grpc_call --- */

grpc_call_error grpc_call_set_credentials(grpc_call* call,
                                          grpc_call_credentials* creds) {
  grpc_core::ExecCtx exec_ctx;
  grpc_client_security_context* ctx = nullptr;
  GRPC_API_TRACE("grpc_call_set_credentials(call=%p, creds=%p)", 2,
                 (call, creds));
  if (!grpc_call_is_client(call)) {
    gpr_log(GPR_ERROR, "Method is client-side only.");
    return GRPC_CALL_ERROR_NOT_ON_SERVER;
  }
  ctx = static_cast<grpc_client_security_context*>(
      grpc_call_context_get(call, GRPC_CONTEXT_SECURITY));
  if (ctx == nullptr) {
    ctx = grpc_client_security_context_create(grpc_call_get_arena(call), creds);
    grpc_call_context_set(call, GRPC_CONTEXT_SECURITY, ctx,
                          grpc_client_security_context_destroy);
  } else {
    ctx->creds = creds != nullptr ? creds->Ref() : nullptr;
  }

  return GRPC_CALL_OK;
}

grpc_auth_context* grpc_call_auth_context(grpc_call* call) {
  void* sec_ctx = grpc_call_context_get(call, GRPC_CONTEXT_SECURITY);
  GRPC_API_TRACE("grpc_call_auth_context(call=%p)", 1, (call));
  if (sec_ctx == nullptr) return nullptr;
  if (grpc_call_is_client(call)) {
    auto* sc = static_cast<grpc_client_security_context*>(sec_ctx);
    if (sc->auth_context == nullptr) {
      return nullptr;
    } else {
      return sc->auth_context
          ->Ref(DEBUG_LOCATION, "grpc_call_auth_context client")
          .release();
    }
  } else {
    auto* sc = static_cast<grpc_server_security_context*>(sec_ctx);
    if (sc->auth_context == nullptr) {
      return nullptr;
    } else {
      return sc->auth_context
          ->Ref(DEBUG_LOCATION, "grpc_call_auth_context server")
          .release();
    }
  }
}

void grpc_auth_context_release(grpc_auth_context* context) {
  GRPC_API_TRACE("grpc_auth_context_release(context=%p)", 1, (context));
  if (context == nullptr) return;
  context->Unref(DEBUG_LOCATION, "grpc_auth_context_unref");
}

/* --- grpc_client_security_context --- */
grpc_client_security_context::~grpc_client_security_context() {
  auth_context.reset(DEBUG_LOCATION, "client_security_context");
  if (extension.instance != nullptr && extension.destroy != nullptr) {
    extension.destroy(extension.instance);
  }
}

grpc_client_security_context* grpc_client_security_context_create(
    grpc_core::Arena* arena, grpc_call_credentials* creds) {
  return arena->New<grpc_client_security_context>(
      creds != nullptr ? creds->Ref() : nullptr);
}

void grpc_client_security_context_destroy(void* ctx) {
  grpc_core::ExecCtx exec_ctx;
  grpc_client_security_context* c =
      static_cast<grpc_client_security_context*>(ctx);
  c->~grpc_client_security_context();
}

/* --- grpc_server_security_context --- */
grpc_server_security_context::~grpc_server_security_context() {
  auth_context.reset(DEBUG_LOCATION, "server_security_context");
  if (extension.instance != nullptr && extension.destroy != nullptr) {
    extension.destroy(extension.instance);
  }
}

grpc_server_security_context* grpc_server_security_context_create(
    grpc_core::Arena* arena) {
  return arena->New<grpc_server_security_context>();
}

void grpc_server_security_context_destroy(void* ctx) {
  grpc_server_security_context* c =
      static_cast<grpc_server_security_context*>(ctx);
  c->~grpc_server_security_context();
}

/* --- grpc_auth_context --- */

static grpc_auth_property_iterator empty_iterator = {nullptr, 0, nullptr};

const char* grpc_auth_context_peer_identity_property_name(
    const grpc_auth_context* ctx) {
  GRPC_API_TRACE("grpc_auth_context_peer_identity_property_name(ctx=%p)", 1,
                 (ctx));
  return ctx->peer_identity_property_name();
}

int grpc_auth_context_set_peer_identity_property_name(grpc_auth_context* ctx,
                                                      const char* name) {
  grpc_auth_property_iterator it =
      grpc_auth_context_find_properties_by_name(ctx, name);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  GRPC_API_TRACE(
      "grpc_auth_context_set_peer_identity_property_name(ctx=%p, name=%s)", 2,
      (ctx, name));
  if (prop == nullptr) {
    gpr_log(GPR_ERROR, "Property name %s not found in auth context.",
            name != nullptr ? name : "NULL");
    return 0;
  }
  ctx->set_peer_identity_property_name(prop->name);
  return 1;
}

int grpc_auth_context_peer_is_authenticated(const grpc_auth_context* ctx) {
  GRPC_API_TRACE("grpc_auth_context_peer_is_authenticated(ctx=%p)", 1, (ctx));
  return ctx->is_authenticated();
}

grpc_auth_property_iterator grpc_auth_context_property_iterator(
    const grpc_auth_context* ctx) {
  grpc_auth_property_iterator it = empty_iterator;
  GRPC_API_TRACE("grpc_auth_context_property_iterator(ctx=%p)", 1, (ctx));
  if (ctx == nullptr) return it;
  it.ctx = ctx;
  return it;
}

const grpc_auth_property* grpc_auth_property_iterator_next(
    grpc_auth_property_iterator* it) {
  GRPC_API_TRACE("grpc_auth_property_iterator_next(it=%p)", 1, (it));
  if (it == nullptr || it->ctx == nullptr) return nullptr;
  while (it->index == it->ctx->properties().count) {
    if (it->ctx->chained() == nullptr) return nullptr;
    it->ctx = it->ctx->chained();
    it->index = 0;
  }
  if (it->name == nullptr) {
    return &it->ctx->properties().array[it->index++];
  } else {
    while (it->index < it->ctx->properties().count) {
      const grpc_auth_property* prop =
          &it->ctx->properties().array[it->index++];
      GPR_ASSERT(prop->name != nullptr);
      if (strcmp(it->name, prop->name) == 0) {
        return prop;
      }
    }
    /* We could not find the name, try another round. */
    return grpc_auth_property_iterator_next(it);
  }
}

grpc_auth_property_iterator grpc_auth_context_find_properties_by_name(
    const grpc_auth_context* ctx, const char* name) {
  grpc_auth_property_iterator it = empty_iterator;
  GRPC_API_TRACE("grpc_auth_context_find_properties_by_name(ctx=%p, name=%s)",
                 2, (ctx, name));
  if (ctx == nullptr || name == nullptr) return empty_iterator;
  it.ctx = ctx;
  it.name = name;
  return it;
}

grpc_auth_property_iterator grpc_auth_context_peer_identity(
    const grpc_auth_context* ctx) {
  GRPC_API_TRACE("grpc_auth_context_peer_identity(ctx=%p)", 1, (ctx));
  if (ctx == nullptr) return empty_iterator;
  return grpc_auth_context_find_properties_by_name(
      ctx, ctx->peer_identity_property_name());
}

void grpc_auth_context::ensure_capacity() {
  if (properties_.count == properties_.capacity) {
    properties_.capacity =
        std::max(properties_.capacity + 8, properties_.capacity * 2);
    properties_.array = static_cast<grpc_auth_property*>(gpr_realloc(
        properties_.array, properties_.capacity * sizeof(grpc_auth_property)));
  }
}

void grpc_auth_context::add_property(const char* name, const char* value,
                                     size_t value_length) {
  ensure_capacity();
  grpc_auth_property* prop = &properties_.array[properties_.count++];
  prop->name = gpr_strdup(name);
  prop->value = static_cast<char*>(gpr_malloc(value_length + 1));
  if (value != nullptr) {
    memcpy(prop->value, value, value_length);
  }
  prop->value[value_length] = '\0';
  prop->value_length = value_length;
}

void grpc_auth_context_add_property(grpc_auth_context* ctx, const char* name,
                                    const char* value, size_t value_length) {
  GRPC_API_TRACE(
      "grpc_auth_context_add_property(ctx=%p, name=%s, value=%*.*s, "
      "value_length=%lu)",
      6,
      (ctx, name, (int)value_length, (int)value_length, value,
       (unsigned long)value_length));
  ctx->add_property(name, value, value_length);
}

void grpc_auth_context::add_cstring_property(const char* name,
                                             const char* value) {
  ensure_capacity();
  grpc_auth_property* prop = &properties_.array[properties_.count++];
  prop->name = gpr_strdup(name);
  prop->value = gpr_strdup(value);
  prop->value_length = strlen(value);
}

void grpc_auth_context_add_cstring_property(grpc_auth_context* ctx,
                                            const char* name,
                                            const char* value) {
  GRPC_API_TRACE(
      "grpc_auth_context_add_cstring_property(ctx=%p, name=%s, value=%s)", 3,
      (ctx, name, value));
  ctx->add_cstring_property(name, value);
}

void grpc_auth_property_reset(grpc_auth_property* property) {
  gpr_free(property->name);
  gpr_free(property->value);
  memset(property, 0, sizeof(grpc_auth_property));
}

static void auth_context_pointer_arg_destroy(void* p) {
  if (p != nullptr) {
    static_cast<grpc_auth_context*>(p)->Unref(DEBUG_LOCATION,
                                              "auth_context_pointer_arg");
  }
}

static void* auth_context_pointer_arg_copy(void* p) {
  auto* ctx = static_cast<grpc_auth_context*>(p);
  return ctx == nullptr
             ? nullptr
             : ctx->Ref(DEBUG_LOCATION, "auth_context_pointer_arg").release();
}

static int auth_context_pointer_cmp(void* a, void* b) {
  return grpc_core::QsortCompare(a, b);
}

static const grpc_arg_pointer_vtable auth_context_pointer_vtable = {
    auth_context_pointer_arg_copy, auth_context_pointer_arg_destroy,
    auth_context_pointer_cmp};

grpc_arg grpc_auth_context_to_arg(grpc_auth_context* c) {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_AUTH_CONTEXT_ARG), c,
      &auth_context_pointer_vtable);
}

grpc_auth_context* grpc_auth_context_from_arg(const grpc_arg* arg) {
  if (strcmp(arg->key, GRPC_AUTH_CONTEXT_ARG) != 0) return nullptr;
  if (arg->type != GRPC_ARG_POINTER) {
    gpr_log(GPR_ERROR, "Invalid type %d for arg %s", arg->type,
            GRPC_AUTH_CONTEXT_ARG);
    return nullptr;
  }
  return static_cast<grpc_auth_context*>(arg->value.pointer.p);
}

grpc_auth_context* grpc_find_auth_context_in_args(
    const grpc_channel_args* args) {
  size_t i;
  if (args == nullptr) return nullptr;
  for (i = 0; i < args->num_args; i++) {
    grpc_auth_context* p = grpc_auth_context_from_arg(&args->args[i]);
    if (p != nullptr) return p;
  }
  return nullptr;
}
