/*
 *
 * Copyright 2016, Google Inc.
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

#include "src/core/lib/security/credentials/iam/iam_credentials.h"

#include <string.h>

#include "src/core/lib/surface/api_trace.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

static void iam_destruct(grpc_call_credentials *creds) {
  grpc_google_iam_credentials *c = (grpc_google_iam_credentials *)creds;
  grpc_credentials_md_store_unref(c->iam_md);
}

static void iam_get_request_metadata(grpc_exec_ctx *exec_ctx,
                                     grpc_call_credentials *creds,
                                     grpc_polling_entity *pollent,
                                     grpc_auth_metadata_context context,
                                     grpc_credentials_metadata_cb cb,
                                     void *user_data) {
  grpc_google_iam_credentials *c = (grpc_google_iam_credentials *)creds;
  cb(exec_ctx, user_data, c->iam_md->entries, c->iam_md->num_entries,
     GRPC_CREDENTIALS_OK);
}

static grpc_call_credentials_vtable iam_vtable = {iam_destruct,
                                                  iam_get_request_metadata};

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
  c = gpr_malloc(sizeof(grpc_google_iam_credentials));
  memset(c, 0, sizeof(grpc_google_iam_credentials));
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
