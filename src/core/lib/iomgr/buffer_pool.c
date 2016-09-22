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

#include "src/core/lib/iomgr/buffer_pool.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/combiner.h"

typedef bool (*bpstate_func)(grpc_exec_ctx *exec_ctx,
                             grpc_buffer_pool *buffer_pool);

struct grpc_buffer_pool {
  grpc_combiner *combiner;
  int64_t size;
  int64_t free_pool;

  bool step_scheduled;
  bool reclaiming;
  grpc_closure bpstep_closure;
};

/*******************************************************************************
 * list management
 */

void bulist_add(grpc_buffer_user *buffer_user, grpc_bulist list);
bool bulist_empty(grpc_buffer_pool *buffer_pool, grpc_bulist list);
grpc_buffer_user *bulist_head(grpc_buffer_pool *buffer_pool, grpc_bulist list);
grpc_buffer_user *bulist_pop(grpc_buffer_pool *buffer_pool, grpc_bulist list);
void bulist_remove(grpc_buffer_user *buffer_pool, grpc_bulist list);

/*******************************************************************************
 * buffer pool state machine
 */

static bool bpalloc(grpc_exec_ctx *exec_ctx, grpc_buffer_pool *buffer_pool);
static bool bpscavenge(grpc_exec_ctx *exec_ctx, grpc_buffer_pool *buffer_pool);
static bool bpreclaim(grpc_exec_ctx *exec_ctx, grpc_buffer_pool *buffer_pool,
                      bool destructive);

static void bpstep(grpc_exec_ctx *exec_ctx, void *bp, grpc_error *error) {
  grpc_buffer_pool *buffer_pool = bp;
  buffer_pool->step_scheduled = false;
  do {
    if (bpalloc(exec_ctx, buffer_pool)) return;
  } while (bpscavenge(exec_ctx, buffer_pool));
  bpreclaim(exec_ctx, buffer_pool, false) ||
      bpreclaim(exec_ctx, buffer_pool, true);
}

static void bpstep_sched(grpc_exec_ctx *exec_ctx,
                         grpc_buffer_pool *buffer_pool) {
  if (buffer_pool->step_scheduled) return;
  buffer_pool->step_scheduled = true;
  grpc_combiner_execute_finally(exec_ctx, buffer_pool->combiner,
                                &buffer_pool->bpstep_closure, GRPC_ERROR_NONE,
                                false);
}

/* returns true if all allocations are completed */
static bool bpalloc(grpc_exec_ctx *exec_ctx, grpc_buffer_pool *buffer_pool) {
  grpc_buffer_user *buffer_user;
  while ((buffer_user =
              bulist_head(buffer_pool, GRPC_BULIST_AWAITING_ALLOCATION))) {
    gpr_mu_lock(&buffer_user->mu);
    if (buffer_user->free_pool < 0 &&
        -buffer_user->free_pool < buffer_pool->free_pool) {
      buffer_pool->free_pool += buffer_user->free_pool;
      buffer_user->free_pool = 0;
    }
    if (buffer_user->free_pool >= 0) {
      buffer_user->allocating = false;
      grpc_exec_ctx_enqueue_list(exec_ctx, &buffer_user->on_allocated, NULL);
      bulist_remove(buffer_user, GRPC_BULIST_AWAITING_ALLOCATION);
      gpr_mu_unlock(&buffer_user->mu);
    } else {
      gpr_mu_unlock(&buffer_user->mu);
      return false;
    }
  }
  return true;
}

/* returns true if any memory could be reclaimed from buffers */
static bool bpscavenge(grpc_exec_ctx *exec_ctx, grpc_buffer_pool *buffer_pool) {
  grpc_buffer_user *buffer_user;
  while ((buffer_user =
              bulist_pop(buffer_pool, GRPC_BULIST_NON_EMPTY_FREE_POOL))) {
    gpr_mu_lock(&buffer_user->mu);
    if (buffer_pool->free_pool > 0) {
      buffer_pool->free_pool += buffer_user->free_pool;
      buffer_user->free_pool = 0;
      gpr_mu_unlock(&buffer_user->mu);
      return true;
    } else {
      gpr_mu_unlock(&buffer_user->mu);
    }
  }
  return false;
}

/* returns true if reclaimation is proceeding */
static bool bpreclaim(grpc_exec_ctx *exec_ctx, grpc_buffer_pool *buffer_pool,
                      bool destructive) {
  if (buffer_pool->reclaiming) return true;
  grpc_bulist list = destructive ? GRPC_BULIST_RECLAIMER_BENIGN
                                 : GRPC_BULIST_RECLAIMER_DESTRUCTIVE;
  grpc_buffer_user *buffer_user = bulist_pop(buffer_pool, list);
  if (buffer_user == NULL) return false;
  buffer_pool->reclaiming = true;
  grpc_exec_ctx_sched(exec_ctx, buffer_user->reclaimers[destructive],
                      GRPC_ERROR_NONE, NULL);
  buffer_user->reclaimers[destructive] = NULL;
  return true;
}

/*******************************************************************************
 * grpc_buffer_pool internal implementation
 */

static void bu_allocate(grpc_exec_ctx *exec_ctx, void *bu, grpc_error *error) {
  grpc_buffer_user *buffer_user = bu;
  if (bulist_empty(buffer_user->buffer_pool, GRPC_BULIST_AWAITING_ALLOCATION)) {
    bpstep_sched(exec_ctx, buffer_user->buffer_pool);
  }
  bulist_add(buffer_user, GRPC_BULIST_AWAITING_ALLOCATION);
}

static void bu_add_to_free_pool(grpc_exec_ctx *exec_ctx, void *bu,
                                grpc_error *error) {
  grpc_buffer_user *buffer_user = bu;
  if (!bulist_empty(buffer_user->buffer_pool,
                    GRPC_BULIST_AWAITING_ALLOCATION) &&
      bulist_empty(buffer_user->buffer_pool, GRPC_BULIST_NON_EMPTY_FREE_POOL)) {
    bpstep_sched(exec_ctx, buffer_user->buffer_pool);
  }
  bulist_add(buffer_user, GRPC_BULIST_AWAITING_ALLOCATION);
}

static void bu_post_benign_reclaimer(grpc_exec_ctx *exec_ctx, void *bu,
                                     grpc_error *error) {
  grpc_buffer_user *buffer_user = bu;
  if (!bulist_empty(buffer_user->buffer_pool,
                    GRPC_BULIST_AWAITING_ALLOCATION) &&
      bulist_empty(buffer_user->buffer_pool, GRPC_BULIST_NON_EMPTY_FREE_POOL) &&
      bulist_empty(buffer_user->buffer_pool, GRPC_BULIST_RECLAIMER_BENIGN)) {
    bpstep_sched(exec_ctx, buffer_user->buffer_pool);
  }
  bulist_add(buffer_user, GRPC_BULIST_RECLAIMER_BENIGN);
}

static void bu_post_destructive_reclaimer(grpc_exec_ctx *exec_ctx, void *bu,
                                          grpc_error *error) {
  grpc_buffer_user *buffer_user = bu;
  if (!bulist_empty(buffer_user->buffer_pool,
                    GRPC_BULIST_AWAITING_ALLOCATION) &&
      bulist_empty(buffer_user->buffer_pool, GRPC_BULIST_NON_EMPTY_FREE_POOL) &&
      bulist_empty(buffer_user->buffer_pool, GRPC_BULIST_RECLAIMER_BENIGN) &&
      bulist_empty(buffer_user->buffer_pool,
                   GRPC_BULIST_RECLAIMER_DESTRUCTIVE)) {
    bpstep_sched(exec_ctx, buffer_user->buffer_pool);
  }
  bulist_add(buffer_user, GRPC_BULIST_RECLAIMER_DESTRUCTIVE);
}

/*******************************************************************************
 * grpc_buffer_pool api
 */

grpc_buffer_pool *grpc_buffer_pool_create(void) {
  grpc_buffer_pool *buffer_pool = gpr_malloc(sizeof(*buffer_pool));
  buffer_pool->combiner = grpc_combiner_create(NULL);
  buffer_pool->free_pool = INT64_MAX;
  buffer_pool->size = INT64_MAX;
  grpc_closure_init(&buffer_pool->bpstep_closure, bpstep, buffer_pool);
  return buffer_pool;
}

/*******************************************************************************
 * grpc_buffer_user api
 */

void grpc_buffer_user_init(grpc_buffer_user *buffer_user,
                           grpc_buffer_pool *buffer_pool) {
  buffer_user->buffer_pool = buffer_pool;
  grpc_closure_init(&buffer_user->allocate_closure, &bu_allocate, buffer_user);
  grpc_closure_init(&buffer_user->add_to_free_pool_closure,
                    &bu_add_to_free_pool, buffer_user);
  grpc_closure_init(&buffer_user->post_reclaimer_closure[0],
                    &bu_post_benign_reclaimer, buffer_user);
  grpc_closure_init(&buffer_user->post_reclaimer_closure[1],
                    &bu_post_destructive_reclaimer, buffer_user);
  gpr_mu_init(&buffer_user->mu);
  buffer_user->allocated = 0;
  buffer_user->free_pool = 0;
  grpc_closure_list_init(&buffer_user->on_allocated);
  buffer_user->allocating = false;
  buffer_user->added_to_free_pool = false;
}

void grpc_buffer_user_alloc(grpc_exec_ctx *exec_ctx,
                            grpc_buffer_user *buffer_user, size_t size,
                            grpc_closure *optional_on_done) {
  gpr_mu_lock(&buffer_user->mu);
  buffer_user->allocated += size;
  buffer_user->free_pool -= size;
  if (buffer_user->free_pool < 0) {
    grpc_closure_list_append(&buffer_user->on_allocated, optional_on_done,
                             GRPC_ERROR_NONE);
    if (!buffer_user->allocating) {
      buffer_user->allocating = true;
      grpc_combiner_execute(exec_ctx, buffer_user->buffer_pool->combiner,
                            &buffer_user->allocate_closure, GRPC_ERROR_NONE);
    }
  } else {
    grpc_exec_ctx_sched(exec_ctx, optional_on_done, GRPC_ERROR_NONE, NULL);
  }
  gpr_mu_unlock(&buffer_user->mu);
}

void grpc_buffer_user_free(grpc_exec_ctx *exec_ctx,
                           grpc_buffer_user *buffer_user, size_t size) {
  gpr_mu_lock(&buffer_user->mu);
  GPR_ASSERT(buffer_user->allocated >= (int64_t)size);
  bool was_zero_or_negative = buffer_user->free_pool <= 0;
  buffer_user->free_pool += size;
  buffer_user->allocated -= size;
  bool is_bigger_than_zero = buffer_user->free_pool > 0;
  if (is_bigger_than_zero && was_zero_or_negative &&
      !buffer_user->added_to_free_pool) {
    buffer_user->added_to_free_pool = true;
    grpc_combiner_execute(exec_ctx, buffer_user->buffer_pool->combiner,
                          &buffer_user->add_to_free_pool_closure,
                          GRPC_ERROR_NONE);
  }
  gpr_mu_unlock(&buffer_user->mu);
}

void grpc_buffer_user_post_reclaimer(grpc_exec_ctx *exec_ctx,
                                     grpc_buffer_user *buffer_user,
                                     bool destructive, grpc_closure *closure) {
  GPR_ASSERT(buffer_user->reclaimers[destructive] == NULL);
  buffer_user->reclaimers[destructive] = closure;
  grpc_combiner_execute(exec_ctx, buffer_user->buffer_pool->combiner,
                        &buffer_user->post_reclaimer_closure[destructive],
                        GRPC_ERROR_NONE);
}
