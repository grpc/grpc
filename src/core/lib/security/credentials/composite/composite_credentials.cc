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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/composite/composite_credentials.h"

#include <string.h>

#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/surface/api_trace.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

/* -- Composite call credentials. -- */

static void composite_call_metadata_cb(void* arg, grpc_error* error);

namespace {
struct grpc_composite_call_credentials_metadata_context {
  grpc_composite_call_credentials_metadata_context(
      grpc_composite_call_credentials* composite_creds,
      grpc_polling_entity* pollent, grpc_auth_metadata_context auth_md_context,
      grpc_credentials_mdelem_array* md_array,
      grpc_closure* on_request_metadata)
      : composite_creds(composite_creds),
        pollent(pollent),
        auth_md_context(auth_md_context),
        md_array(md_array),
        on_request_metadata(on_request_metadata) {
    GRPC_CLOSURE_INIT(&internal_on_request_metadata, composite_call_metadata_cb,
                      this, grpc_schedule_on_exec_ctx);
  }

  grpc_composite_call_credentials* composite_creds;
  size_t creds_index = 0;
  grpc_polling_entity* pollent;
  grpc_auth_metadata_context auth_md_context;
  grpc_credentials_mdelem_array* md_array;
  grpc_closure* on_request_metadata;
  grpc_closure internal_on_request_metadata;
};
}  // namespace

grpc_composite_call_credentials::~grpc_composite_call_credentials() {
  for (size_t i = 0; i < inner_.num_creds; i++) {
    inner_.creds_array[i]->Unref();
  }
  gpr_free(inner_.creds_array);
}

static void composite_call_metadata_cb(void* arg, grpc_error* error) {
  grpc_composite_call_credentials_metadata_context* ctx =
      static_cast<grpc_composite_call_credentials_metadata_context*>(arg);
  if (error == GRPC_ERROR_NONE) {
    const grpc_call_credentials_array& inner = ctx->composite_creds->inner();
    /* See if we need to get some more metadata. */
    if (ctx->creds_index < inner.num_creds) {
      grpc_call_credentials* inner_creds =
          inner.creds_array[ctx->creds_index++];
      if (inner_creds->get_request_metadata(
              ctx->pollent, ctx->auth_md_context, ctx->md_array,
              &ctx->internal_on_request_metadata, &error)) {
        // Synchronous response, so call ourselves recursively.
        composite_call_metadata_cb(arg, error);
        GRPC_ERROR_UNREF(error);
      }
      return;
    }
    // We're done!
  }
  GRPC_CLOSURE_SCHED(ctx->on_request_metadata, GRPC_ERROR_REF(error));
  gpr_free(ctx);
}

bool grpc_composite_call_credentials::get_request_metadata(
    grpc_polling_entity* pollent, grpc_auth_metadata_context auth_md_context,
    grpc_credentials_mdelem_array* md_array, grpc_closure* on_request_metadata,
    grpc_error** error) {
  grpc_composite_call_credentials_metadata_context* ctx;
  ctx = grpc_core::New<grpc_composite_call_credentials_metadata_context>(
      this, pollent, auth_md_context, md_array, on_request_metadata);
  bool synchronous = true;
  const grpc_call_credentials_array& inner = ctx->composite_creds->inner();
  while (ctx->creds_index < inner.num_creds) {
    grpc_call_credentials* inner_creds = inner.creds_array[ctx->creds_index++];
    if (inner_creds->get_request_metadata(
            ctx->pollent, ctx->auth_md_context, ctx->md_array,
            &ctx->internal_on_request_metadata, error)) {
      if (*error != GRPC_ERROR_NONE) break;
    } else {
      synchronous = false;  // Async return.
      break;
    }
  }
  if (synchronous) grpc_core::Delete(ctx);
  return synchronous;
}

void grpc_composite_call_credentials::cancel_get_request_metadata(
    grpc_credentials_mdelem_array* md_array, grpc_error* error) {
  for (size_t i = 0; i < inner_.num_creds; ++i) {
    inner_.creds_array[i]->cancel_get_request_metadata(md_array,
                                                       GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
}

static grpc_call_credentials_array get_creds_array(
    grpc_call_credentials** creds_addr) {
  grpc_call_credentials_array result;
  grpc_call_credentials* creds = *creds_addr;
  result.creds_array = creds_addr;
  result.num_creds = 1;
  if (strcmp(creds->type(), GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) == 0) {
    result = *grpc_composite_call_credentials_get_credentials(creds);
  }
  return result;
}

grpc_composite_call_credentials::grpc_composite_call_credentials(
    grpc_call_credentials* creds1, grpc_call_credentials* creds2)
    : grpc_call_credentials(GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) {
  grpc_call_credentials_array creds1_array = get_creds_array(&creds1);
  grpc_call_credentials_array creds2_array = get_creds_array(&creds2);
  inner_.num_creds = creds1_array.num_creds + creds2_array.num_creds;
  size_t creds_array_byte_size =
      inner_.num_creds * sizeof(grpc_call_credentials*);
  inner_.creds_array =
      static_cast<grpc_call_credentials**>(gpr_malloc(creds_array_byte_size));
  for (size_t i = 0; i < creds1_array.num_creds; i++) {
    grpc_call_credentials* cur_creds = creds1_array.creds_array[i]->Ref();
    inner_.creds_array[i] = cur_creds;
  }
  for (size_t i = 0; i < creds2_array.num_creds; i++) {
    grpc_call_credentials* cur_creds = creds2_array.creds_array[i]->Ref();
    inner_.creds_array[i + creds1_array.num_creds] = cur_creds;
  }
}

grpc_call_credentials* grpc_composite_call_credentials_create(
    grpc_call_credentials* creds1, grpc_call_credentials* creds2,
    void* reserved) {
  GRPC_API_TRACE(
      "grpc_composite_call_credentials_create(creds1=%p, creds2=%p, "
      "reserved=%p)",
      3, (creds1, creds2, reserved));
  GPR_ASSERT(reserved == nullptr);
  GPR_ASSERT(creds1 != nullptr);
  GPR_ASSERT(creds2 != nullptr);

  return grpc_core::New<grpc_composite_call_credentials>(creds1, creds2);
}

const grpc_call_credentials_array*
grpc_composite_call_credentials_get_credentials(grpc_call_credentials* creds) {
  const grpc_composite_call_credentials* c =
      reinterpret_cast<const grpc_composite_call_credentials*>(creds);
  GPR_ASSERT(strcmp(creds->type(), GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) == 0);
  return &c->inner();
}

grpc_call_credentials* grpc_credentials_contains_type(
    grpc_call_credentials* creds, const char* type,
    grpc_call_credentials** composite_creds) {
  size_t i;
  if (strcmp(creds->type(), type) == 0) {
    if (composite_creds != nullptr) *composite_creds = nullptr;
    return creds;
  } else if (strcmp(creds->type(), GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) == 0) {
    const grpc_call_credentials_array* inner_creds_array =
        grpc_composite_call_credentials_get_credentials(creds);
    for (i = 0; i < inner_creds_array->num_creds; i++) {
      if (strcmp(type, inner_creds_array->creds_array[i]->type()) == 0) {
        if (composite_creds != nullptr) *composite_creds = creds;
        return inner_creds_array->creds_array[i];
      }
    }
  }
  return nullptr;
}

/* -- Composite channel credentials. -- */

grpc_composite_channel_credentials::~grpc_composite_channel_credentials() {
  inner_creds_->Unref();
  call_creds_->Unref();
}

grpc_security_status
grpc_composite_channel_credentials::create_security_connector(
    grpc_call_credentials* call_creds, const char* target,
    const grpc_channel_args* args, grpc_channel_security_connector** sc,
    grpc_channel_args** new_args) {
  grpc_security_status status = GRPC_SECURITY_ERROR;

  GPR_ASSERT(inner_creds_ != nullptr && call_creds_ != nullptr);
  /* If we are passed a call_creds, create a call composite to pass it
     downstream. */
  if (call_creds != nullptr) {
    grpc_call_credentials* composite_call_creds =
        grpc_composite_call_credentials_create(call_creds_, call_creds,
                                               nullptr);
    status = inner_creds_->create_security_connector(
        composite_call_creds, target, args, sc, new_args);
    composite_call_creds->Unref();
  } else {
    status = inner_creds_->create_security_connector(call_creds_, target, args,
                                                     sc, new_args);
  }
  return status;
}

grpc_channel_credentials*
grpc_composite_channel_credentials::duplicate_without_call_credentials() {
  return inner_creds_->Ref();
}

grpc_channel_credentials* grpc_composite_channel_credentials_create(
    grpc_channel_credentials* channel_creds, grpc_call_credentials* call_creds,
    void* reserved) {
  GPR_ASSERT(channel_creds != nullptr && call_creds != nullptr &&
             reserved == nullptr);
  GRPC_API_TRACE(
      "grpc_composite_channel_credentials_create(channel_creds=%p, "
      "call_creds=%p, reserved=%p)",
      3, (channel_creds, call_creds, reserved));
  return grpc_core::New<grpc_composite_channel_credentials>(channel_creds,
                                                            call_creds);
}
