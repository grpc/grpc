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

static void iam_destruct(grpc_exec_ctx *exec_ctx,
                         grpc_call_credentials *creds) {
  grpc_google_iam_credentials *c = (grpc_google_iam_credentials *)creds;
  grpc_credentials_md_store_unref(exec_ctx, c->iam_md);
}

static void iam_get_request_metadata(grpc_exec_ctx *exec_ctx,
                                     grpc_call_credentials *creds,
                                     grpc_polling_entity *pollent,
                                     grpc_auth_metadata_context context,
                                     grpc_credentials_metadata_cb cb,
                                     void *user_data) {
  grpc_google_iam_credentials *c = (grpc_google_iam_credentials *)creds;
  cb(exec_ctx, user_data, c->iam_md->entries, c->iam_md->num_entries,
     GRPC_CREDENTIALS_OK, NULL);
}

static grpc_call_credentials_vtable iam_vtable = {iam_destruct,
                                                  iam_get_request_metadata,
                                                  NULL};

grpc_call_credentials *grpc_google_iam_credentials_create(
    const char *token, const char *authority_selector, void *reserved) {
  grpc_google_iam_credentials *c;
  GRPC_API_TRACE(
      "grpc_iam_credentials_create(token=%s, authority_selector=%s, "
      "reserved=%p)",
      3, (token, authority_selector, reserved));
  GPR_ASSERT(reserved == NULL);
  GPR_ASSERT(token != NULL);
  GPR_ASSERT(authority_selector != NULL);
  c = gpr_zalloc(sizeof(grpc_google_iam_credentials));
  c->base.type = GRPC_CALL_CREDENTIALS_TYPE_IAM;
  c->base.vtable = &iam_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  c->iam_md = grpc_credentials_md_store_create(2);
  grpc_credentials_md_store_add_cstrings(
      c->iam_md, GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY, token);
  grpc_credentials_md_store_add_cstrings(
      c->iam_md, GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY, authority_selector);
  return &c->base;
}
