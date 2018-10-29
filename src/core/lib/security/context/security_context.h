/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_CONTEXT_SECURITY_CONTEXT_H
#define GRPC_CORE_LIB_SECURITY_CONTEXT_SECURITY_CONTEXT_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/security/credentials/credentials.h"

extern grpc_core::DebugOnlyTraceFlag grpc_trace_auth_context_refcount;

struct gpr_arena;

/* --- grpc_auth_context ---

   High level authentication context object. Can optionally be chained. */

/* Property names are always NULL terminated. */

struct grpc_auth_property_array {
  grpc_auth_property* array = nullptr;
  size_t count = 0;
  size_t capacity = 0;
};

struct grpc_auth_context {
  grpc_auth_context() { gpr_ref_init(&refcount, 0); }

  struct grpc_auth_context* chained = nullptr;
  grpc_auth_property_array properties;
  gpr_refcount refcount;
  const char* peer_identity_property_name = nullptr;
  grpc_pollset* pollset = nullptr;
};

/* Creation. */
grpc_auth_context* grpc_auth_context_create(grpc_auth_context* chained);

/* Refcounting. */
#ifndef NDEBUG
#define GRPC_AUTH_CONTEXT_REF(p, r) \
  grpc_auth_context_ref((p), __FILE__, __LINE__, (r))
#define GRPC_AUTH_CONTEXT_UNREF(p, r) \
  grpc_auth_context_unref((p), __FILE__, __LINE__, (r))
grpc_auth_context* grpc_auth_context_ref(grpc_auth_context* policy,
                                         const char* file, int line,
                                         const char* reason);
void grpc_auth_context_unref(grpc_auth_context* policy, const char* file,
                             int line, const char* reason);
#else
#define GRPC_AUTH_CONTEXT_REF(p, r) grpc_auth_context_ref((p))
#define GRPC_AUTH_CONTEXT_UNREF(p, r) grpc_auth_context_unref((p))
grpc_auth_context* grpc_auth_context_ref(grpc_auth_context* policy);
void grpc_auth_context_unref(grpc_auth_context* policy);
#endif

void grpc_auth_property_reset(grpc_auth_property* property);

/* --- grpc_security_context_extension ---

   Extension to the security context that may be set in a filter and accessed
   later by a higher level method on a grpc_call object. */

struct grpc_security_context_extension {
  void* instance = nullptr;
  void (*destroy)(void*) = nullptr;
};

/* --- grpc_client_security_context ---

   Internal client-side security context. */

struct grpc_client_security_context {
  grpc_client_security_context() = default;
  ~grpc_client_security_context();

  grpc_call_credentials* creds = nullptr;
  grpc_auth_context* auth_context = nullptr;
  grpc_security_context_extension extension;
};

grpc_client_security_context* grpc_client_security_context_create(
    gpr_arena* arena);
void grpc_client_security_context_destroy(void* ctx);

/* --- grpc_server_security_context ---

   Internal server-side security context. */

struct grpc_server_security_context {
  grpc_server_security_context() = default;
  ~grpc_server_security_context();

  grpc_auth_context* auth_context = nullptr;
  grpc_security_context_extension extension;
};

grpc_server_security_context* grpc_server_security_context_create(
    gpr_arena* arena);
void grpc_server_security_context_destroy(void* ctx);

/* --- Channel args for auth context --- */
#define GRPC_AUTH_CONTEXT_ARG "grpc.auth_context"

grpc_arg grpc_auth_context_to_arg(grpc_auth_context* c);
grpc_auth_context* grpc_auth_context_from_arg(const grpc_arg* arg);
grpc_auth_context* grpc_find_auth_context_in_args(
    const grpc_channel_args* args);

#endif /* GRPC_CORE_LIB_SECURITY_CONTEXT_SECURITY_CONTEXT_H */
