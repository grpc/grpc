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

#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

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

grpc_client_security_context *grpc_client_security_context_create(void) {
  grpc_client_security_context *ctx =
      gpr_malloc(sizeof(grpc_client_security_context));
  memset(ctx, 0, sizeof(grpc_client_security_context));
  return ctx;
}

void grpc_client_security_context_destroy(void *ctx) {
  grpc_client_security_context *c = (grpc_client_security_context *)ctx;
  grpc_credentials_unref(c->creds);
  gpr_free(ctx);
}
