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

#include "src/core/client_config/resolver.h"

void grpc_resolver_init(grpc_resolver *resolver,
                        const grpc_resolver_vtable *vtable) {
  resolver->vtable = vtable;
  gpr_ref_init(&resolver->refs, 1);
}

#ifdef GRPC_RESOLVER_REFCOUNT_DEBUG
void grpc_resolver_ref(grpc_resolver *resolver, grpc_closure_list *closure_list,
                       const char *file, int line, const char *reason) {
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "RESOLVER:%p   ref %d -> %d %s",
          resolver, (int)resolver->refs.count, (int)resolver->refs.count + 1,
          reason);
#else
void grpc_resolver_ref(grpc_resolver *resolver) {
#endif
  gpr_ref(&resolver->refs);
}

#ifdef GRPC_RESOLVER_REFCOUNT_DEBUG
void grpc_resolver_unref(grpc_resolver *resolver,
                         grpc_closure_list *closure_list, const char *file,
                         int line, const char *reason) {
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "RESOLVER:%p unref %d -> %d %s",
          resolver, (int)resolver->refs.count, (int)resolver->refs.count - 1,
          reason);
#else
void grpc_resolver_unref(grpc_exec_ctx *exec_ctx, grpc_resolver *resolver) {
#endif
  if (gpr_unref(&resolver->refs)) {
    resolver->vtable->destroy(exec_ctx, resolver);
  }
}

void grpc_resolver_shutdown(grpc_exec_ctx *exec_ctx, grpc_resolver *resolver) {
  resolver->vtable->shutdown(exec_ctx, resolver);
}

void grpc_resolver_channel_saw_error(grpc_exec_ctx *exec_ctx,
                                     grpc_resolver *resolver) {
  resolver->vtable->channel_saw_error(exec_ctx, resolver);
}

void grpc_resolver_next(grpc_exec_ctx *exec_ctx, grpc_resolver *resolver,
                        grpc_client_config **target_config,
                        grpc_closure *on_complete) {
  resolver->vtable->next(exec_ctx, resolver, target_config, on_complete);
}
