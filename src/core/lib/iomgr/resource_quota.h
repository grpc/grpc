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

extern int grpc_resource_quota_trace;

grpc_resource_quota *grpc_resource_quota_internal_ref(
    grpc_resource_quota *resource_quota);
void grpc_resource_quota_internal_unref(grpc_exec_ctx *exec_ctx,
                                        grpc_resource_quota *resource_quota);
grpc_resource_quota *grpc_resource_quota_from_channel_args(
    const grpc_channel_args *channel_args);

typedef enum {
  GRPC_BULIST_AWAITING_ALLOCATION,
  GRPC_BULIST_NON_EMPTY_FREE_POOL,
  GRPC_BULIST_RECLAIMER_BENIGN,
  GRPC_BULIST_RECLAIMER_DESTRUCTIVE,
  GRPC_BULIST_COUNT
} grpc_bulist;

typedef struct grpc_resource_user grpc_resource_user;

typedef struct {
  grpc_resource_user *next;
  grpc_resource_user *prev;
} grpc_resource_user_link;

struct grpc_resource_user {
  grpc_resource_quota *resource_quota;

  grpc_closure allocate_closure;
  grpc_closure add_to_free_pool_closure;

#ifndef NDEBUG
  void *asan_canary;
#endif

  gpr_mu mu;
  int64_t allocated;
  int64_t free_pool;
  grpc_closure_list on_allocated;
  bool allocating;
  bool added_to_free_pool;

  grpc_closure *reclaimers[2];
  grpc_closure post_reclaimer_closure[2];

  grpc_closure destroy_closure;
  gpr_atm on_done_destroy_closure;

  grpc_resource_user_link links[GRPC_BULIST_COUNT];

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

void grpc_resource_user_alloc(grpc_exec_ctx *exec_ctx,
                              grpc_resource_user *resource_user, size_t size,
                              grpc_closure *optional_on_done);
void grpc_resource_user_free(grpc_exec_ctx *exec_ctx,
                             grpc_resource_user *resource_user, size_t size);
void grpc_resource_user_post_reclaimer(grpc_exec_ctx *exec_ctx,
                                       grpc_resource_user *resource_user,
                                       bool destructive, grpc_closure *closure);
void grpc_resource_user_finish_reclaimation(grpc_exec_ctx *exec_ctx,
                                            grpc_resource_user *resource_user);

typedef struct grpc_resource_user_slice_allocator {
  grpc_closure on_allocated;
  grpc_closure on_done;
  size_t length;
  size_t count;
  gpr_slice_buffer *dest;
  grpc_resource_user *resource_user;
} grpc_resource_user_slice_allocator;

void grpc_resource_user_slice_allocator_init(
    grpc_resource_user_slice_allocator *slice_allocator,
    grpc_resource_user *resource_user, grpc_iomgr_cb_func cb, void *p);

void grpc_resource_user_alloc_slices(
    grpc_exec_ctx *exec_ctx,
    grpc_resource_user_slice_allocator *slice_allocator, size_t length,
    size_t count, gpr_slice_buffer *dest);

#endif /* GRPC_CORE_LIB_IOMGR_RESOURCE_QUOTA_H */
