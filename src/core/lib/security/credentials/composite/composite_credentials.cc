/*
 *
 * Copyright 2015-2016 gRPC authors.
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

#include "src/core/lib/security/credentials/composite/composite_credentials.h"

#include <string.h>

#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/surface/api_trace.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

/* -- Composite call credentials. -- */

typedef struct {
  grpc_composite_call_credentials* composite_creds;
  size_t creds_index;
  grpc_polling_entity* pollent;
  grpc_auth_metadata_context auth_md_context;
  grpc_credentials_mdelem_array* md_array;
  grpc_closure* on_request_metadata;
  grpc_closure internal_on_request_metadata;
} grpc_composite_call_credentials_metadata_context;

static void composite_call_destruct(grpc_exec_ctx* exec_ctx,
                                    grpc_call_credentials* creds) {
  grpc_composite_call_credentials* c = (grpc_composite_call_credentials*)creds;
  for (size_t i = 0; i < c->inner.num_creds; i++) {
    grpc_call_credentials_unref(exec_ctx, c->inner.creds_array[i]);
  }
  gpr_free(c->inner.creds_array);
}

static void composite_call_metadata_cb(grpc_exec_ctx* exec_ctx, void* arg,
                                       grpc_error* error) {
  grpc_composite_call_credentials_metadata_context* ctx =
      (grpc_composite_call_credentials_metadata_context*)arg;
  if (error == GRPC_ERROR_NONE) {
    /* See if we need to get some more metadata. */
    if (ctx->creds_index < ctx->composite_creds->inner.num_creds) {
      grpc_call_credentials* inner_creds =
          ctx->composite_creds->inner.creds_array[ctx->creds_index++];
      if (grpc_call_credentials_get_request_metadata(
              exec_ctx, inner_creds, ctx->pollent, ctx->auth_md_context,
              ctx->md_array, &ctx->internal_on_request_metadata, &error)) {
        // Synchronous response, so call ourselves recursively.
        composite_call_metadata_cb(exec_ctx, arg, error);
        GRPC_ERROR_UNREF(error);
      }
      return;
    }
    // We're done!
  }
  GRPC_CLOSURE_SCHED(exec_ctx, ctx->on_request_metadata, GRPC_ERROR_REF(error));
  gpr_free(ctx);
}

static bool composite_call_get_request_metadata(
    grpc_exec_ctx* exec_ctx, grpc_call_credentials* creds,
    grpc_polling_entity* pollent, grpc_auth_metadata_context auth_md_context,
    grpc_credentials_mdelem_array* md_array, grpc_closure* on_request_metadata,
    grpc_error** error) {
  grpc_composite_call_credentials* c = (grpc_composite_call_credentials*)creds;
  grpc_composite_call_credentials_metadata_context* ctx;
  ctx = (grpc_composite_call_credentials_metadata_context*)gpr_zalloc(
      sizeof(grpc_composite_call_credentials_metadata_context));
  ctx->composite_creds = c;
  ctx->pollent = pollent;
  ctx->auth_md_context = auth_md_context;
  ctx->md_array = md_array;
  ctx->on_request_metadata = on_request_metadata;
  GRPC_CLOSURE_INIT(&ctx->internal_on_request_metadata,
                    composite_call_metadata_cb, ctx, grpc_schedule_on_exec_ctx);
  bool synchronous = true;
  while (ctx->creds_index < ctx->composite_creds->inner.num_creds) {
    grpc_call_credentials* inner_creds =
        ctx->composite_creds->inner.creds_array[ctx->creds_index++];
    if (grpc_call_credentials_get_request_metadata(
            exec_ctx, inner_creds, ctx->pollent, ctx->auth_md_context,
            ctx->md_array, &ctx->internal_on_request_metadata, error)) {
      if (*error != GRPC_ERROR_NONE) break;
    } else {
      synchronous = false;  // Async return.
      break;
    }
  }
  if (synchronous) gpr_free(ctx);
  return synchronous;
}

static void composite_call_cancel_get_request_metadata(
    grpc_exec_ctx* exec_ctx, grpc_call_credentials* creds,
    grpc_credentials_mdelem_array* md_array, grpc_error* error) {
  grpc_composite_call_credentials* c = (grpc_composite_call_credentials*)creds;
  for (size_t i = 0; i < c->inner.num_creds; ++i) {
    grpc_call_credentials_cancel_get_request_metadata(
        exec_ctx, c->inner.creds_array[i], md_array, GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
}

static grpc_call_credentials_vtable composite_call_credentials_vtable = {
    composite_call_destruct, composite_call_get_request_metadata,
    composite_call_cancel_get_request_metadata};

static grpc_call_credentials_array get_creds_array(
    grpc_call_credentials** creds_addr) {
  grpc_call_credentials_array result;
  grpc_call_credentials* creds = *creds_addr;
  result.creds_array = creds_addr;
  result.num_creds = 1;
  if (strcmp(creds->type, GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) == 0) {
    result = *grpc_composite_call_credentials_get_credentials(creds);
  }
  return result;
}

grpc_call_credentials* grpc_composite_call_credentials_create(
    grpc_call_credentials* creds1, grpc_call_credentials* creds2,
    void* reserved) {
  size_t i;
  size_t creds_array_byte_size;
  grpc_call_credentials_array creds1_array;
  grpc_call_credentials_array creds2_array;
  grpc_composite_call_credentials* c;
  GRPC_API_TRACE(
      "grpc_composite_call_credentials_create(creds1=%p, creds2=%p, "
      "reserved=%p)",
      3, (creds1, creds2, reserved));
  GPR_ASSERT(reserved == nullptr);
  GPR_ASSERT(creds1 != nullptr);
  GPR_ASSERT(creds2 != nullptr);
  c = (grpc_composite_call_credentials*)gpr_zalloc(
      sizeof(grpc_composite_call_credentials));
  c->base.type = GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE;
  c->base.vtable = &composite_call_credentials_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  creds1_array = get_creds_array(&creds1);
  creds2_array = get_creds_array(&creds2);
  c->inner.num_creds = creds1_array.num_creds + creds2_array.num_creds;
  creds_array_byte_size = c->inner.num_creds * sizeof(grpc_call_credentials*);
  c->inner.creds_array =
      (grpc_call_credentials**)gpr_zalloc(creds_array_byte_size);
  for (i = 0; i < creds1_array.num_creds; i++) {
    grpc_call_credentials* cur_creds = creds1_array.creds_array[i];
    c->inner.creds_array[i] = grpc_call_credentials_ref(cur_creds);
  }
  for (i = 0; i < creds2_array.num_creds; i++) {
    grpc_call_credentials* cur_creds = creds2_array.creds_array[i];
    c->inner.creds_array[i + creds1_array.num_creds] =
        grpc_call_credentials_ref(cur_creds);
  }
  return &c->base;
}

const grpc_call_credentials_array*
grpc_composite_call_credentials_get_credentials(grpc_call_credentials* creds) {
  const grpc_composite_call_credentials* c =
      (const grpc_composite_call_credentials*)creds;
  GPR_ASSERT(strcmp(creds->type, GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) == 0);
  return &c->inner;
}

grpc_call_credentials* grpc_credentials_contains_type(
    grpc_call_credentials* creds, const char* type,
    grpc_call_credentials** composite_creds) {
  size_t i;
  if (strcmp(creds->type, type) == 0) {
    if (composite_creds != nullptr) *composite_creds = nullptr;
    return creds;
  } else if (strcmp(creds->type, GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) == 0) {
    const grpc_call_credentials_array* inner_creds_array =
        grpc_composite_call_credentials_get_credentials(creds);
    for (i = 0; i < inner_creds_array->num_creds; i++) {
      if (strcmp(type, inner_creds_array->creds_array[i]->type) == 0) {
        if (composite_creds != nullptr) *composite_creds = creds;
        return inner_creds_array->creds_array[i];
      }
    }
  }
  return nullptr;
}

/* -- Composite channel credentials. -- */

static void composite_channel_destruct(grpc_exec_ctx* exec_ctx,
                                       grpc_channel_credentials* creds) {
  grpc_composite_channel_credentials* c =
      (grpc_composite_channel_credentials*)creds;
  grpc_channel_credentials_unref(exec_ctx, c->inner_creds);
  grpc_call_credentials_unref(exec_ctx, c->call_creds);
}

static grpc_security_status composite_channel_create_security_connector(
    grpc_exec_ctx* exec_ctx, grpc_channel_credentials* creds,
    grpc_call_credentials* call_creds, const char* target,
    const grpc_channel_args* args, grpc_channel_security_connector** sc,
    grpc_channel_args** new_args) {
  grpc_composite_channel_credentials* c =
      (grpc_composite_channel_credentials*)creds;
  grpc_security_status status = GRPC_SECURITY_ERROR;

  GPR_ASSERT(c->inner_creds != nullptr && c->call_creds != nullptr &&
             c->inner_creds->vtable != nullptr &&
             c->inner_creds->vtable->create_security_connector != nullptr);
  /* If we are passed a call_creds, create a call composite to pass it
     downstream. */
  if (call_creds != nullptr) {
    grpc_call_credentials* composite_call_creds =
        grpc_composite_call_credentials_create(c->call_creds, call_creds,
                                               nullptr);
    status = c->inner_creds->vtable->create_security_connector(
        exec_ctx, c->inner_creds, composite_call_creds, target, args, sc,
        new_args);
    grpc_call_credentials_unref(exec_ctx, composite_call_creds);
  } else {
    status = c->inner_creds->vtable->create_security_connector(
        exec_ctx, c->inner_creds, c->call_creds, target, args, sc, new_args);
  }
  return status;
}

static grpc_channel_credentials*
composite_channel_duplicate_without_call_credentials(
    grpc_channel_credentials* creds) {
  grpc_composite_channel_credentials* c =
      (grpc_composite_channel_credentials*)creds;
  return grpc_channel_credentials_ref(c->inner_creds);
}

static grpc_channel_credentials_vtable composite_channel_credentials_vtable = {
    composite_channel_destruct, composite_channel_create_security_connector,
    composite_channel_duplicate_without_call_credentials};

grpc_channel_credentials* grpc_composite_channel_credentials_create(
    grpc_channel_credentials* channel_creds, grpc_call_credentials* call_creds,
    void* reserved) {
  grpc_composite_channel_credentials* c =
      (grpc_composite_channel_credentials*)gpr_zalloc(sizeof(*c));
  GPR_ASSERT(channel_creds != nullptr && call_creds != nullptr &&
             reserved == nullptr);
  GRPC_API_TRACE(
      "grpc_composite_channel_credentials_create(channel_creds=%p, "
      "call_creds=%p, reserved=%p)",
      3, (channel_creds, call_creds, reserved));
  c->base.type = channel_creds->type;
  c->base.vtable = &composite_channel_credentials_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  c->inner_creds = grpc_channel_credentials_ref(channel_creds);
  c->call_creds = grpc_call_credentials_ref(call_creds);
  return &c->base;
}
