/*
 *
 * Copyright 2016 gRPC authors.
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

#include "src/core/lib/security/credentials/iam/iam_credentials.h"

#include <string.h>

#include "src/core/lib/surface/api_trace.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

static void iam_destruct(grpc_call_credentials* creds) {
  grpc_google_iam_credentials* c = (grpc_google_iam_credentials*)creds;
  grpc_credentials_mdelem_array_destroy(&c->md_array);
}

static bool iam_get_request_metadata(grpc_call_credentials* creds,
                                     grpc_polling_entity* pollent,
                                     grpc_auth_metadata_context context,
                                     grpc_credentials_mdelem_array* md_array,
                                     grpc_closure* on_request_metadata,
                                     grpc_error** error) {
  grpc_google_iam_credentials* c = (grpc_google_iam_credentials*)creds;
  grpc_credentials_mdelem_array_append(md_array, &c->md_array);
  return true;
}

static void iam_cancel_get_request_metadata(
    grpc_call_credentials* c, grpc_credentials_mdelem_array* md_array,
    grpc_error* error) {
  GRPC_ERROR_UNREF(error);
}

static grpc_call_credentials_vtable iam_vtable = {
    iam_destruct, iam_get_request_metadata, iam_cancel_get_request_metadata};

grpc_call_credentials* grpc_google_iam_credentials_create(
    const char* token, const char* authority_selector, void* reserved) {
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE(
      "grpc_iam_credentials_create(token=%s, authority_selector=%s, "
      "reserved=%p)",
      3, (token, authority_selector, reserved));
  GPR_ASSERT(reserved == nullptr);
  GPR_ASSERT(token != nullptr);
  GPR_ASSERT(authority_selector != nullptr);
  grpc_google_iam_credentials* c =
      (grpc_google_iam_credentials*)gpr_zalloc(sizeof(*c));
  c->base.type = GRPC_CALL_CREDENTIALS_TYPE_IAM;
  c->base.vtable = &iam_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  grpc_mdelem md = grpc_mdelem_from_slices(
      grpc_slice_from_static_string(GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY),
      grpc_slice_from_copied_string(token));
  grpc_credentials_mdelem_array_add(&c->md_array, md);
  GRPC_MDELEM_UNREF(md);
  md = grpc_mdelem_from_slices(
      grpc_slice_from_static_string(GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY),
      grpc_slice_from_copied_string(authority_selector));
  grpc_credentials_mdelem_array_add(&c->md_array, md);
  GRPC_MDELEM_UNREF(md);

  return &c->base;
}
