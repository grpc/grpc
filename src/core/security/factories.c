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

#include "src/core/security/credentials.h"
#include "src/core/security/security_context.h"
#include "src/core/surface/lame_client.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

grpc_channel *grpc_secure_channel_create(grpc_credentials *creds,
                                         const char *target,
                                         const grpc_channel_args *args) {
  grpc_secure_channel_factory factories[] = {
      {GRPC_CREDENTIALS_TYPE_SSL, grpc_ssl_channel_create},
      {GRPC_CREDENTIALS_TYPE_FAKE_TRANSPORT_SECURITY,
       grpc_fake_transport_security_channel_create}};
  return grpc_secure_channel_create_with_factories(
      factories, GPR_ARRAY_SIZE(factories), creds, target, args);
}

grpc_server *grpc_secure_server_create(grpc_server_credentials *creds,
                                       grpc_completion_queue *cq,
                                       const grpc_channel_args *args) {
  grpc_security_status status = GRPC_SECURITY_ERROR;
  grpc_security_context *ctx = NULL;
  grpc_server *server = NULL;
  if (creds == NULL) return NULL; /* TODO(ctiller): Return lame server. */

  if (!strcmp(creds->type, GRPC_CREDENTIALS_TYPE_SSL)) {
    status = grpc_ssl_server_security_context_create(
        grpc_ssl_server_credentials_get_config(creds), &ctx);
  } else if (!strcmp(creds->type,
                     GRPC_CREDENTIALS_TYPE_FAKE_TRANSPORT_SECURITY)) {
    ctx = grpc_fake_server_security_context_create();
    status = GRPC_SECURITY_OK;
  }

  if (status != GRPC_SECURITY_OK) {
    gpr_log(GPR_ERROR,
            "Unable to create secure server with credentials of type %s.",
            creds->type);
    return NULL; /* TODO(ctiller): Return lame server. */
  }
  server = grpc_secure_server_create_internal(cq, args, ctx);
  grpc_security_context_unref(ctx);
  return server;
}