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

#include "src/core/ext/filters/client_channel/resolver.h"
#include "src/core/lib/iomgr/combiner.h"

grpc_core::DebugOnlyTraceFlag grpc_trace_resolver_refcount(false,
                                                           "resolver_refcount");

void grpc_resolver_init(grpc_resolver* resolver,
                        const grpc_resolver_vtable* vtable,
                        grpc_combiner* combiner) {
  resolver->vtable = vtable;
  resolver->combiner = GRPC_COMBINER_REF(combiner, "resolver");
  gpr_ref_init(&resolver->refs, 1);
}

#ifndef NDEBUG
void grpc_resolver_ref(grpc_resolver* resolver, const char* file, int line,
                       const char* reason) {
  if (grpc_trace_resolver_refcount.enabled()) {
    gpr_atm old_refs = gpr_atm_no_barrier_load(&resolver->refs.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "RESOLVER:%p   ref %" PRIdPTR " -> %" PRIdPTR " %s", resolver,
            old_refs, old_refs + 1, reason);
  }
#else
void grpc_resolver_ref(grpc_resolver* resolver) {
#endif
  gpr_ref(&resolver->refs);
}

#ifndef NDEBUG
void grpc_resolver_unref(grpc_exec_ctx* exec_ctx, grpc_resolver* resolver,
                         const char* file, int line, const char* reason) {
  if (grpc_trace_resolver_refcount.enabled()) {
    gpr_atm old_refs = gpr_atm_no_barrier_load(&resolver->refs.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "RESOLVER:%p unref %" PRIdPTR " -> %" PRIdPTR " %s", resolver,
            old_refs, old_refs - 1, reason);
  }
#else
void grpc_resolver_unref(grpc_exec_ctx* exec_ctx, grpc_resolver* resolver) {
#endif
  if (gpr_unref(&resolver->refs)) {
    grpc_combiner* combiner = resolver->combiner;
    resolver->vtable->destroy(exec_ctx, resolver);
    GRPC_COMBINER_UNREF(exec_ctx, combiner, "resolver");
  }
}

void grpc_resolver_shutdown_locked(grpc_exec_ctx* exec_ctx,
                                   grpc_resolver* resolver) {
  resolver->vtable->shutdown_locked(exec_ctx, resolver);
}

void grpc_resolver_channel_saw_error_locked(grpc_exec_ctx* exec_ctx,
                                            grpc_resolver* resolver) {
  resolver->vtable->channel_saw_error_locked(exec_ctx, resolver);
}

void grpc_resolver_next_locked(grpc_exec_ctx* exec_ctx, grpc_resolver* resolver,
                               grpc_channel_args** result,
                               grpc_closure* on_complete) {
  resolver->vtable->next_locked(exec_ctx, resolver, result, on_complete);
}
