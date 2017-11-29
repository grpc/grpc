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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_H

#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/lib/iomgr/iomgr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grpc_resolver grpc_resolver;
typedef struct grpc_resolver_vtable grpc_resolver_vtable;

extern grpc_core::DebugOnlyTraceFlag grpc_trace_resolver_refcount;

/** \a grpc_resolver provides \a grpc_channel_args objects to its caller */
struct grpc_resolver {
  const grpc_resolver_vtable* vtable;
  gpr_refcount refs;
  grpc_combiner* combiner;
};

struct grpc_resolver_vtable {
  void (*destroy)(grpc_exec_ctx* exec_ctx, grpc_resolver* resolver);
  void (*shutdown_locked)(grpc_exec_ctx* exec_ctx, grpc_resolver* resolver);
  void (*channel_saw_error_locked)(grpc_exec_ctx* exec_ctx,
                                   grpc_resolver* resolver);
  void (*next_locked)(grpc_exec_ctx* exec_ctx, grpc_resolver* resolver,
                      grpc_channel_args** result, grpc_closure* on_complete);
};

#ifndef NDEBUG
#define GRPC_RESOLVER_REF(p, r) grpc_resolver_ref((p), __FILE__, __LINE__, (r))
#define GRPC_RESOLVER_UNREF(e, p, r) \
  grpc_resolver_unref((e), (p), __FILE__, __LINE__, (r))
void grpc_resolver_ref(grpc_resolver* policy, const char* file, int line,
                       const char* reason);
void grpc_resolver_unref(grpc_exec_ctx* exec_ctx, grpc_resolver* policy,
                         const char* file, int line, const char* reason);
#else
#define GRPC_RESOLVER_REF(p, r) grpc_resolver_ref((p))
#define GRPC_RESOLVER_UNREF(e, p, r) grpc_resolver_unref((e), (p))
void grpc_resolver_ref(grpc_resolver* policy);
void grpc_resolver_unref(grpc_exec_ctx* exec_ctx, grpc_resolver* policy);
#endif

void grpc_resolver_init(grpc_resolver* resolver,
                        const grpc_resolver_vtable* vtable,
                        grpc_combiner* combiner);

void grpc_resolver_shutdown_locked(grpc_exec_ctx* exec_ctx,
                                   grpc_resolver* resolver);

/** Notification that the channel has seen an error on some address.
    Can be used as a hint that re-resolution is desirable soon.

    Must be called from the combiner passed as a resolver_arg at construction
    time.*/
void grpc_resolver_channel_saw_error_locked(grpc_exec_ctx* exec_ctx,
                                            grpc_resolver* resolver);

/** Get the next result from the resolver.  Expected to set \a *result with
    new channel args and then schedule \a on_complete for execution.

    If resolution is fatally broken, set \a *result to NULL and
    schedule \a on_complete.

    Must be called from the combiner passed as a resolver_arg at construction
    time.*/
void grpc_resolver_next_locked(grpc_exec_ctx* exec_ctx, grpc_resolver* resolver,
                               grpc_channel_args** result,
                               grpc_closure* on_complete);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_H */
