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

#include "src/core/lib/iomgr/resource_quota.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/combiner.h"

int grpc_resource_quota_trace = 0;

struct grpc_resource_quota {
  /* refcount */
  gpr_refcount refs;

  /* Master combiner lock: all activity on a quota executes under this combiner
   */
  grpc_combiner *combiner;
  /* Size of the resource quota */
  int64_t size;
  /* Amount of free memory in the resource quota */
  int64_t free_pool;

  /* Has rq_step been scheduled to occur? */
  bool step_scheduled;
  /* Are we currently reclaiming memory */
  bool reclaiming;
  /* Closure around rq_step */
  grpc_closure rq_step_closure;
  /* Closure around rq_reclamation_done */
  grpc_closure rq_reclamation_done_closure;

  /* Roots of all resource user lists */
  grpc_resource_user *roots[GRPC_RULIST_COUNT];

  char *name;
};

/*******************************************************************************
 * list management
 */

static void rulist_add_tail(grpc_resource_user *resource_user,
                            grpc_rulist list) {
  grpc_resource_quota *resource_quota = resource_user->resource_quota;
  grpc_resource_user **root = &resource_quota->roots[list];
  if (*root == NULL) {
    *root = resource_user;
    resource_user->links[list].next = resource_user->links[list].prev =
        resource_user;
  } else {
    resource_user->links[list].next = *root;
    resource_user->links[list].prev = (*root)->links[list].prev;
    resource_user->links[list].next->links[list].prev =
        resource_user->links[list].prev->links[list].next = resource_user;
  }
}

static void rulist_add_head(grpc_resource_user *resource_user,
                            grpc_rulist list) {
  grpc_resource_quota *resource_quota = resource_user->resource_quota;
  grpc_resource_user **root = &resource_quota->roots[list];
  if (*root == NULL) {
    *root = resource_user;
    resource_user->links[list].next = resource_user->links[list].prev =
        resource_user;
  } else {
    resource_user->links[list].next = (*root)->links[list].next;
    resource_user->links[list].prev = *root;
    resource_user->links[list].next->links[list].prev =
        resource_user->links[list].prev->links[list].next = resource_user;
    *root = resource_user;
  }
}

static bool rulist_empty(grpc_resource_quota *resource_quota,
                         grpc_rulist list) {
  return resource_quota->roots[list] == NULL;
}

static grpc_resource_user *rulist_pop(grpc_resource_quota *resource_quota,
                                      grpc_rulist list) {
  grpc_resource_user **root = &resource_quota->roots[list];
  grpc_resource_user *resource_user = *root;
  if (resource_user == NULL) {
    return NULL;
  }
  if (resource_user->links[list].next == resource_user) {
    *root = NULL;
  } else {
    resource_user->links[list].next->links[list].prev =
        resource_user->links[list].prev;
    resource_user->links[list].prev->links[list].next =
        resource_user->links[list].next;
    *root = resource_user->links[list].next;
  }
  resource_user->links[list].next = resource_user->links[list].prev = NULL;
  return resource_user;
}

static void rulist_remove(grpc_resource_user *resource_user, grpc_rulist list) {
  if (resource_user->links[list].next == NULL) return;
  grpc_resource_quota *resource_quota = resource_user->resource_quota;
  if (resource_quota->roots[list] == resource_user) {
    resource_quota->roots[list] = resource_user->links[list].next;
    if (resource_quota->roots[list] == resource_user) {
      resource_quota->roots[list] = NULL;
    }
  }
  resource_user->links[list].next->links[list].prev =
      resource_user->links[list].prev;
  resource_user->links[list].prev->links[list].next =
      resource_user->links[list].next;
}

/*******************************************************************************
 * buffer pool state machine
 */

static bool rq_alloc(grpc_exec_ctx *exec_ctx,
                     grpc_resource_quota *resource_quota);
static bool rq_scavenge(grpc_exec_ctx *exec_ctx,
                        grpc_resource_quota *resource_quota);
static bool rq_reclaim(grpc_exec_ctx *exec_ctx,
                       grpc_resource_quota *resource_quota, bool destructive);

static void rq_step(grpc_exec_ctx *exec_ctx, void *bp, grpc_error *error) {
  grpc_resource_quota *resource_quota = bp;
  resource_quota->step_scheduled = false;
  do {
    if (rq_alloc(exec_ctx, resource_quota)) goto done;
  } while (rq_scavenge(exec_ctx, resource_quota));
  rq_reclaim(exec_ctx, resource_quota, false) ||
      rq_reclaim(exec_ctx, resource_quota, true);
done:
  grpc_resource_quota_internal_unref(exec_ctx, resource_quota);
}

static void rq_step_sched(grpc_exec_ctx *exec_ctx,
                          grpc_resource_quota *resource_quota) {
  if (resource_quota->step_scheduled) return;
  resource_quota->step_scheduled = true;
  grpc_resource_quota_internal_ref(resource_quota);
  grpc_combiner_execute_finally(exec_ctx, resource_quota->combiner,
                                &resource_quota->rq_step_closure,
                                GRPC_ERROR_NONE, false);
}

/* returns true if all allocations are completed */
static bool rq_alloc(grpc_exec_ctx *exec_ctx,
                     grpc_resource_quota *resource_quota) {
  grpc_resource_user *resource_user;
  while ((resource_user =
              rulist_pop(resource_quota, GRPC_RULIST_AWAITING_ALLOCATION))) {
    gpr_mu_lock(&resource_user->mu);
    if (resource_user->free_pool < 0 &&
        -resource_user->free_pool <= resource_quota->free_pool) {
      int64_t amt = -resource_user->free_pool;
      resource_user->free_pool = 0;
      resource_quota->free_pool -= amt;
      if (grpc_resource_quota_trace) {
        gpr_log(GPR_DEBUG, "BP %s %s: grant alloc %" PRId64
                           " bytes; rq_free_pool -> %" PRId64,
                resource_quota->name, resource_user->name, amt,
                resource_quota->free_pool);
      }
    } else if (grpc_resource_quota_trace && resource_user->free_pool >= 0) {
      gpr_log(GPR_DEBUG, "BP %s %s: discard already satisfied alloc request",
              resource_quota->name, resource_user->name);
    }
    if (resource_user->free_pool >= 0) {
      resource_user->allocating = false;
      grpc_exec_ctx_enqueue_list(exec_ctx, &resource_user->on_allocated, NULL);
      gpr_mu_unlock(&resource_user->mu);
    } else {
      rulist_add_head(resource_user, GRPC_RULIST_AWAITING_ALLOCATION);
      gpr_mu_unlock(&resource_user->mu);
      return false;
    }
  }
  return true;
}

/* returns true if any memory could be reclaimed from buffers */
static bool rq_scavenge(grpc_exec_ctx *exec_ctx,
                        grpc_resource_quota *resource_quota) {
  grpc_resource_user *resource_user;
  while ((resource_user =
              rulist_pop(resource_quota, GRPC_RULIST_NON_EMPTY_FREE_POOL))) {
    gpr_mu_lock(&resource_user->mu);
    if (resource_user->free_pool > 0) {
      int64_t amt = resource_user->free_pool;
      resource_user->free_pool = 0;
      resource_quota->free_pool += amt;
      if (grpc_resource_quota_trace) {
        gpr_log(GPR_DEBUG, "BP %s %s: scavenge %" PRId64
                           " bytes; rq_free_pool -> %" PRId64,
                resource_quota->name, resource_user->name, amt,
                resource_quota->free_pool);
      }
      gpr_mu_unlock(&resource_user->mu);
      return true;
    } else {
      gpr_mu_unlock(&resource_user->mu);
    }
  }
  return false;
}

/* returns true if reclamation is proceeding */
static bool rq_reclaim(grpc_exec_ctx *exec_ctx,
                       grpc_resource_quota *resource_quota, bool destructive) {
  if (resource_quota->reclaiming) return true;
  grpc_rulist list = destructive ? GRPC_RULIST_RECLAIMER_DESTRUCTIVE
                                 : GRPC_RULIST_RECLAIMER_BENIGN;
  grpc_resource_user *resource_user = rulist_pop(resource_quota, list);
  if (resource_user == NULL) return false;
  if (grpc_resource_quota_trace) {
    gpr_log(GPR_DEBUG, "BP %s %s: initiate %s reclamation",
            resource_quota->name, resource_user->name,
            destructive ? "destructive" : "benign");
  }
  resource_quota->reclaiming = true;
  grpc_resource_quota_internal_ref(resource_quota);
  grpc_closure *c = resource_user->reclaimers[destructive];
  resource_user->reclaimers[destructive] = NULL;
  grpc_closure_run(exec_ctx, c, GRPC_ERROR_NONE);
  return true;
}

/*******************************************************************************
 * ru_slice: a slice implementation that is backed by a grpc_resource_user
 */

typedef struct {
  gpr_slice_refcount base;
  gpr_refcount refs;
  grpc_resource_user *resource_user;
  size_t size;
} ru_slice_refcount;

static void ru_slice_ref(void *p) {
  ru_slice_refcount *rc = p;
  gpr_ref(&rc->refs);
}

static void ru_slice_unref(void *p) {
  ru_slice_refcount *rc = p;
  if (gpr_unref(&rc->refs)) {
    /* TODO(ctiller): this is dangerous, but I think safe for now:
       we have no guarantee here that we're at a safe point for creating an
       execution context, but we have no way of writing this code otherwise.
       In the future: consider lifting gpr_slice to grpc, and offering an
       internal_{ref,unref} pair that is execution context aware.
       Alternatively,
       make exec_ctx be thread local and 'do the right thing' (whatever that
       is)
       if NULL */
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resource_user_free(&exec_ctx, rc->resource_user, rc->size);
    grpc_exec_ctx_finish(&exec_ctx);
    gpr_free(rc);
  }
}

static gpr_slice ru_slice_create(grpc_resource_user *resource_user,
                                 size_t size) {
  ru_slice_refcount *rc = gpr_malloc(sizeof(ru_slice_refcount) + size);
  rc->base.ref = ru_slice_ref;
  rc->base.unref = ru_slice_unref;
  gpr_ref_init(&rc->refs, 1);
  rc->resource_user = resource_user;
  rc->size = size;
  gpr_slice slice;
  slice.refcount = &rc->base;
  slice.data.refcounted.bytes = (uint8_t *)(rc + 1);
  slice.data.refcounted.length = size;
  return slice;
}

/*******************************************************************************
 * grpc_resource_quota internal implementation
 */

static void ru_allocate(grpc_exec_ctx *exec_ctx, void *bu, grpc_error *error) {
  grpc_resource_user *resource_user = bu;
  if (rulist_empty(resource_user->resource_quota,
                   GRPC_RULIST_AWAITING_ALLOCATION)) {
    rq_step_sched(exec_ctx, resource_user->resource_quota);
  }
  rulist_add_tail(resource_user, GRPC_RULIST_AWAITING_ALLOCATION);
}

static void ru_add_to_free_pool(grpc_exec_ctx *exec_ctx, void *bu,
                                grpc_error *error) {
  grpc_resource_user *resource_user = bu;
  if (!rulist_empty(resource_user->resource_quota,
                    GRPC_RULIST_AWAITING_ALLOCATION) &&
      rulist_empty(resource_user->resource_quota,
                   GRPC_RULIST_NON_EMPTY_FREE_POOL)) {
    rq_step_sched(exec_ctx, resource_user->resource_quota);
  }
  rulist_add_tail(resource_user, GRPC_RULIST_NON_EMPTY_FREE_POOL);
}

static void ru_post_benign_reclaimer(grpc_exec_ctx *exec_ctx, void *bu,
                                     grpc_error *error) {
  grpc_resource_user *resource_user = bu;
  if (!rulist_empty(resource_user->resource_quota,
                    GRPC_RULIST_AWAITING_ALLOCATION) &&
      rulist_empty(resource_user->resource_quota,
                   GRPC_RULIST_NON_EMPTY_FREE_POOL) &&
      rulist_empty(resource_user->resource_quota,
                   GRPC_RULIST_RECLAIMER_BENIGN)) {
    rq_step_sched(exec_ctx, resource_user->resource_quota);
  }
  rulist_add_tail(resource_user, GRPC_RULIST_RECLAIMER_BENIGN);
}

static void ru_post_destructive_reclaimer(grpc_exec_ctx *exec_ctx, void *bu,
                                          grpc_error *error) {
  grpc_resource_user *resource_user = bu;
  if (!rulist_empty(resource_user->resource_quota,
                    GRPC_RULIST_AWAITING_ALLOCATION) &&
      rulist_empty(resource_user->resource_quota,
                   GRPC_RULIST_NON_EMPTY_FREE_POOL) &&
      rulist_empty(resource_user->resource_quota,
                   GRPC_RULIST_RECLAIMER_BENIGN) &&
      rulist_empty(resource_user->resource_quota,
                   GRPC_RULIST_RECLAIMER_DESTRUCTIVE)) {
    rq_step_sched(exec_ctx, resource_user->resource_quota);
  }
  rulist_add_tail(resource_user, GRPC_RULIST_RECLAIMER_DESTRUCTIVE);
}

static void ru_destroy(grpc_exec_ctx *exec_ctx, void *bu, grpc_error *error) {
  grpc_resource_user *resource_user = bu;
  GPR_ASSERT(resource_user->allocated == 0);
  for (int i = 0; i < GRPC_RULIST_COUNT; i++) {
    rulist_remove(resource_user, (grpc_rulist)i);
  }
  grpc_exec_ctx_sched(exec_ctx, resource_user->reclaimers[0],
                      GRPC_ERROR_CANCELLED, NULL);
  grpc_exec_ctx_sched(exec_ctx, resource_user->reclaimers[1],
                      GRPC_ERROR_CANCELLED, NULL);
  grpc_exec_ctx_sched(exec_ctx, (grpc_closure *)gpr_atm_no_barrier_load(
                                    &resource_user->on_done_destroy_closure),
                      GRPC_ERROR_NONE, NULL);
  if (resource_user->free_pool != 0) {
    resource_user->resource_quota->free_pool += resource_user->free_pool;
    rq_step_sched(exec_ctx, resource_user->resource_quota);
  }
}

static void ru_allocated_slices(grpc_exec_ctx *exec_ctx, void *ts,
                                grpc_error *error) {
  grpc_resource_user_slice_allocator *slice_allocator = ts;
  if (error == GRPC_ERROR_NONE) {
    for (size_t i = 0; i < slice_allocator->count; i++) {
      gpr_slice_buffer_add_indexed(
          slice_allocator->dest, ru_slice_create(slice_allocator->resource_user,
                                                 slice_allocator->length));
    }
  }
  grpc_closure_run(exec_ctx, &slice_allocator->on_done, GRPC_ERROR_REF(error));
}

typedef struct {
  int64_t size;
  grpc_resource_quota *resource_quota;
  grpc_closure closure;
} rq_resize_args;

static void rq_resize(grpc_exec_ctx *exec_ctx, void *args, grpc_error *error) {
  rq_resize_args *a = args;
  int64_t delta = a->size - a->resource_quota->size;
  a->resource_quota->size += delta;
  a->resource_quota->free_pool += delta;
  if (delta < 0 && a->resource_quota->free_pool < 0) {
    rq_step_sched(exec_ctx, a->resource_quota);
  } else if (delta > 0 &&
             !rulist_empty(a->resource_quota,
                           GRPC_RULIST_AWAITING_ALLOCATION)) {
    rq_step_sched(exec_ctx, a->resource_quota);
  }
  grpc_resource_quota_internal_unref(exec_ctx, a->resource_quota);
  gpr_free(a);
}

static void rq_reclamation_done(grpc_exec_ctx *exec_ctx, void *bp,
                                grpc_error *error) {
  grpc_resource_quota *resource_quota = bp;
  resource_quota->reclaiming = false;
  rq_step_sched(exec_ctx, resource_quota);
  grpc_resource_quota_internal_unref(exec_ctx, resource_quota);
}

/*******************************************************************************
 * grpc_resource_quota api
 */

grpc_resource_quota *grpc_resource_quota_create(const char *name) {
  grpc_resource_quota *resource_quota = gpr_malloc(sizeof(*resource_quota));
  gpr_ref_init(&resource_quota->refs, 1);
  resource_quota->combiner = grpc_combiner_create(NULL);
  resource_quota->free_pool = INT64_MAX;
  resource_quota->size = INT64_MAX;
  resource_quota->step_scheduled = false;
  resource_quota->reclaiming = false;
  if (name != NULL) {
    resource_quota->name = gpr_strdup(name);
  } else {
    gpr_asprintf(&resource_quota->name, "anonymous_pool_%" PRIxPTR,
                 (intptr_t)resource_quota);
  }
  grpc_closure_init(&resource_quota->rq_step_closure, rq_step, resource_quota);
  grpc_closure_init(&resource_quota->rq_reclamation_done_closure,
                    rq_reclamation_done, resource_quota);
  for (int i = 0; i < GRPC_RULIST_COUNT; i++) {
    resource_quota->roots[i] = NULL;
  }
  return resource_quota;
}

void grpc_resource_quota_internal_unref(grpc_exec_ctx *exec_ctx,
                                        grpc_resource_quota *resource_quota) {
  if (gpr_unref(&resource_quota->refs)) {
    grpc_combiner_destroy(exec_ctx, resource_quota->combiner);
    gpr_free(resource_quota->name);
    gpr_free(resource_quota);
  }
}

void grpc_resource_quota_unref(grpc_resource_quota *resource_quota) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_resource_quota_internal_unref(&exec_ctx, resource_quota);
  grpc_exec_ctx_finish(&exec_ctx);
}

grpc_resource_quota *grpc_resource_quota_internal_ref(
    grpc_resource_quota *resource_quota) {
  gpr_ref(&resource_quota->refs);
  return resource_quota;
}

void grpc_resource_quota_ref(grpc_resource_quota *resource_quota) {
  grpc_resource_quota_internal_ref(resource_quota);
}

void grpc_resource_quota_resize(grpc_resource_quota *resource_quota,
                                size_t size) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  rq_resize_args *a = gpr_malloc(sizeof(*a));
  a->resource_quota = grpc_resource_quota_internal_ref(resource_quota);
  a->size = (int64_t)size;
  grpc_closure_init(&a->closure, rq_resize, a);
  grpc_combiner_execute(&exec_ctx, resource_quota->combiner, &a->closure,
                        GRPC_ERROR_NONE, false);
  grpc_exec_ctx_finish(&exec_ctx);
}

/*******************************************************************************
 * grpc_resource_user channel args api
 */

grpc_resource_quota *grpc_resource_quota_from_channel_args(
    const grpc_channel_args *channel_args) {
  for (size_t i = 0; i < channel_args->num_args; i++) {
    if (0 == strcmp(channel_args->args[i].key, GRPC_ARG_RESOURCE_QUOTA)) {
      if (channel_args->args[i].type == GRPC_ARG_POINTER) {
        return grpc_resource_quota_internal_ref(
            channel_args->args[i].value.pointer.p);
      } else {
        gpr_log(GPR_DEBUG, GRPC_ARG_RESOURCE_QUOTA " should be a pointer");
      }
    }
  }
  return grpc_resource_quota_create(NULL);
}

static void *rq_copy(void *bp) {
  grpc_resource_quota_ref(bp);
  return bp;
}

static void rq_destroy(void *bp) { grpc_resource_quota_unref(bp); }

static int rq_cmp(void *a, void *b) { return GPR_ICMP(a, b); }

const grpc_arg_pointer_vtable *grpc_resource_quota_arg_vtable(void) {
  static const grpc_arg_pointer_vtable vtable = {rq_copy, rq_destroy, rq_cmp};
  return &vtable;
}

/*******************************************************************************
 * grpc_resource_user api
 */

void grpc_resource_user_init(grpc_resource_user *resource_user,
                             grpc_resource_quota *resource_quota,
                             const char *name) {
  resource_user->resource_quota =
      grpc_resource_quota_internal_ref(resource_quota);
  grpc_closure_init(&resource_user->allocate_closure, &ru_allocate,
                    resource_user);
  grpc_closure_init(&resource_user->add_to_free_pool_closure,
                    &ru_add_to_free_pool, resource_user);
  grpc_closure_init(&resource_user->post_reclaimer_closure[0],
                    &ru_post_benign_reclaimer, resource_user);
  grpc_closure_init(&resource_user->post_reclaimer_closure[1],
                    &ru_post_destructive_reclaimer, resource_user);
  grpc_closure_init(&resource_user->destroy_closure, &ru_destroy,
                    resource_user);
  gpr_mu_init(&resource_user->mu);
  resource_user->allocated = 0;
  resource_user->free_pool = 0;
  grpc_closure_list_init(&resource_user->on_allocated);
  resource_user->allocating = false;
  resource_user->added_to_free_pool = false;
  gpr_atm_no_barrier_store(&resource_user->on_done_destroy_closure, 0);
  resource_user->reclaimers[0] = NULL;
  resource_user->reclaimers[1] = NULL;
  for (int i = 0; i < GRPC_RULIST_COUNT; i++) {
    resource_user->links[i].next = resource_user->links[i].prev = NULL;
  }
#ifndef NDEBUG
  resource_user->asan_canary = gpr_malloc(1);
#endif
  if (name != NULL) {
    resource_user->name = gpr_strdup(name);
  } else {
    gpr_asprintf(&resource_user->name, "anonymous_resource_user_%" PRIxPTR,
                 (intptr_t)resource_user);
  }
}

void grpc_resource_user_shutdown(grpc_exec_ctx *exec_ctx,
                                 grpc_resource_user *resource_user,
                                 grpc_closure *on_done) {
  gpr_mu_lock(&resource_user->mu);
  GPR_ASSERT(gpr_atm_no_barrier_load(&resource_user->on_done_destroy_closure) ==
             0);
  gpr_atm_no_barrier_store(&resource_user->on_done_destroy_closure,
                           (gpr_atm)on_done);
  if (resource_user->allocated == 0) {
    grpc_combiner_execute(exec_ctx, resource_user->resource_quota->combiner,
                          &resource_user->destroy_closure, GRPC_ERROR_NONE,
                          false);
  }
  gpr_mu_unlock(&resource_user->mu);
}

void grpc_resource_user_destroy(grpc_exec_ctx *exec_ctx,
                                grpc_resource_user *resource_user) {
#ifndef NDEBUG
  gpr_free(resource_user->asan_canary);
#endif
  grpc_resource_quota_internal_unref(exec_ctx, resource_user->resource_quota);
  gpr_mu_destroy(&resource_user->mu);
  gpr_free(resource_user->name);
}

void grpc_resource_user_alloc(grpc_exec_ctx *exec_ctx,
                              grpc_resource_user *resource_user, size_t size,
                              grpc_closure *optional_on_done) {
  gpr_mu_lock(&resource_user->mu);
  grpc_closure *on_done_destroy = (grpc_closure *)gpr_atm_no_barrier_load(
      &resource_user->on_done_destroy_closure);
  if (on_done_destroy != NULL) {
    /* already shutdown */
    if (grpc_resource_quota_trace) {
      gpr_log(GPR_DEBUG, "BP %s %s: alloc %" PRIdPTR " after shutdown",
              resource_user->resource_quota->name, resource_user->name, size);
    }
    grpc_exec_ctx_sched(
        exec_ctx, optional_on_done,
        GRPC_ERROR_CREATE("Buffer pool user is already shutdown"), NULL);
    gpr_mu_unlock(&resource_user->mu);
    return;
  }
  resource_user->allocated += (int64_t)size;
  resource_user->free_pool -= (int64_t)size;
  if (grpc_resource_quota_trace) {
    gpr_log(GPR_DEBUG, "BP %s %s: alloc %" PRIdPTR "; allocated -> %" PRId64
                       ", free_pool -> %" PRId64,
            resource_user->resource_quota->name, resource_user->name, size,
            resource_user->allocated, resource_user->free_pool);
  }
  if (resource_user->free_pool < 0) {
    grpc_closure_list_append(&resource_user->on_allocated, optional_on_done,
                             GRPC_ERROR_NONE);
    if (!resource_user->allocating) {
      resource_user->allocating = true;
      grpc_combiner_execute(exec_ctx, resource_user->resource_quota->combiner,
                            &resource_user->allocate_closure, GRPC_ERROR_NONE,
                            false);
    }
  } else {
    grpc_exec_ctx_sched(exec_ctx, optional_on_done, GRPC_ERROR_NONE, NULL);
  }
  gpr_mu_unlock(&resource_user->mu);
}

void grpc_resource_user_free(grpc_exec_ctx *exec_ctx,
                             grpc_resource_user *resource_user, size_t size) {
  gpr_mu_lock(&resource_user->mu);
  GPR_ASSERT(resource_user->allocated >= (int64_t)size);
  bool was_zero_or_negative = resource_user->free_pool <= 0;
  resource_user->free_pool += (int64_t)size;
  resource_user->allocated -= (int64_t)size;
  if (grpc_resource_quota_trace) {
    gpr_log(GPR_DEBUG, "BP %s %s: free %" PRIdPTR "; allocated -> %" PRId64
                       ", free_pool -> %" PRId64,
            resource_user->resource_quota->name, resource_user->name, size,
            resource_user->allocated, resource_user->free_pool);
  }
  bool is_bigger_than_zero = resource_user->free_pool > 0;
  if (is_bigger_than_zero && was_zero_or_negative &&
      !resource_user->added_to_free_pool) {
    resource_user->added_to_free_pool = true;
    grpc_combiner_execute(exec_ctx, resource_user->resource_quota->combiner,
                          &resource_user->add_to_free_pool_closure,
                          GRPC_ERROR_NONE, false);
  }
  grpc_closure *on_done_destroy = (grpc_closure *)gpr_atm_no_barrier_load(
      &resource_user->on_done_destroy_closure);
  if (on_done_destroy != NULL && resource_user->allocated == 0) {
    grpc_combiner_execute(exec_ctx, resource_user->resource_quota->combiner,
                          &resource_user->destroy_closure, GRPC_ERROR_NONE,
                          false);
  }
  gpr_mu_unlock(&resource_user->mu);
}

void grpc_resource_user_post_reclaimer(grpc_exec_ctx *exec_ctx,
                                       grpc_resource_user *resource_user,
                                       bool destructive,
                                       grpc_closure *closure) {
  if (gpr_atm_acq_load(&resource_user->on_done_destroy_closure) == 0) {
    GPR_ASSERT(resource_user->reclaimers[destructive] == NULL);
    resource_user->reclaimers[destructive] = closure;
    grpc_combiner_execute(exec_ctx, resource_user->resource_quota->combiner,
                          &resource_user->post_reclaimer_closure[destructive],
                          GRPC_ERROR_NONE, false);
  } else {
    grpc_exec_ctx_sched(exec_ctx, closure, GRPC_ERROR_CANCELLED, NULL);
  }
}

void grpc_resource_user_finish_reclamation(grpc_exec_ctx *exec_ctx,
                                           grpc_resource_user *resource_user) {
  if (grpc_resource_quota_trace) {
    gpr_log(GPR_DEBUG, "BP %s %s: reclamation complete",
            resource_user->resource_quota->name, resource_user->name);
  }
  grpc_combiner_execute(
      exec_ctx, resource_user->resource_quota->combiner,
      &resource_user->resource_quota->rq_reclamation_done_closure,
      GRPC_ERROR_NONE, false);
}

void grpc_resource_user_slice_allocator_init(
    grpc_resource_user_slice_allocator *slice_allocator,
    grpc_resource_user *resource_user, grpc_iomgr_cb_func cb, void *p) {
  grpc_closure_init(&slice_allocator->on_allocated, ru_allocated_slices,
                    slice_allocator);
  grpc_closure_init(&slice_allocator->on_done, cb, p);
  slice_allocator->resource_user = resource_user;
}

void grpc_resource_user_alloc_slices(
    grpc_exec_ctx *exec_ctx,
    grpc_resource_user_slice_allocator *slice_allocator, size_t length,
    size_t count, gpr_slice_buffer *dest) {
  slice_allocator->length = length;
  slice_allocator->count = count;
  slice_allocator->dest = dest;
  grpc_resource_user_alloc(exec_ctx, slice_allocator->resource_user,
                           count * length, &slice_allocator->on_allocated);
}
