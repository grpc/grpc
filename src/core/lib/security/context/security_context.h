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
inline grpc_auth_context* grpc_auth_context_ref(grpc_auth_context* policy);
inline void grpc_auth_context_unref(grpc_auth_context* policy);
#endif

void grpc_auth_property_reset(grpc_auth_property* property);

// This type is forward declared as a C struct and we cannot define it as a
// class. Otherwise, compiler will complain about type mismatch due to
// -Wmismatched-tags.
struct grpc_auth_context {
 public:
  explicit grpc_auth_context(grpc_auth_context* chained = nullptr)
      : chained_(chained ? GRPC_AUTH_CONTEXT_REF(chained, "chained")
                         : nullptr) {
    if (chained != nullptr) {
      peer_identity_property_name_ = chained->peer_identity_property_name_;
    }
  }

  ~grpc_auth_context() {
    GRPC_AUTH_CONTEXT_UNREF(chained_, "chained");
    if (properties_.array != nullptr) {
      for (size_t i = 0; i < properties_.count; i++) {
        grpc_auth_property_reset(&properties_.array[i]);
      }
      gpr_free(properties_.array);
    }
  }

  grpc_auth_context* Ref() GRPC_MUST_USE_RESULT {
    refcount_.Ref();
    return this;
  }

  void Unref() {
    if (refcount_.Unref()) {
      grpc_core::Delete(this);
    }
  }

  grpc_core::RefCount::Value refcount() const { return refcount_.get(); }

  grpc_auth_context* chained() const { return chained_; }
  const grpc_auth_property_array& properties() const { return properties_; }

  bool is_authenticated() const {
    return peer_identity_property_name_ != nullptr;
  }
  const char* peer_identity_property_name() const {
    return peer_identity_property_name_;
  }
  void set_peer_identity_property_name(const char* name) {
    peer_identity_property_name_ = name;
  }

  void ensure_capacity();
  void add_property(const char* name, const char* value, size_t value_length);
  void add_cstring_property(const char* name, const char* value);

 private:
  grpc_auth_context* chained_;
  grpc_auth_property_array properties_;
  grpc_core::RefCount refcount_;
  const char* peer_identity_property_name_ = nullptr;
};

/* Creation. */
grpc_auth_context* grpc_auth_context_create(grpc_auth_context* chained);

inline grpc_auth_context* grpc_auth_context_ref(grpc_auth_context* policy) {
  return policy ? policy->Ref() : nullptr;
}
inline void grpc_auth_context_unref(grpc_auth_context* policy) {
  if (policy) policy->Unref();
}

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
  grpc_client_security_context(grpc_call_credentials* creds)
      : creds(creds ? creds->Ref() : nullptr) {}
  ~grpc_client_security_context();

  void set_creds(grpc_call_credentials* creds) {
    if (this->creds) this->creds->Unref();
    this->creds = creds ? creds->Ref() : nullptr;
  }

  grpc_call_credentials* creds;
  grpc_auth_context* auth_context = nullptr;
  grpc_security_context_extension extension;
};

grpc_client_security_context* grpc_client_security_context_create(
    gpr_arena* arena, grpc_call_credentials* creds);
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
