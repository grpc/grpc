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

#ifndef GRPC_CORE_LIB_SECURITY_CONTEXT_SECURITY_CONTEXT_H
#define GRPC_CORE_LIB_SECURITY_CONTEXT_SECURITY_CONTEXT_H

#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/security/credentials/credentials.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- grpc_auth_context ---

   High level authentication context object. Can optionally be chained. */

/* Property names are always NULL terminated. */

typedef struct {
  grpc_auth_property *array;
  size_t count;
  size_t capacity;
} grpc_auth_property_array;

struct grpc_auth_context {
  struct grpc_auth_context *chained;
  grpc_auth_property_array properties;
  gpr_refcount refcount;
  const char *peer_identity_property_name;
  grpc_pollset *pollset;
};

/* Creation. */
grpc_auth_context *grpc_auth_context_create(grpc_auth_context *chained);

/* Refcounting. */
#ifdef GRPC_AUTH_CONTEXT_REFCOUNT_DEBUG
#define GRPC_AUTH_CONTEXT_REF(p, r) \
  grpc_auth_context_ref((p), __FILE__, __LINE__, (r))
#define GRPC_AUTH_CONTEXT_UNREF(p, r) \
  grpc_auth_context_unref((p), __FILE__, __LINE__, (r))
grpc_auth_context *grpc_auth_context_ref(grpc_auth_context *policy,
                                         const char *file, int line,
                                         const char *reason);
void grpc_auth_context_unref(grpc_auth_context *policy, const char *file,
                             int line, const char *reason);
#else
#define GRPC_AUTH_CONTEXT_REF(p, r) grpc_auth_context_ref((p))
#define GRPC_AUTH_CONTEXT_UNREF(p, r) grpc_auth_context_unref((p))
grpc_auth_context *grpc_auth_context_ref(grpc_auth_context *policy);
void grpc_auth_context_unref(grpc_auth_context *policy);
#endif

void grpc_auth_property_reset(grpc_auth_property *property);

/* --- grpc_security_context_extension ---

   Extension to the security context that may be set in a filter and accessed
   later by a higher level method on a grpc_call object. */

typedef struct {
  void *instance;
  void (*destroy)(void *);
} grpc_security_context_extension;

/* --- grpc_client_security_context ---

   Internal client-side security context. */

typedef struct {
  grpc_call_credentials *creds;
  grpc_auth_context *auth_context;
  grpc_security_context_extension extension;
} grpc_client_security_context;

grpc_client_security_context *grpc_client_security_context_create(void);
void grpc_client_security_context_destroy(void *ctx);

/* --- grpc_server_security_context ---

   Internal server-side security context. */

typedef struct {
  grpc_auth_context *auth_context;
  grpc_security_context_extension extension;
} grpc_server_security_context;

grpc_server_security_context *grpc_server_security_context_create(void);
void grpc_server_security_context_destroy(void *ctx);

/* --- Channel args for auth context --- */
#define GRPC_AUTH_CONTEXT_ARG "grpc.auth_context"

grpc_arg grpc_auth_context_to_arg(grpc_auth_context *c);
grpc_auth_context *grpc_auth_context_from_arg(const grpc_arg *arg);
grpc_auth_context *grpc_find_auth_context_in_args(
    const grpc_channel_args *args);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_SECURITY_CONTEXT_SECURITY_CONTEXT_H */
