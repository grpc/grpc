/*
 *
 * Copyright 2016, Google Inc.
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

#ifndef GRPC_CORE_LIB_IOMGR_RESOURCE_QUOTA_H
#define GRPC_CORE_LIB_IOMGR_RESOURCE_QUOTA_H

#include <grpc/grpc.h>

#include "src/core/lib/iomgr/exec_ctx.h"

/** \file Tracks resource usage against a pool.

    The current implementation tracks only memory usage, but in the future
    this may be extended to (for example) threads and file descriptors.

    A grpc_resource_quota represents the pooled resources, and
    grpc_resource_user instances attach to the quota and consume those
    resources. They also offer a vector for reclamation: if we become
    resource constrained, grpc_resource_user instances are asked (in turn) to
    free up whatever they can so that the system as a whole can make progress.

    There are three kinds of reclamation that take place, in order of increasing
    invasiveness:
    - an internal reclamation, where cached resource at the resource user level
      is returned to the quota
    - a benign reclamation phase, whereby resources that are in use but are not
      helping anything make progress are reclaimed
    - a destructive reclamation, whereby resources that are helping something
      make progress may be enacted so that at least one part of the system can
      complete.

    Only one reclamation will be outstanding for a given quota at a given time.
    On each reclamation attempt, the kinds of reclamation are tried in order of
    increasing invasiveness, stopping at the first one that succeeds. Thus, on a
    given reclamation attempt, if internal and benign reclamation both fail, it
    will wind up doing a destructive reclamation. However, the next reclamation
    attempt may then be able to get what it needs via internal or benign
    reclamation, due to resources that may have been freed up by the destructive
    reclamation in the previous attempt.

    Future work will be to expose the current resource pressure so that back
    pressure can be applied to avoid reclamation phases starting.

    Resource users own references to resource quotas, and resource quotas
    maintain lists of users (which users arrange to leave before they are
    destroyed) */

extern int grpc_resource_quota_trace;

grpc_resource_quota *grpc_resource_quota_ref_internal(
    grpc_resource_quota *resource_quota);
void grpc_resource_quota_unref_internal(grpc_exec_ctx *exec_ctx,
                                        grpc_resource_quota *resource_quota);
grpc_resource_quota *grpc_resource_quota_from_channel_args(
    const grpc_channel_args *channel_args);

/* Return a number indicating current memory pressure:
   0.0 ==> no memory usage
   1.0 ==> maximum memory usage */
double grpc_resource_quota_get_memory_pressure(
    grpc_resource_quota *resource_quota);

typedef struct grpc_resource_user grpc_resource_user;

grpc_resource_user *grpc_resource_user_create(
    grpc_resource_quota *resource_quota, const char *name);

/* Returns a borrowed reference to the underlying resource quota for this
   resource user. */
grpc_resource_quota *grpc_resource_user_quota(
    grpc_resource_user *resource_user);

void grpc_resource_user_ref(grpc_resource_user *resource_user);
void grpc_resource_user_unref(grpc_exec_ctx *exec_ctx,
                              grpc_resource_user *resource_user);
void grpc_resource_user_shutdown(grpc_exec_ctx *exec_ctx,
                                 grpc_resource_user *resource_user);

/* Allocate from the resource user (and its quota).
   If optional_on_done is NULL, then allocate immediately. This may push the
   quota over-limit, at which point reclamation will kick in.
   If optional_on_done is non-NULL, it will be scheduled when the allocation has
   been granted by the quota. */
void grpc_resource_user_alloc(grpc_exec_ctx *exec_ctx,
                              grpc_resource_user *resource_user, size_t size,
                              grpc_closure *optional_on_done);
/* Release memory back to the quota */
void grpc_resource_user_free(grpc_exec_ctx *exec_ctx,
                             grpc_resource_user *resource_user, size_t size);
/* Post a memory reclaimer to the resource user. Only one benign and one
   destructive reclaimer can be posted at once. When executed, the reclaimer
   MUST call grpc_resource_user_finish_reclamation before it completes, to
   return control to the resource quota. */
void grpc_resource_user_post_reclaimer(grpc_exec_ctx *exec_ctx,
                                       grpc_resource_user *resource_user,
                                       bool destructive, grpc_closure *closure);
/* Finish a reclamation step */
void grpc_resource_user_finish_reclamation(grpc_exec_ctx *exec_ctx,
                                           grpc_resource_user *resource_user);

/* Helper to allocate slices from a resource user */
typedef struct grpc_resource_user_slice_allocator {
  /* Closure for when a resource user allocation completes */
  grpc_closure on_allocated;
  /* Closure to call when slices have been allocated */
  grpc_closure on_done;
  /* Length of slices to allocate on the current request */
  size_t length;
  /* Number of slices to allocate on the current request */
  size_t count;
  /* Destination for slices to allocate on the current request */
  grpc_slice_buffer *dest;
  /* Parent resource user */
  grpc_resource_user *resource_user;
} grpc_resource_user_slice_allocator;

/* Initialize a slice allocator.
   When an allocation is completed, calls \a cb with arg \p. */
void grpc_resource_user_slice_allocator_init(
    grpc_resource_user_slice_allocator *slice_allocator,
    grpc_resource_user *resource_user, grpc_iomgr_cb_func cb, void *p);

/* Allocate \a count slices of length \a length into \a dest. Only one request
   can be outstanding at a time. */
void grpc_resource_user_alloc_slices(
    grpc_exec_ctx *exec_ctx,
    grpc_resource_user_slice_allocator *slice_allocator, size_t length,
    size_t count, grpc_slice_buffer *dest);

/* Allocate one slice of length \a size synchronously. */
grpc_slice grpc_resource_user_slice_malloc(grpc_exec_ctx *exec_ctx,
                                           grpc_resource_user *resource_user,
                                           size_t size);

#endif /* GRPC_CORE_LIB_IOMGR_RESOURCE_QUOTA_H */
