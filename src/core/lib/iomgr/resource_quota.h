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

    There are three kinds of reclamation that take place:
    - an internal reclamation, where cached resource at the resource user level
      is returned to the quota
    - a benign reclamation phase, whereby resources that are in use but are not
      helping anything make progress are reclaimed
    - a destructive reclamation, whereby resources that are helping something
      make progress may be enacted so that at least one part of the system can
      complete.

    These reclamations are tried in priority order, and only one reclamation
    is outstanding for a quota at any given time (meaning that if a destructive
    reclamation makes progress, we may follow up with a benign reclamation).

    Future work will be to expose the current resource pressure so that back
    pressure can be applied to avoid reclamation phases starting.

    Resource users own references to resource quotas, and resource quotas
    maintain lists of users (which users arrange to leave before they are
    destroyed) */

extern int grpc_resource_quota_trace;

grpc_resource_quota *grpc_resource_quota_internal_ref(
    grpc_resource_quota *resource_quota);
void grpc_resource_quota_internal_unref(grpc_exec_ctx *exec_ctx,
                                        grpc_resource_quota *resource_quota);
grpc_resource_quota *grpc_resource_quota_from_channel_args(
    const grpc_channel_args *channel_args);

/* Resource users are kept in (potentially) several intrusive linked lists
   at once. These are the list names. */
typedef enum {
  /* Resource users that are waiting for an allocation */
  GRPC_RULIST_AWAITING_ALLOCATION,
  /* Resource users that have free memory available for internal reclamation */
  GRPC_RULIST_NON_EMPTY_FREE_POOL,
  /* Resource users that have published a benign reclamation is available */
  GRPC_RULIST_RECLAIMER_BENIGN,
  /* Resource users that have published a destructive reclamation is
     available */
  GRPC_RULIST_RECLAIMER_DESTRUCTIVE,
  /* Number of lists: must be last */
  GRPC_RULIST_COUNT
} grpc_rulist;

typedef struct grpc_resource_user grpc_resource_user;

/* Internal linked list pointers for a resource user */
typedef struct {
  grpc_resource_user *next;
  grpc_resource_user *prev;
} grpc_resource_user_link;

struct grpc_resource_user {
  /* The quota this resource user consumes from */
  grpc_resource_quota *resource_quota;

  /* Closure to schedule an allocation onder the resource quota combiner lock */
  grpc_closure allocate_closure;
  /* Closure to publish a non empty free pool under the resource quota combiner
     lock */
  grpc_closure add_to_free_pool_closure;

#ifndef NDEBUG
  /* Canary object to detect leaked resource users with ASAN */
  void *asan_canary;
#endif

  gpr_mu mu;
  /* Total allocated memory outstanding by this resource user;
     always positive */
  int64_t allocated;
  /* The amount of memory this user has cached for its own use: to avoid quota
     contention, each resource user can keep some memory aside from the quota,
     and the quota can pull it back under memory pressure.
     This value can become negative if more memory has been requested than
     existed in the free pool, at which point the quota is consulted to bring
     this value non-negative (asynchronously). */
  int64_t free_pool;
  /* A list of closures to call once free_pool becomes non-negative - ie when
     all outstanding allocations have been granted. */
  grpc_closure_list on_allocated;
  /* True if we are currently trying to allocate from the quota, false if not */
  bool allocating;
  /* True if we are currently trying to add ourselves to the non-free quota
     list, false otherwise */
  bool added_to_free_pool;

  /* Reclaimers: index 0 is the benign reclaimer, 1 is the destructive reclaimer
   */
  grpc_closure *reclaimers[2];
  /* Trampoline closures to finish reclamation and re-enter the quota combiner
     lock */
  grpc_closure post_reclaimer_closure[2];

  /* Closure to execute under the quota combiner to de-register and shutdown the
     resource user */
  grpc_closure destroy_closure;
  /* User supplied closure to call once the user has finished shutting down AND
     all outstanding allocations have been freed */
  gpr_atm on_done_destroy_closure;

  /* Links in the various grpc_rulist lists */
  grpc_resource_user_link links[GRPC_RULIST_COUNT];

  /* The name of this resource user, for debugging/tracing */
  char *name;
};

void grpc_resource_user_init(grpc_resource_user *resource_user,
                             grpc_resource_quota *resource_quota,
                             const char *name);
void grpc_resource_user_shutdown(grpc_exec_ctx *exec_ctx,
                                 grpc_resource_user *resource_user,
                                 grpc_closure *on_done);
void grpc_resource_user_destroy(grpc_exec_ctx *exec_ctx,
                                grpc_resource_user *resource_user);

/* Allocate from the resource user (and it's quota).
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
  grpc_closure on_allocated;
  grpc_closure on_done;
  size_t length;
  size_t count;
  gpr_slice_buffer *dest;
  grpc_resource_user *resource_user;
} grpc_resource_user_slice_allocator;

/* Initialize a slice allocator */
void grpc_resource_user_slice_allocator_init(
    grpc_resource_user_slice_allocator *slice_allocator,
    grpc_resource_user *resource_user, grpc_iomgr_cb_func cb, void *p);

/* Allocate \a count slices of length \a length into \a dest. */
void grpc_resource_user_alloc_slices(
    grpc_exec_ctx *exec_ctx,
    grpc_resource_user_slice_allocator *slice_allocator, size_t length,
    size_t count, gpr_slice_buffer *dest);

#endif /* GRPC_CORE_LIB_IOMGR_RESOURCE_QUOTA_H */
