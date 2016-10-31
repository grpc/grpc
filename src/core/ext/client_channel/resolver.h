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

#ifndef GRPC_CORE_EXT_CLIENT_CHANNEL_RESOLVER_H
#define GRPC_CORE_EXT_CLIENT_CHANNEL_RESOLVER_H

#include "src/core/ext/client_channel/subchannel.h"
#include "src/core/lib/iomgr/iomgr.h"

typedef struct grpc_resolver grpc_resolver;
typedef struct grpc_resolver_vtable grpc_resolver_vtable;

/** \a grpc_resolver provides \a grpc_channel_args objects to its caller */
struct grpc_resolver {
  const grpc_resolver_vtable *vtable;
  gpr_refcount refs;
};

struct grpc_resolver_vtable {
  void (*destroy)(grpc_exec_ctx *exec_ctx, grpc_resolver *resolver);
  void (*shutdown)(grpc_exec_ctx *exec_ctx, grpc_resolver *resolver);
  void (*channel_saw_error)(grpc_exec_ctx *exec_ctx, grpc_resolver *resolver);
  void (*next)(grpc_exec_ctx *exec_ctx, grpc_resolver *resolver,
               grpc_channel_args **result, grpc_closure *on_complete);
};

#ifdef GRPC_RESOLVER_REFCOUNT_DEBUG
#define GRPC_RESOLVER_REF(p, r) grpc_resolver_ref((p), __FILE__, __LINE__, (r))
#define GRPC_RESOLVER_UNREF(cl, p, r) \
  grpc_resolver_unref((cl), (p), __FILE__, __LINE__, (r))
void grpc_resolver_ref(grpc_resolver *policy, const char *file, int line,
                       const char *reason);
void grpc_resolver_unref(grpc_resolver *policy, grpc_closure_list *closure_list,
                         const char *file, int line, const char *reason);
#else
#define GRPC_RESOLVER_REF(p, r) grpc_resolver_ref((p))
#define GRPC_RESOLVER_UNREF(cl, p, r) grpc_resolver_unref((cl), (p))
void grpc_resolver_ref(grpc_resolver *policy);
void grpc_resolver_unref(grpc_exec_ctx *exec_ctx, grpc_resolver *policy);
#endif

void grpc_resolver_init(grpc_resolver *resolver,
                        const grpc_resolver_vtable *vtable);

void grpc_resolver_shutdown(grpc_exec_ctx *exec_ctx, grpc_resolver *resolver);

/** Notification that the channel has seen an error on some address.
    Can be used as a hint that re-resolution is desirable soon. */
void grpc_resolver_channel_saw_error(grpc_exec_ctx *exec_ctx,
                                     grpc_resolver *resolver);

/** Get the next result from the resolver.  Expected to set \a *result with
    new channel args and then schedule \a on_complete for execution.

    If resolution is fatally broken, set \a *result to NULL and
    schedule \a on_complete. */
void grpc_resolver_next(grpc_exec_ctx *exec_ctx, grpc_resolver *resolver,
                        grpc_channel_args **result, grpc_closure *on_complete);

#endif /* GRPC_CORE_EXT_CLIENT_CHANNEL_RESOLVER_H */
