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

#ifndef GRPC_INTERNAL_CORE_SECURITY_SECURITY_CONTEXT_H
#define GRPC_INTERNAL_CORE_SECURITY_SECURITY_CONTEXT_H

#include "src/core/security/credentials.h"

/* --- grpc_auth_context ---

   High level authentication context object. Can optionally be chained. */

/* Property names are always NULL terminated. */

struct grpc_auth_context {
  struct grpc_auth_context *chained;
  grpc_auth_property *properties;
  size_t property_count;
  gpr_refcount refcount;
  const char *peer_identity_property_name;
};

/* Constructor. */
grpc_auth_context *grpc_auth_context_create(grpc_auth_context *chained,
                                            size_t property_count);

/* Refcounting. */
grpc_auth_context *grpc_auth_context_ref(
    grpc_auth_context *ctx);
void grpc_auth_context_unref(grpc_auth_context *ctx);

grpc_auth_property grpc_auth_property_init_from_cstring(const char *name,
                                                        const char *value);

grpc_auth_property grpc_auth_property_init(const char *name, const char *value,
                                           size_t value_length);

void grpc_auth_property_reset(grpc_auth_property *property);

/* --- grpc_client_security_context ---

   Internal client-side security context. */

typedef struct {
  grpc_credentials *creds;
  grpc_auth_context *auth_context;
} grpc_client_security_context;

grpc_client_security_context *grpc_client_security_context_create(void);
void grpc_client_security_context_destroy(void *ctx);

/* --- grpc_server_security_context ---

   Internal server-side security context. */

typedef struct {
  grpc_auth_context *auth_context;
} grpc_server_security_context;

grpc_server_security_context *grpc_server_security_context_create(void);
void grpc_server_security_context_destroy(void *ctx);

#endif  /* GRPC_INTERNAL_CORE_SECURITY_SECURITY_CONTEXT_H */

