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

#ifndef GRPC_CORE_LIB_IOMGR_BUFFER_POOL_H
#define GRPC_CORE_LIB_IOMGR_BUFFER_POOL_H

#include <grpc/grpc.h>

#include "src/core/lib/iomgr/exec_ctx.h"

grpc_buffer_pool *grpc_buffer_pool_internal_ref(grpc_buffer_pool *buffer_pool);
void grpc_buffer_pool_internal_unref(grpc_exec_ctx *exec_ctx,
                                     grpc_buffer_pool *buffer_pool);

typedef enum {
  GRPC_BULIST_AWAITING_ALLOCATION,
  GRPC_BULIST_NON_EMPTY_FREE_POOL,
  GRPC_BULIST_RECLAIMER_BENIGN,
  GRPC_BULIST_RECLAIMER_DESTRUCTIVE,
  GRPC_BULIST_COUNT
} grpc_bulist;

typedef struct grpc_buffer_user grpc_buffer_user;

struct grpc_buffer_user {
  grpc_buffer_pool *buffer_pool;

  grpc_closure allocate_closure;
  grpc_closure add_to_free_pool_closure;

  gpr_mu mu;
  int64_t allocated;
  int64_t free_pool;
  grpc_closure_list on_allocated;
  bool allocating;
  bool added_to_free_pool;

  grpc_closure *reclaimers[2];
  grpc_closure post_reclaimer_closure[2];

  grpc_buffer_user *next[GRPC_BULIST_COUNT];
};

void grpc_buffer_user_init(grpc_buffer_user *buffer_user,
                           grpc_buffer_pool *buffer_pool);
void grpc_buffer_user_destroy(grpc_exec_ctx *exec_ctx,
                              grpc_buffer_user *buffer_user,
                              grpc_closure *on_done);

void grpc_buffer_user_alloc(grpc_exec_ctx *exec_ctx,
                            grpc_buffer_user *buffer_user, size_t size,
                            grpc_closure *optional_on_done);
void grpc_buffer_user_free(grpc_exec_ctx *exec_ctx,
                           grpc_buffer_user *buffer_user, size_t size);
void grpc_buffer_user_post_reclaimer(grpc_exec_ctx *exec_ctx,
                                     grpc_buffer_user *buffer_user,
                                     bool destructive, grpc_closure *closure);
void grpc_buffer_user_finish_reclaimation(grpc_exec_ctx *exec_ctx,
                                          grpc_buffer_user *buffer_user);

#endif /* GRPC_CORE_LIB_IOMGR_BUFFER_POOL_H */
