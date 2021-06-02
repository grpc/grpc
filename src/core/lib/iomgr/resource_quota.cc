/*
 *
 * Copyright 2016 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/resource_quota.h"

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <string>

#include "absl/strings/str_cat.h"

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/slice/slice_internal.h"

grpc_core::TraceFlag grpc_resource_quota_trace(false, "resource_quota");

#define MEMORY_USAGE_ESTIMATION_MAX 65536

/* Internal linked list pointers for a resource user */
struct grpc_resource_user_link {
  grpc_resource_user* next;
  grpc_resource_user* prev;
};
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

struct grpc_resource_user {
  /* The quota this resource user consumes from */
  grpc_resource_quota* resource_quota;

  /* Closure to schedule an allocation under the resource quota combiner lock */
  grpc_closure allocate_closure;
  /* Closure to publish a non empty free pool under the resource quota combiner
     lock */
  grpc_closure add_to_free_pool_closure;

  /* one ref for each ref call (released by grpc_resource_user_unref), and one
     ref for each byte allocated (released by grpc_resource_user_free) */
  gpr_atm refs;
  /* is this resource user unlocked? starts at 0, increases for each shutdown
     call */
  gpr_atm shutdown;

  gpr_mu mu;
  /* The amount of memory (in bytes) this user has cached for its own use: to
     avoid quota contention, each resource user can keep some memory in
     addition to what it is immediately using (e.g., for caching), and the quota
     can pull it back under memory pressure.
     This value can become negative if more memory has been requested than
     existed in the free pool, at which point the quota is consulted to bring
     this value non-negative (asynchronously). */
  int64_t free_pool;
  /* A list of closures to call once free_pool becomes non-negative - ie when
     all outstanding allocations have been granted. */
  grpc_closure_list on_allocated;
  /* True if we are currently trying to allocate from the quota, false if not */
  bool allocating;
  /* The amount of memory (in bytes) that has been requested from this user
   * asynchronously but hasn't been granted yet. */
  int64_t outstanding_allocations;
  /* True if we are currently trying to add ourselves to the non-free quota
     list, false otherwise */
  bool added_to_free_pool;

  /* The number of threads currently allocated to this resource user */
  gpr_atm num_threads_allocated;

  /* Reclaimers: index 0 is the benign reclaimer, 1 is the destructive reclaimer
   */
  grpc_closure* reclaimers[2];
  /* Reclaimers just posted: once we're in the combiner lock, we'll move them
     to the array above */
  grpc_closure* new_reclaimers[2];
  /* Trampoline closures to finish reclamation and re-enter the quota combiner
     lock */
  grpc_closure post_reclaimer_closure[2];

  /* Closure to execute under the quota combiner to de-register and shutdown the
     resource user */
  grpc_closure destroy_closure;

  /* Links in the various grpc_rulist lists */
  grpc_resource_user_link links[GRPC_RULIST_COUNT];

  /* The name of this resource user, for debugging/tracing */
  std::string name;
};

struct grpc_resource_quota {
  /* refcount */
  gpr_refcount refs;

  /* estimate of current memory usage
     scaled to the range [0..RESOURCE_USAGE_ESTIMATION_MAX] */
  gpr_atm memory_usage_estimation;

  /* Main combiner lock: all activity on a quota executes under this combiner
   * (so no mutex is needed for this data structure) */
  grpc_core::Combiner* combiner;
  /* Size of the resource quota */
  int64_t size;
  /* Amount of free memory in the resource quota */
  int64_t free_pool;
  /* Used size of memory in the resource quota. Updated as soon as the resource
   * users start to allocate or free the memory. */
  gpr_atm used;

  gpr_atm last_size;

  /* Mutex to protect max_threads and num_threads_allocated */
  /* Note: We could have used gpr_atm for max_threads and num_threads_allocated
   * and avoid having this mutex; but in that case, each invocation of the
   * function grpc_resource_user_allocate_threads() would have had to do at
   * least two atomic loads (for max_threads and num_threads_allocated) followed
   * by a CAS (on num_threads_allocated).
   * Moreover, we expect grpc_resource_user_allocate_threads() to be often
   * called concurrently thereby increasing the chances of failing the CAS
   * operation. This additional complexity is not worth the tiny perf gain we
   * may (or may not) have by using atomics */
  gpr_mu thread_count_mu;

  /* Max number of threads allowed */
  int max_threads;

  /* Number of threads currently allocated via this resource_quota object */
  int num_threads_allocated;

  /* Has rq_step been scheduled to occur? */
  bool step_scheduled;

  /* Are we currently reclaiming memory */
  bool reclaiming;

  /* Closure around rq_step */
  grpc_closure rq_step_closure;

  /* Closure around rq_reclamation_done */
  grpc_closure rq_reclamation_done_closure;

  /* This is only really usable for debugging: it's always a stale pointer, but
     a stale pointer that might just be fresh enough to guide us to where the
     reclamation system is stuck */
  grpc_closure* debug_only_last_initiated_reclaimer;
  grpc_resource_user* debug_only_last_reclaimer_resource_user;

  /* Roots of all resource user lists */
  grpc_resource_user* roots[GRPC_RULIST_COUNT];

  std::string name;
};

static void ru_unref_by(grpc_resource_user* resource_user, gpr_atm amount);

/*******************************************************************************
 * list management
 */

static void rulist_add_head(grpc_resource_user* resource_user,
                            grpc_rulist list) {
  grpc_resource_quota* resource_quota = resource_user->resource_quota;
  grpc_resource_user** root = &resource_quota->roots[list];
  if (*root == nullptr) {
    *root = resource_user;
    resource_user->links[list].next = resource_user->links[list].prev =
        resource_user;
  } else {
    resource_user->links[list].next = *root;
    resource_user->links[list].prev = (*root)->links[list].prev;
    resource_user->links[list].next->links[list].prev =
        resource_user->links[list].prev->links[list].next = resource_user;
    *root = resource_user;
  }
}

static void rulist_add_tail(grpc_resource_user* resource_user,
                            grpc_rulist list) {
  grpc_resource_quota* resource_quota = resource_user->resource_quota;
  grpc_resource_user** root = &resource_quota->roots[list];
  if (*root == nullptr) {
    *root = resource_user;
    resource_user->links[list].next = resource_user->links[list].prev =
        resource_user;
  } else {
    resource_user->links[list].next = (*root)->links[list].next;
    resource_user->links[list].prev = *root;
    resource_user->links[list].next->links[list].prev =
        resource_user->links[list].prev->links[list].next = resource_user;
  }
}

static bool rulist_empty(grpc_resource_quota* resource_quota,
                         grpc_rulist list) {
  return resource_quota->roots[list] == nullptr;
}

static grpc_resource_user* rulist_pop_head(grpc_resource_quota* resource_quota,
                                           grpc_rulist list) {
  grpc_resource_user** root = &resource_quota->roots[list];
  grpc_resource_user* resource_user = *root;
  if (resource_user == nullptr) {
    return nullptr;
  }
  if (resource_user->links[list].next == resource_user) {
    *root = nullptr;
  } else {
    resource_user->links[list].next->links[list].prev =
        resource_user->links[list].prev;
    resource_user->links[list].prev->links[list].next =
        resource_user->links[list].next;
    *root = resource_user->links[list].next;
  }
  resource_user->links[list].next = resource_user->links[list].prev = nullptr;
  return resource_user;
}

static void rulist_remove(grpc_resource_user* resource_user, grpc_rulist list) {
  if (resource_user->links[list].next == nullptr) return;
  grpc_resource_quota* resource_quota = resource_user->resource_quota;
  if (resource_quota->roots[list] == resource_user) {
    resource_quota->roots[list] = resource_user->links[list].next;
    if (resource_quota->roots[list] == resource_user) {
      resource_quota->roots[list] = nullptr;
    }
  }
  resource_user->links[list].next->links[list].prev =
      resource_user->links[list].prev;
  resource_user->links[list].prev->links[list].next =
      resource_user->links[list].next;
  resource_user->links[list].next = resource_user->links[list].prev = nullptr;
}

/*******************************************************************************
 * resource quota state machine
 */

static bool rq_alloc(grpc_resource_quota* resource_quota);
static bool rq_reclaim_from_per_user_free_pool(
    grpc_resource_quota* resource_quota);
static bool rq_reclaim(grpc_resource_quota* resource_quota, bool destructive);

static void rq_step(void* rq, grpc_error_handle /*error*/) {
  grpc_resource_quota* resource_quota = static_cast<grpc_resource_quota*>(rq);
  resource_quota->step_scheduled = false;
  do {
    if (rq_alloc(resource_quota)) goto done;
  } while (rq_reclaim_from_per_user_free_pool(resource_quota));

  if (!rq_reclaim(resource_quota, false)) {
    rq_reclaim(resource_quota, true);
  }

done:
  grpc_resource_quota_unref_internal(resource_quota);
}

static void rq_step_sched(grpc_resource_quota* resource_quota) {
  if (resource_quota->step_scheduled) return;
  resource_quota->step_scheduled = true;
  grpc_resource_quota_ref_internal(resource_quota);
  resource_quota->combiner->FinallyRun(&resource_quota->rq_step_closure,
                                       GRPC_ERROR_NONE);
}

/* update the atomically available resource estimate - use no barriers since
   timeliness of delivery really doesn't matter much */
static void rq_update_estimate(grpc_resource_quota* resource_quota) {
  gpr_atm memory_usage_estimation = MEMORY_USAGE_ESTIMATION_MAX;
  if (resource_quota->size != 0) {
    memory_usage_estimation =
        GPR_CLAMP((gpr_atm)((1.0 - ((double)resource_quota->free_pool) /
                                       ((double)resource_quota->size)) *
                            MEMORY_USAGE_ESTIMATION_MAX),
                  0, MEMORY_USAGE_ESTIMATION_MAX);
  }
  gpr_atm_no_barrier_store(&resource_quota->memory_usage_estimation,
                           memory_usage_estimation);
}

/* returns true if all allocations are completed */
static bool rq_alloc(grpc_resource_quota* resource_quota) {
  grpc_resource_user* resource_user;
  while ((resource_user = rulist_pop_head(resource_quota,
                                          GRPC_RULIST_AWAITING_ALLOCATION))) {
    gpr_mu_lock(&resource_user->mu);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
      gpr_log(GPR_INFO,
              "RQ: check allocation for user %p shutdown=%" PRIdPTR
              " free_pool=%" PRId64 " outstanding_allocations=%" PRId64,
              resource_user, gpr_atm_no_barrier_load(&resource_user->shutdown),
              resource_user->free_pool, resource_user->outstanding_allocations);
    }
    if (gpr_atm_no_barrier_load(&resource_user->shutdown)) {
      resource_user->allocating = false;
      grpc_closure_list_fail_all(
          &resource_user->on_allocated,
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Resource user shutdown"));
      int64_t aborted_allocations = resource_user->outstanding_allocations;
      resource_user->outstanding_allocations = 0;
      resource_user->free_pool += aborted_allocations;
      grpc_core::ExecCtx::RunList(DEBUG_LOCATION, &resource_user->on_allocated);
      gpr_mu_unlock(&resource_user->mu);
      if (aborted_allocations > 0) {
        ru_unref_by(resource_user, static_cast<gpr_atm>(aborted_allocations));
      }
      continue;
    }
    if (resource_user->free_pool < 0 &&
        -resource_user->free_pool <= resource_quota->free_pool) {
      int64_t amt = -resource_user->free_pool;
      resource_user->free_pool = 0;
      resource_quota->free_pool -= amt;
      rq_update_estimate(resource_quota);
      if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
        gpr_log(GPR_INFO,
                "RQ %s %s: grant alloc %" PRId64
                " bytes; rq_free_pool -> %" PRId64,
                resource_quota->name.c_str(), resource_user->name.c_str(), amt,
                resource_quota->free_pool);
      }
    } else if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace) &&
               resource_user->free_pool >= 0) {
      gpr_log(GPR_INFO, "RQ %s %s: discard already satisfied alloc request",
              resource_quota->name.c_str(), resource_user->name.c_str());
    }
    if (resource_user->free_pool >= 0) {
      resource_user->allocating = false;
      resource_user->outstanding_allocations = 0;
      grpc_core::ExecCtx::RunList(DEBUG_LOCATION, &resource_user->on_allocated);
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
static bool rq_reclaim_from_per_user_free_pool(
    grpc_resource_quota* resource_quota) {
  grpc_resource_user* resource_user;
  while ((resource_user = rulist_pop_head(resource_quota,
                                          GRPC_RULIST_NON_EMPTY_FREE_POOL))) {
    gpr_mu_lock(&resource_user->mu);
    resource_user->added_to_free_pool = false;
    if (resource_user->free_pool > 0) {
      int64_t amt = resource_user->free_pool;
      resource_user->free_pool = 0;
      resource_quota->free_pool += amt;
      rq_update_estimate(resource_quota);
      if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
        gpr_log(GPR_INFO,
                "RQ %s %s: reclaim_from_per_user_free_pool %" PRId64
                " bytes; rq_free_pool -> %" PRId64,
                resource_quota->name.c_str(), resource_user->name.c_str(), amt,
                resource_quota->free_pool);
      }
      gpr_mu_unlock(&resource_user->mu);
      return true;
    } else {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
        gpr_log(GPR_INFO,
                "RQ %s %s: failed to reclaim_from_per_user_free_pool; "
                "free_pool = %" PRId64 "; rq_free_pool = %" PRId64,
                resource_quota->name.c_str(), resource_user->name.c_str(),
                resource_user->free_pool, resource_quota->free_pool);
      }
      gpr_mu_unlock(&resource_user->mu);
    }
  }
  return false;
}

/* returns true if reclamation is proceeding */
static bool rq_reclaim(grpc_resource_quota* resource_quota, bool destructive) {
  if (resource_quota->reclaiming) return true;
  grpc_rulist list = destructive ? GRPC_RULIST_RECLAIMER_DESTRUCTIVE
                                 : GRPC_RULIST_RECLAIMER_BENIGN;
  grpc_resource_user* resource_user = rulist_pop_head(resource_quota, list);
  if (resource_user == nullptr) return false;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
    gpr_log(GPR_INFO, "RQ %s %s: initiate %s reclamation",
            resource_quota->name.c_str(), resource_user->name.c_str(),
            destructive ? "destructive" : "benign");
  }
  resource_quota->reclaiming = true;
  grpc_resource_quota_ref_internal(resource_quota);
  grpc_closure* c = resource_user->reclaimers[destructive];
  GPR_ASSERT(c);
  resource_quota->debug_only_last_reclaimer_resource_user = resource_user;
  resource_quota->debug_only_last_initiated_reclaimer = c;
  resource_user->reclaimers[destructive] = nullptr;
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, GRPC_ERROR_NONE);
  return true;
}

/*******************************************************************************
 * ru_slice: a slice implementation that is backed by a grpc_resource_user
 */

namespace grpc_core {

class RuSliceRefcount {
 public:
  static void Destroy(void* p) {
    auto* rc = static_cast<RuSliceRefcount*>(p);
    rc->~RuSliceRefcount();
    gpr_free(rc);
  }
  RuSliceRefcount(grpc_resource_user* resource_user, size_t size)
      : base_(grpc_slice_refcount::Type::REGULAR, &refs_, Destroy, this,
              &base_),
        resource_user_(resource_user),
        size_(size) {
    // Nothing to do here.
  }
  ~RuSliceRefcount() { grpc_resource_user_free(resource_user_, size_); }

  grpc_slice_refcount* base_refcount() { return &base_; }

 private:
  grpc_slice_refcount base_;
  RefCount refs_;
  grpc_resource_user* resource_user_;
  size_t size_;
};

}  // namespace grpc_core

static grpc_slice ru_slice_create(grpc_resource_user* resource_user,
                                  size_t size) {
  auto* rc = static_cast<grpc_core::RuSliceRefcount*>(
      gpr_malloc(sizeof(grpc_core::RuSliceRefcount) + size));
  new (rc) grpc_core::RuSliceRefcount(resource_user, size);
  grpc_slice slice;

  slice.refcount = rc->base_refcount();
  slice.data.refcounted.bytes = reinterpret_cast<uint8_t*>(rc + 1);
  slice.data.refcounted.length = size;
  return slice;
}

/*******************************************************************************
 * grpc_resource_quota internal implementation: resource user manipulation under
 * the combiner
 */

static void ru_allocate(void* ru, grpc_error_handle /*error*/) {
  grpc_resource_user* resource_user = static_cast<grpc_resource_user*>(ru);
  if (rulist_empty(resource_user->resource_quota,
                   GRPC_RULIST_AWAITING_ALLOCATION)) {
    rq_step_sched(resource_user->resource_quota);
  }
  rulist_add_tail(resource_user, GRPC_RULIST_AWAITING_ALLOCATION);
}

static void ru_add_to_free_pool(void* ru, grpc_error_handle /*error*/) {
  grpc_resource_user* resource_user = static_cast<grpc_resource_user*>(ru);
  if (!rulist_empty(resource_user->resource_quota,
                    GRPC_RULIST_AWAITING_ALLOCATION) &&
      rulist_empty(resource_user->resource_quota,
                   GRPC_RULIST_NON_EMPTY_FREE_POOL)) {
    rq_step_sched(resource_user->resource_quota);
  }
  rulist_add_tail(resource_user, GRPC_RULIST_NON_EMPTY_FREE_POOL);
}

static bool ru_post_reclaimer(grpc_resource_user* resource_user,
                              bool destructive) {
  grpc_closure* closure = resource_user->new_reclaimers[destructive];
  GPR_ASSERT(closure != nullptr);
  resource_user->new_reclaimers[destructive] = nullptr;
  GPR_ASSERT(resource_user->reclaimers[destructive] == nullptr);
  if (gpr_atm_acq_load(&resource_user->shutdown) > 0) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, GRPC_ERROR_CANCELLED);
    return false;
  }
  resource_user->reclaimers[destructive] = closure;
  return true;
}

static void ru_post_benign_reclaimer(void* ru, grpc_error_handle /*error*/) {
  grpc_resource_user* resource_user = static_cast<grpc_resource_user*>(ru);
  if (!ru_post_reclaimer(resource_user, false)) return;
  if (!rulist_empty(resource_user->resource_quota,
                    GRPC_RULIST_AWAITING_ALLOCATION) &&
      rulist_empty(resource_user->resource_quota,
                   GRPC_RULIST_NON_EMPTY_FREE_POOL) &&
      rulist_empty(resource_user->resource_quota,
                   GRPC_RULIST_RECLAIMER_BENIGN)) {
    rq_step_sched(resource_user->resource_quota);
  }
  rulist_add_tail(resource_user, GRPC_RULIST_RECLAIMER_BENIGN);
}

static void ru_post_destructive_reclaimer(void* ru,
                                          grpc_error_handle /*error*/) {
  grpc_resource_user* resource_user = static_cast<grpc_resource_user*>(ru);
  if (!ru_post_reclaimer(resource_user, true)) return;
  if (!rulist_empty(resource_user->resource_quota,
                    GRPC_RULIST_AWAITING_ALLOCATION) &&
      rulist_empty(resource_user->resource_quota,
                   GRPC_RULIST_NON_EMPTY_FREE_POOL) &&
      rulist_empty(resource_user->resource_quota,
                   GRPC_RULIST_RECLAIMER_BENIGN) &&
      rulist_empty(resource_user->resource_quota,
                   GRPC_RULIST_RECLAIMER_DESTRUCTIVE)) {
    rq_step_sched(resource_user->resource_quota);
  }
  rulist_add_tail(resource_user, GRPC_RULIST_RECLAIMER_DESTRUCTIVE);
}

static void ru_shutdown(void* ru, grpc_error_handle /*error*/) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
    gpr_log(GPR_INFO, "RU shutdown %p", ru);
  }
  grpc_resource_user* resource_user = static_cast<grpc_resource_user*>(ru);
  gpr_mu_lock(&resource_user->mu);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, resource_user->reclaimers[0],
                          GRPC_ERROR_CANCELLED);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, resource_user->reclaimers[1],
                          GRPC_ERROR_CANCELLED);
  resource_user->reclaimers[0] = nullptr;
  resource_user->reclaimers[1] = nullptr;
  rulist_remove(resource_user, GRPC_RULIST_RECLAIMER_BENIGN);
  rulist_remove(resource_user, GRPC_RULIST_RECLAIMER_DESTRUCTIVE);
  if (resource_user->allocating) {
    rq_step_sched(resource_user->resource_quota);
  }
  gpr_mu_unlock(&resource_user->mu);
}

static void ru_destroy(void* ru, grpc_error_handle /*error*/) {
  grpc_resource_user* resource_user = static_cast<grpc_resource_user*>(ru);
  GPR_ASSERT(gpr_atm_no_barrier_load(&resource_user->refs) == 0);
  // Free all the remaining thread quota
  grpc_resource_user_free_threads(resource_user,
                                  static_cast<int>(gpr_atm_no_barrier_load(
                                      &resource_user->num_threads_allocated)));

  for (int i = 0; i < GRPC_RULIST_COUNT; i++) {
    rulist_remove(resource_user, static_cast<grpc_rulist>(i));
  }
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, resource_user->reclaimers[0],
                          GRPC_ERROR_CANCELLED);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, resource_user->reclaimers[1],
                          GRPC_ERROR_CANCELLED);
  if (resource_user->free_pool != 0) {
    resource_user->resource_quota->free_pool += resource_user->free_pool;
    rq_step_sched(resource_user->resource_quota);
  }
  grpc_resource_quota_unref_internal(resource_user->resource_quota);
  gpr_mu_destroy(&resource_user->mu);
  delete resource_user;
}

static void ru_alloc_slices(
    grpc_resource_user_slice_allocator* slice_allocator) {
  for (size_t i = 0; i < slice_allocator->count; i++) {
    grpc_slice_buffer_add_indexed(
        slice_allocator->dest, ru_slice_create(slice_allocator->resource_user,
                                               slice_allocator->length));
  }
}

static void ru_allocated_slices(void* arg, grpc_error_handle error) {
  grpc_resource_user_slice_allocator* slice_allocator =
      static_cast<grpc_resource_user_slice_allocator*>(arg);
  if (error == GRPC_ERROR_NONE) ru_alloc_slices(slice_allocator);
  grpc_core::Closure::Run(DEBUG_LOCATION, &slice_allocator->on_done,
                          GRPC_ERROR_REF(error));
}

/*******************************************************************************
 * grpc_resource_quota internal implementation: quota manipulation under the
 * combiner
 */

struct rq_resize_args {
  int64_t size;
  grpc_resource_quota* resource_quota;
  grpc_closure closure;
};
static void rq_resize(void* args, grpc_error_handle /*error*/) {
  rq_resize_args* a = static_cast<rq_resize_args*>(args);
  int64_t delta = a->size - a->resource_quota->size;
  a->resource_quota->size += delta;
  a->resource_quota->free_pool += delta;
  rq_update_estimate(a->resource_quota);
  rq_step_sched(a->resource_quota);
  grpc_resource_quota_unref_internal(a->resource_quota);
  gpr_free(a);
}

static void rq_reclamation_done(void* rq, grpc_error_handle /*error*/) {
  grpc_resource_quota* resource_quota = static_cast<grpc_resource_quota*>(rq);
  resource_quota->reclaiming = false;
  rq_step_sched(resource_quota);
  grpc_resource_quota_unref_internal(resource_quota);
}

/*******************************************************************************
 * grpc_resource_quota api
 */

/* Public API */
grpc_resource_quota* grpc_resource_quota_create(const char* name) {
  grpc_resource_quota* resource_quota = new grpc_resource_quota;
  gpr_ref_init(&resource_quota->refs, 1);
  resource_quota->combiner = grpc_combiner_create();
  resource_quota->free_pool = INT64_MAX;
  resource_quota->size = INT64_MAX;
  resource_quota->used = 0;
  gpr_atm_no_barrier_store(&resource_quota->last_size, GPR_ATM_MAX);
  gpr_mu_init(&resource_quota->thread_count_mu);
  resource_quota->max_threads = INT_MAX;
  resource_quota->num_threads_allocated = 0;
  resource_quota->step_scheduled = false;
  resource_quota->reclaiming = false;
  gpr_atm_no_barrier_store(&resource_quota->memory_usage_estimation, 0);
  if (name != nullptr) {
    resource_quota->name = name;
  } else {
    resource_quota->name = absl::StrCat(
        "anonymous_pool_", reinterpret_cast<intptr_t>(resource_quota));
  }
  GRPC_CLOSURE_INIT(&resource_quota->rq_step_closure, rq_step, resource_quota,
                    nullptr);
  GRPC_CLOSURE_INIT(&resource_quota->rq_reclamation_done_closure,
                    rq_reclamation_done, resource_quota, nullptr);
  for (int i = 0; i < GRPC_RULIST_COUNT; i++) {
    resource_quota->roots[i] = nullptr;
  }
  return resource_quota;
}

void grpc_resource_quota_unref_internal(grpc_resource_quota* resource_quota) {
  if (gpr_unref(&resource_quota->refs)) {
    // No outstanding thread quota
    GPR_ASSERT(resource_quota->num_threads_allocated == 0);
    GRPC_COMBINER_UNREF(resource_quota->combiner, "resource_quota");
    gpr_mu_destroy(&resource_quota->thread_count_mu);
    delete resource_quota;
  }
}

/* Public API */
void grpc_resource_quota_unref(grpc_resource_quota* resource_quota) {
  grpc_core::ExecCtx exec_ctx;
  grpc_resource_quota_unref_internal(resource_quota);
}

grpc_resource_quota* grpc_resource_quota_ref_internal(
    grpc_resource_quota* resource_quota) {
  gpr_ref(&resource_quota->refs);
  return resource_quota;
}

/* Public API */
void grpc_resource_quota_ref(grpc_resource_quota* resource_quota) {
  grpc_resource_quota_ref_internal(resource_quota);
}

double grpc_resource_quota_get_memory_pressure(
    grpc_resource_quota* resource_quota) {
  return (static_cast<double>(gpr_atm_no_barrier_load(
             &resource_quota->memory_usage_estimation))) /
         (static_cast<double>(MEMORY_USAGE_ESTIMATION_MAX));
}

/* Public API */
void grpc_resource_quota_set_max_threads(grpc_resource_quota* resource_quota,
                                         int new_max_threads) {
  GPR_ASSERT(new_max_threads >= 0);
  gpr_mu_lock(&resource_quota->thread_count_mu);
  resource_quota->max_threads = new_max_threads;
  gpr_mu_unlock(&resource_quota->thread_count_mu);
}

/* Public API */
void grpc_resource_quota_resize(grpc_resource_quota* resource_quota,
                                size_t size) {
  grpc_core::ExecCtx exec_ctx;
  rq_resize_args* a = static_cast<rq_resize_args*>(gpr_malloc(sizeof(*a)));
  a->resource_quota = grpc_resource_quota_ref_internal(resource_quota);
  a->size = static_cast<int64_t>(size);
  gpr_atm_no_barrier_store(&resource_quota->last_size,
                           (gpr_atm)GPR_MIN((size_t)GPR_ATM_MAX, size));
  GRPC_CLOSURE_INIT(&a->closure, rq_resize, a, grpc_schedule_on_exec_ctx);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, &a->closure, GRPC_ERROR_NONE);
}

size_t grpc_resource_quota_peek_size(grpc_resource_quota* resource_quota) {
  return static_cast<size_t>(
      gpr_atm_no_barrier_load(&resource_quota->last_size));
}

/*******************************************************************************
 * grpc_resource_user channel args api
 */

grpc_resource_quota* grpc_resource_quota_from_channel_args(
    const grpc_channel_args* channel_args, bool create) {
  for (size_t i = 0; i < channel_args->num_args; i++) {
    if (0 == strcmp(channel_args->args[i].key, GRPC_ARG_RESOURCE_QUOTA)) {
      if (channel_args->args[i].type == GRPC_ARG_POINTER) {
        return grpc_resource_quota_ref_internal(
            static_cast<grpc_resource_quota*>(
                channel_args->args[i].value.pointer.p));
      } else {
        gpr_log(GPR_DEBUG, GRPC_ARG_RESOURCE_QUOTA " should be a pointer");
      }
    }
  }
  return create ? grpc_resource_quota_create(nullptr) : nullptr;
}

static void* rq_copy(void* rq) {
  grpc_resource_quota_ref(static_cast<grpc_resource_quota*>(rq));
  return rq;
}

static void rq_destroy(void* rq) {
  grpc_resource_quota_unref_internal(static_cast<grpc_resource_quota*>(rq));
}

static int rq_cmp(void* a, void* b) { return GPR_ICMP(a, b); }

const grpc_arg_pointer_vtable* grpc_resource_quota_arg_vtable(void) {
  static const grpc_arg_pointer_vtable vtable = {rq_copy, rq_destroy, rq_cmp};
  return &vtable;
}

/*******************************************************************************
 * grpc_resource_user api
 */

grpc_resource_user* grpc_resource_user_create(
    grpc_resource_quota* resource_quota, const char* name) {
  grpc_resource_user* resource_user = new grpc_resource_user;
  resource_user->resource_quota =
      grpc_resource_quota_ref_internal(resource_quota);
  GRPC_CLOSURE_INIT(&resource_user->allocate_closure, &ru_allocate,
                    resource_user, nullptr);
  GRPC_CLOSURE_INIT(&resource_user->add_to_free_pool_closure,
                    &ru_add_to_free_pool, resource_user, nullptr);
  GRPC_CLOSURE_INIT(&resource_user->post_reclaimer_closure[0],
                    &ru_post_benign_reclaimer, resource_user, nullptr);
  GRPC_CLOSURE_INIT(&resource_user->post_reclaimer_closure[1],
                    &ru_post_destructive_reclaimer, resource_user, nullptr);
  GRPC_CLOSURE_INIT(&resource_user->destroy_closure, &ru_destroy, resource_user,
                    nullptr);
  gpr_mu_init(&resource_user->mu);
  gpr_atm_rel_store(&resource_user->refs, 1);
  gpr_atm_rel_store(&resource_user->shutdown, 0);
  resource_user->free_pool = 0;
  grpc_closure_list_init(&resource_user->on_allocated);
  resource_user->allocating = false;
  resource_user->added_to_free_pool = false;
  gpr_atm_no_barrier_store(&resource_user->num_threads_allocated, 0);
  resource_user->reclaimers[0] = nullptr;
  resource_user->reclaimers[1] = nullptr;
  resource_user->new_reclaimers[0] = nullptr;
  resource_user->new_reclaimers[1] = nullptr;
  resource_user->outstanding_allocations = 0;
  for (int i = 0; i < GRPC_RULIST_COUNT; i++) {
    resource_user->links[i].next = resource_user->links[i].prev = nullptr;
  }
  if (name != nullptr) {
    resource_user->name = name;
  } else {
    resource_user->name = absl::StrCat(
        "anonymous_resource_user_", reinterpret_cast<intptr_t>(resource_user));
  }
  return resource_user;
}

grpc_resource_quota* grpc_resource_user_quota(
    grpc_resource_user* resource_user) {
  return resource_user->resource_quota;
}

static void ru_ref_by(grpc_resource_user* resource_user, gpr_atm amount) {
  GPR_ASSERT(amount > 0);
  GPR_ASSERT(gpr_atm_no_barrier_fetch_add(&resource_user->refs, amount) != 0);
}

static void ru_unref_by(grpc_resource_user* resource_user, gpr_atm amount) {
  GPR_ASSERT(amount > 0);
  gpr_atm old = gpr_atm_full_fetch_add(&resource_user->refs, -amount);
  GPR_ASSERT(old >= amount);
  if (old == amount) {
    resource_user->resource_quota->combiner->Run(
        &resource_user->destroy_closure, GRPC_ERROR_NONE);
  }
}

void grpc_resource_user_ref(grpc_resource_user* resource_user) {
  ru_ref_by(resource_user, 1);
}

void grpc_resource_user_unref(grpc_resource_user* resource_user) {
  ru_unref_by(resource_user, 1);
}

void grpc_resource_user_shutdown(grpc_resource_user* resource_user) {
  if (gpr_atm_full_fetch_add(&resource_user->shutdown, 1) == 0) {
    resource_user->resource_quota->combiner->Run(
        GRPC_CLOSURE_CREATE(ru_shutdown, resource_user, nullptr),
        GRPC_ERROR_NONE);
  }
}

bool grpc_resource_user_allocate_threads(grpc_resource_user* resource_user,
                                         int thread_count) {
  GPR_ASSERT(thread_count >= 0);
  bool is_success = false;
  gpr_mu_lock(&resource_user->resource_quota->thread_count_mu);
  grpc_resource_quota* rq = resource_user->resource_quota;
  if (rq->num_threads_allocated + thread_count <= rq->max_threads) {
    rq->num_threads_allocated += thread_count;
    gpr_atm_no_barrier_fetch_add(&resource_user->num_threads_allocated,
                                 thread_count);
    is_success = true;
  }
  gpr_mu_unlock(&resource_user->resource_quota->thread_count_mu);
  return is_success;
}

void grpc_resource_user_free_threads(grpc_resource_user* resource_user,
                                     int thread_count) {
  GPR_ASSERT(thread_count >= 0);
  gpr_mu_lock(&resource_user->resource_quota->thread_count_mu);
  grpc_resource_quota* rq = resource_user->resource_quota;
  rq->num_threads_allocated -= thread_count;
  int old_count = static_cast<int>(gpr_atm_no_barrier_fetch_add(
      &resource_user->num_threads_allocated, -thread_count));
  if (old_count < thread_count || rq->num_threads_allocated < 0) {
    gpr_log(GPR_ERROR,
            "Releasing more threads (%d) than currently allocated (rq threads: "
            "%d, ru threads: %d)",
            thread_count, rq->num_threads_allocated + thread_count, old_count);
    abort();
  }
  gpr_mu_unlock(&resource_user->resource_quota->thread_count_mu);
}

static bool resource_user_alloc_locked(grpc_resource_user* resource_user,
                                       size_t size,
                                       grpc_closure* optional_on_done) {
  ru_ref_by(resource_user, static_cast<gpr_atm>(size));
  resource_user->free_pool -= static_cast<int64_t>(size);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
    gpr_log(GPR_INFO, "RQ %s %s: alloc %" PRIdPTR "; free_pool -> %" PRId64,
            resource_user->resource_quota->name.c_str(),
            resource_user->name.c_str(), size, resource_user->free_pool);
  }
  if (GPR_LIKELY(resource_user->free_pool >= 0)) return true;
  // Slow path: We need to wait for the free pool to refill.
  if (optional_on_done != nullptr) {
    resource_user->outstanding_allocations += static_cast<int64_t>(size);
    grpc_closure_list_append(&resource_user->on_allocated, optional_on_done,
                             GRPC_ERROR_NONE);
  }
  if (!resource_user->allocating) {
    resource_user->allocating = true;
    resource_user->resource_quota->combiner->Run(
        &resource_user->allocate_closure, GRPC_ERROR_NONE);
  }
  return false;
}

bool grpc_resource_user_safe_alloc(grpc_resource_user* resource_user,
                                   size_t size) {
  if (gpr_atm_no_barrier_load(&resource_user->shutdown)) return false;
  gpr_mu_lock(&resource_user->mu);
  grpc_resource_quota* resource_quota = resource_user->resource_quota;
  bool cas_success;
  do {
    gpr_atm used = gpr_atm_no_barrier_load(&resource_quota->used);
    gpr_atm new_used = used + size;
    if (static_cast<size_t>(new_used) >
        grpc_resource_quota_peek_size(resource_quota)) {
      gpr_mu_unlock(&resource_user->mu);
      return false;
    }
    cas_success = gpr_atm_full_cas(&resource_quota->used, used, new_used);
  } while (!cas_success);
  resource_user_alloc_locked(resource_user, size, nullptr);
  gpr_mu_unlock(&resource_user->mu);
  return true;
}

bool grpc_resource_user_alloc(grpc_resource_user* resource_user, size_t size,
                              grpc_closure* optional_on_done) {
  // TODO(juanlishen): Maybe return immediately if shutting down. Deferring this
  // because some tests become flaky after the change.
  gpr_mu_lock(&resource_user->mu);
  grpc_resource_quota* resource_quota = resource_user->resource_quota;
  gpr_atm_no_barrier_fetch_add(&resource_quota->used, size);
  const bool ret =
      resource_user_alloc_locked(resource_user, size, optional_on_done);
  gpr_mu_unlock(&resource_user->mu);
  return ret;
}

void grpc_resource_user_free(grpc_resource_user* resource_user, size_t size) {
  gpr_mu_lock(&resource_user->mu);
  grpc_resource_quota* resource_quota = resource_user->resource_quota;
  gpr_atm prior = gpr_atm_no_barrier_fetch_add(&resource_quota->used, -size);
  GPR_ASSERT(prior >= static_cast<long>(size));
  bool was_zero_or_negative = resource_user->free_pool <= 0;
  resource_user->free_pool += static_cast<int64_t>(size);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
    gpr_log(GPR_INFO, "RQ %s %s: free %" PRIdPTR "; free_pool -> %" PRId64,
            resource_user->resource_quota->name.c_str(),
            resource_user->name.c_str(), size, resource_user->free_pool);
  }
  bool is_bigger_than_zero = resource_user->free_pool > 0;
  if (is_bigger_than_zero && was_zero_or_negative &&
      !resource_user->added_to_free_pool) {
    resource_user->added_to_free_pool = true;
    resource_quota->combiner->Run(&resource_user->add_to_free_pool_closure,
                                  GRPC_ERROR_NONE);
  }
  gpr_mu_unlock(&resource_user->mu);
  ru_unref_by(resource_user, static_cast<gpr_atm>(size));
}

void grpc_resource_user_post_reclaimer(grpc_resource_user* resource_user,
                                       bool destructive,
                                       grpc_closure* closure) {
  GPR_ASSERT(resource_user->new_reclaimers[destructive] == nullptr);
  resource_user->new_reclaimers[destructive] = closure;
  resource_user->resource_quota->combiner->Run(
      &resource_user->post_reclaimer_closure[destructive], GRPC_ERROR_NONE);
}

void grpc_resource_user_finish_reclamation(grpc_resource_user* resource_user) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
    gpr_log(GPR_INFO, "RQ %s %s: reclamation complete",
            resource_user->resource_quota->name.c_str(),
            resource_user->name.c_str());
  }
  resource_user->resource_quota->combiner->Run(
      &resource_user->resource_quota->rq_reclamation_done_closure,
      GRPC_ERROR_NONE);
}

void grpc_resource_user_slice_allocator_init(
    grpc_resource_user_slice_allocator* slice_allocator,
    grpc_resource_user* resource_user, grpc_iomgr_cb_func cb, void* p) {
  GRPC_CLOSURE_INIT(&slice_allocator->on_allocated, ru_allocated_slices,
                    slice_allocator, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&slice_allocator->on_done, cb, p,
                    grpc_schedule_on_exec_ctx);
  slice_allocator->resource_user = resource_user;
}

bool grpc_resource_user_alloc_slices(
    grpc_resource_user_slice_allocator* slice_allocator, size_t length,
    size_t count, grpc_slice_buffer* dest) {
  if (GPR_UNLIKELY(
          gpr_atm_no_barrier_load(&slice_allocator->resource_user->shutdown))) {
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION, &slice_allocator->on_allocated,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Resource user shutdown"));
    return false;
  }
  slice_allocator->length = length;
  slice_allocator->count = count;
  slice_allocator->dest = dest;
  const bool ret =
      grpc_resource_user_alloc(slice_allocator->resource_user, count * length,
                               &slice_allocator->on_allocated);
  if (ret) ru_alloc_slices(slice_allocator);
  return ret;
}
