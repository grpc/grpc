/*
 *
 * Copyright 2015-2016, Google Inc.
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

#include "src/core/lib/security/credentials/composite/composite_credentials.h"

#include <string.h>

#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/surface/api_trace.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

/* -- Composite call credentials. -- */

typedef struct {
  grpc_composite_call_credentials *composite_creds;
  size_t creds_index;
  grpc_credentials_md_store *md_elems;
  grpc_auth_metadata_context auth_md_context;
  void *user_data;
  grpc_polling_entity *pollent;
  grpc_credentials_metadata_cb cb;
} grpc_composite_call_credentials_metadata_context;

static void composite_call_destruct(grpc_call_credentials *creds) {
  grpc_composite_call_credentials *c = (grpc_composite_call_credentials *)creds;
  size_t i;
  for (i = 0; i < c->inner.num_creds; i++) {
    grpc_call_credentials_unref(c->inner.creds_array[i]);
  }
  gpr_free(c->inner.creds_array);
}

static void composite_call_md_context_destroy(
    grpc_composite_call_credentials_metadata_context *ctx) {
  grpc_credentials_md_store_unref(ctx->md_elems);
  gpr_free(ctx);
}

static void composite_call_metadata_cb(grpc_exec_ctx *exec_ctx, void *user_data,
                                       grpc_credentials_md *md_elems,
                                       size_t num_md,
                                       grpc_credentials_status status,
                                       const char *error_details) {
  grpc_composite_call_credentials_metadata_context *ctx =
      (grpc_composite_call_credentials_metadata_context *)user_data;
  if (status != GRPC_CREDENTIALS_OK) {
    ctx->cb(exec_ctx, ctx->user_data, NULL, 0, status, error_details);
    return;
  }

  /* Copy the metadata in the context. */
  if (num_md > 0) {
    size_t i;
    for (i = 0; i < num_md; i++) {
      grpc_credentials_md_store_add(ctx->md_elems, md_elems[i].key,
                                    md_elems[i].value);
    }
  }

  /* See if we need to get some more metadata. */
  if (ctx->creds_index < ctx->composite_creds->inner.num_creds) {
    grpc_call_credentials *inner_creds =
        ctx->composite_creds->inner.creds_array[ctx->creds_index++];
    grpc_call_credentials_get_request_metadata(
        exec_ctx, inner_creds, ctx->pollent, ctx->auth_md_context,
        composite_call_metadata_cb, ctx);
    return;
  }

  /* We're done!. */
  ctx->cb(exec_ctx, ctx->user_data, ctx->md_elems->entries,
          ctx->md_elems->num_entries, GRPC_CREDENTIALS_OK, NULL);
  composite_call_md_context_destroy(ctx);
}

static void composite_call_get_request_metadata(
    grpc_exec_ctx *exec_ctx, grpc_call_credentials *creds,
    grpc_polling_entity *pollent, grpc_auth_metadata_context auth_md_context,
    grpc_credentials_metadata_cb cb, void *user_data) {
  grpc_composite_call_credentials *c = (grpc_composite_call_credentials *)creds;
  grpc_composite_call_credentials_metadata_context *ctx;

  ctx = gpr_malloc(sizeof(grpc_composite_call_credentials_metadata_context));
  memset(ctx, 0, sizeof(grpc_composite_call_credentials_metadata_context));
  ctx->auth_md_context = auth_md_context;
  ctx->user_data = user_data;
  ctx->cb = cb;
  ctx->composite_creds = c;
  ctx->pollent = pollent;
  ctx->md_elems = grpc_credentials_md_store_create(c->inner.num_creds);
  grpc_call_credentials_get_request_metadata(
      exec_ctx, c->inner.creds_array[ctx->creds_index++], ctx->pollent,
      auth_md_context, composite_call_metadata_cb, ctx);
}

static grpc_call_credentials_vtable composite_call_credentials_vtable = {
    composite_call_destruct, composite_call_get_request_metadata};

static grpc_call_credentials_array get_creds_array(
    grpc_call_credentials **creds_addr) {
  grpc_call_credentials_array result;
  grpc_call_credentials *creds = *creds_addr;
  result.creds_array = creds_addr;
  result.num_creds = 1;
  if (strcmp(creds->type, GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) == 0) {
    result = *grpc_composite_call_credentials_get_credentials(creds);
  }
  return result;
}

grpc_call_credentials *grpc_composite_call_credentials_create(
    grpc_call_credentials *creds1, grpc_call_credentials *creds2,
    void *reserved) {
  size_t i;
  size_t creds_array_byte_size;
  grpc_call_credentials_array creds1_array;
  grpc_call_credentials_array creds2_array;
  grpc_composite_call_credentials *c;
  GRPC_API_TRACE(
      "grpc_composite_call_credentials_create(creds1=%p, creds2=%p, "
      "reserved=%p)",
      3, (creds1, creds2, reserved));
  GPR_ASSERT(reserved == NULL);
  GPR_ASSERT(creds1 != NULL);
  GPR_ASSERT(creds2 != NULL);
  c = gpr_malloc(sizeof(grpc_composite_call_credentials));
  memset(c, 0, sizeof(grpc_composite_call_credentials));
  c->base.type = GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE;
  c->base.vtable = &composite_call_credentials_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  creds1_array = get_creds_array(&creds1);
  creds2_array = get_creds_array(&creds2);
  c->inner.num_creds = creds1_array.num_creds + creds2_array.num_creds;
  creds_array_byte_size = c->inner.num_creds * sizeof(grpc_call_credentials *);
  c->inner.creds_array = gpr_malloc(creds_array_byte_size);
  memset(c->inner.creds_array, 0, creds_array_byte_size);
  for (i = 0; i < creds1_array.num_creds; i++) {
    grpc_call_credentials *cur_creds = creds1_array.creds_array[i];
    c->inner.creds_array[i] = grpc_call_credentials_ref(cur_creds);
  }
  for (i = 0; i < creds2_array.num_creds; i++) {
    grpc_call_credentials *cur_creds = creds2_array.creds_array[i];
    c->inner.creds_array[i + creds1_array.num_creds] =
        grpc_call_credentials_ref(cur_creds);
  }
  return &c->base;
}

const grpc_call_credentials_array *
grpc_composite_call_credentials_get_credentials(grpc_call_credentials *creds) {
  const grpc_composite_call_credentials *c =
      (const grpc_composite_call_credentials *)creds;
  GPR_ASSERT(strcmp(creds->type, GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) == 0);
  return &c->inner;
}

grpc_call_credentials *grpc_credentials_contains_type(
    grpc_call_credentials *creds, const char *type,
    grpc_call_credentials **composite_creds) {
  size_t i;
  if (strcmp(creds->type, type) == 0) {
    if (composite_creds != NULL) *composite_creds = NULL;
    return creds;
  } else if (strcmp(creds->type, GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) == 0) {
    const grpc_call_credentials_array *inner_creds_array =
        grpc_composite_call_credentials_get_credentials(creds);
    for (i = 0; i < inner_creds_array->num_creds; i++) {
      if (strcmp(type, inner_creds_array->creds_array[i]->type) == 0) {
        if (composite_creds != NULL) *composite_creds = creds;
        return inner_creds_array->creds_array[i];
      }
    }
  }
  return NULL;
}

/* -- Composite channel credentials. -- */

static void composite_channel_destruct(grpc_channel_credentials *creds) {
  grpc_composite_channel_credentials *c =
      (grpc_composite_channel_credentials *)creds;
  grpc_channel_credentials_unref(c->inner_creds);
  grpc_call_credentials_unref(c->call_creds);
}

static grpc_security_status composite_channel_create_security_connector(
    grpc_channel_credentials *creds, grpc_call_credentials *call_creds,
    const char *target, const grpc_channel_args *args,
    grpc_channel_security_connector **sc, grpc_channel_args **new_args) {
  grpc_composite_channel_credentials *c =
      (grpc_composite_channel_credentials *)creds;
  grpc_security_status status = GRPC_SECURITY_ERROR;

  GPR_ASSERT(c->inner_creds != NULL && c->call_creds != NULL &&
             c->inner_creds->vtable != NULL &&
             c->inner_creds->vtable->create_security_connector != NULL);
  /* If we are passed a call_creds, create a call composite to pass it
     downstream. */
  if (call_creds != NULL) {
    grpc_call_credentials *composite_call_creds =
        grpc_composite_call_credentials_create(c->call_creds, call_creds, NULL);
    status = c->inner_creds->vtable->create_security_connector(
        c->inner_creds, composite_call_creds, target, args, sc, new_args);
    grpc_call_credentials_unref(composite_call_creds);
  } else {
    status = c->inner_creds->vtable->create_security_connector(
        c->inner_creds, c->call_creds, target, args, sc, new_args);
  }
  return status;
}

static grpc_channel_credentials_vtable composite_channel_credentials_vtable = {
    composite_channel_destruct, composite_channel_create_security_connector};

grpc_channel_credentials *grpc_composite_channel_credentials_create(
    grpc_channel_credentials *channel_creds, grpc_call_credentials *call_creds,
    void *reserved) {
  grpc_composite_channel_credentials *c = gpr_malloc(sizeof(*c));
  memset(c, 0, sizeof(*c));
  GPR_ASSERT(channel_creds != NULL && call_creds != NULL && reserved == NULL);
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
