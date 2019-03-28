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

#include <grpc/support/port_platform.h>

#include "src/core/lib/transport/metadata.h"

#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <string.h>

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/murmur_hash.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/static_metadata.h"

/* There are two kinds of mdelem and mdstr instances.
 * Static instances are declared in static_metadata.{h,c} and
 * are initialized by grpc_mdctx_global_init().
 * Dynamic instances are stored in hash tables on grpc_mdctx, and are backed
 * by internal_string and internal_element structures.
 * Internal helper functions here-in (is_mdstr_static, is_mdelem_static) are
 * used to determine which kind of element a pointer refers to.
 */

grpc_core::DebugOnlyTraceFlag grpc_trace_metadata(false, "metadata");

#ifndef NDEBUG
#define DEBUG_ARGS , const char *file, int line
#define FWD_DEBUG_ARGS , file, line
#define REF_MD_LOCKED(shard, s) ref_md_locked((shard), (s), __FILE__, __LINE__)
#else
#define DEBUG_ARGS
#define FWD_DEBUG_ARGS
#define REF_MD_LOCKED(shard, s) ref_md_locked((shard), (s))
#endif

#define INITIAL_SHARD_CAPACITY 8
#define LOG2_SHARD_COUNT 4
#define SHARD_COUNT ((size_t)(1 << LOG2_SHARD_COUNT))

#define TABLE_IDX(hash, capacity) (((hash) >> (LOG2_SHARD_COUNT)) % (capacity))
#define SHARD_IDX(hash) ((hash) & ((1 << (LOG2_SHARD_COUNT)) - 1))

typedef void (*destroy_user_data_func)(void* user_data);

struct UserData {
  gpr_mu mu_user_data;
  gpr_atm destroy_user_data;
  gpr_atm user_data;
};

/* Shadow structure for grpc_mdelem_data for interned elements */
typedef struct interned_metadata {
  /* must be byte compatible with grpc_mdelem_data */
  grpc_slice key;
  grpc_slice value;

  /* private only data */
  gpr_atm refcnt;

  UserData user_data;

  struct interned_metadata* bucket_next;
} interned_metadata;

/* Shadow structure for grpc_mdelem_data for allocated elements */
typedef struct allocated_metadata {
  /* must be byte compatible with grpc_mdelem_data */
  grpc_slice key;
  grpc_slice value;

  /* private only data */
  gpr_atm refcnt;

  UserData user_data;
} allocated_metadata;

typedef struct mdtab_shard {
  gpr_mu mu;
  interned_metadata** elems;
  size_t count;
  size_t capacity;
  /** Estimate of the number of unreferenced mdelems in the hash table.
      This will eventually converge to the exact number, but it's instantaneous
      accuracy is not guaranteed */
  gpr_atm free_estimate;
} mdtab_shard;

static mdtab_shard g_shards[SHARD_COUNT];

static void gc_mdtab(mdtab_shard* shard);

void grpc_mdctx_global_init(void) {
  /* initialize shards */
  for (size_t i = 0; i < SHARD_COUNT; i++) {
    mdtab_shard* shard = &g_shards[i];
    gpr_mu_init(&shard->mu);
    shard->count = 0;
    gpr_atm_no_barrier_store(&shard->free_estimate, 0);
    shard->capacity = INITIAL_SHARD_CAPACITY;
    shard->elems = static_cast<interned_metadata**>(
        gpr_zalloc(sizeof(*shard->elems) * shard->capacity));
  }
}

void grpc_mdctx_global_shutdown() {
  for (size_t i = 0; i < SHARD_COUNT; i++) {
    mdtab_shard* shard = &g_shards[i];
    gpr_mu_destroy(&shard->mu);
    gc_mdtab(shard);
    /* TODO(ctiller): GPR_ASSERT(shard->count == 0); */
    if (shard->count != 0) {
      gpr_log(GPR_DEBUG, "WARNING: %" PRIuPTR " metadata elements were leaked",
              shard->count);
      if (grpc_iomgr_abort_on_leaks()) {
        abort();
      }
    }
    gpr_free(shard->elems);
  }
}

static int is_mdelem_static(grpc_mdelem e) {
  return GRPC_MDELEM_DATA(e) >= &grpc_static_mdelem_table[0] &&
         GRPC_MDELEM_DATA(e) <
             &grpc_static_mdelem_table[GRPC_STATIC_MDELEM_COUNT];
}

static void ref_md_locked(mdtab_shard* shard,
                          interned_metadata* md DEBUG_ARGS) {
#ifndef NDEBUG
  if (grpc_trace_metadata.enabled()) {
    char* key_str = grpc_slice_to_c_string(md->key);
    char* value_str = grpc_slice_to_c_string(md->value);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "ELM   REF:%p:%" PRIdPTR "->%" PRIdPTR ": '%s' = '%s'", (void*)md,
            gpr_atm_no_barrier_load(&md->refcnt),
            gpr_atm_no_barrier_load(&md->refcnt) + 1, key_str, value_str);
    gpr_free(key_str);
    gpr_free(value_str);
  }
#endif
  if (0 == gpr_atm_no_barrier_fetch_add(&md->refcnt, 1)) {
    gpr_atm_no_barrier_fetch_add(&shard->free_estimate, -1);
  }
}

static void gc_mdtab(mdtab_shard* shard) {
  GPR_TIMER_SCOPE("gc_mdtab", 0);

  size_t i;
  interned_metadata** prev_next;
  interned_metadata *md, *next;
  gpr_atm num_freed = 0;

  for (i = 0; i < shard->capacity; i++) {
    prev_next = &shard->elems[i];
    for (md = shard->elems[i]; md; md = next) {
      void* user_data =
          (void*)gpr_atm_no_barrier_load(&md->user_data.user_data);
      next = md->bucket_next;
      if (gpr_atm_acq_load(&md->refcnt) == 0) {
        grpc_slice_unref_internal(md->key);
        grpc_slice_unref_internal(md->value);
        if (md->user_data.user_data) {
          ((destroy_user_data_func)gpr_atm_no_barrier_load(
              &md->user_data.destroy_user_data))(user_data);
        }
        gpr_mu_destroy(&md->user_data.mu_user_data);
        gpr_free(md);
        *prev_next = next;
        num_freed++;
        shard->count--;
      } else {
        prev_next = &md->bucket_next;
      }
    }
  }
  gpr_atm_no_barrier_fetch_add(&shard->free_estimate, -num_freed);
}

static void grow_mdtab(mdtab_shard* shard) {
  GPR_TIMER_SCOPE("grow_mdtab", 0);

  size_t capacity = shard->capacity * 2;
  size_t i;
  interned_metadata** mdtab;
  interned_metadata *md, *next;
  uint32_t hash;

  mdtab = static_cast<interned_metadata**>(
      gpr_zalloc(sizeof(interned_metadata*) * capacity));

  for (i = 0; i < shard->capacity; i++) {
    for (md = shard->elems[i]; md; md = next) {
      size_t idx;
      hash = GRPC_MDSTR_KV_HASH(grpc_slice_hash(md->key),
                                grpc_slice_hash(md->value));
      next = md->bucket_next;
      idx = TABLE_IDX(hash, capacity);
      md->bucket_next = mdtab[idx];
      mdtab[idx] = md;
    }
  }
  gpr_free(shard->elems);
  shard->elems = mdtab;
  shard->capacity = capacity;
}

static void rehash_mdtab(mdtab_shard* shard) {
  if (gpr_atm_no_barrier_load(&shard->free_estimate) >
      static_cast<gpr_atm>(shard->capacity / 4)) {
    gc_mdtab(shard);
  } else {
    grow_mdtab(shard);
  }
}

grpc_mdelem grpc_mdelem_create(
    const grpc_slice& key, const grpc_slice& value,
    grpc_mdelem_data* compatible_external_backing_store) {
  if (!grpc_slice_is_interned(key) || !grpc_slice_is_interned(value)) {
    if (compatible_external_backing_store != nullptr) {
      return GRPC_MAKE_MDELEM(compatible_external_backing_store,
                              GRPC_MDELEM_STORAGE_EXTERNAL);
    }

    allocated_metadata* allocated =
        static_cast<allocated_metadata*>(gpr_malloc(sizeof(*allocated)));
    allocated->key = grpc_slice_ref_internal(key);
    allocated->value = grpc_slice_ref_internal(value);
    gpr_atm_rel_store(&allocated->refcnt, 1);
    allocated->user_data.user_data = 0;
    allocated->user_data.destroy_user_data = 0;
    gpr_mu_init(&allocated->user_data.mu_user_data);
#ifndef NDEBUG
    if (grpc_trace_metadata.enabled()) {
      char* key_str = grpc_slice_to_c_string(allocated->key);
      char* value_str = grpc_slice_to_c_string(allocated->value);
      gpr_log(GPR_DEBUG, "ELM ALLOC:%p:%" PRIdPTR ": '%s' = '%s'",
              (void*)allocated, gpr_atm_no_barrier_load(&allocated->refcnt),
              key_str, value_str);
      gpr_free(key_str);
      gpr_free(value_str);
    }
#endif
    return GRPC_MAKE_MDELEM(allocated, GRPC_MDELEM_STORAGE_ALLOCATED);
  }

  if (GRPC_IS_STATIC_METADATA_STRING(key) &&
      GRPC_IS_STATIC_METADATA_STRING(value)) {
    grpc_mdelem static_elem = grpc_static_mdelem_for_static_strings(
        GRPC_STATIC_METADATA_INDEX(key), GRPC_STATIC_METADATA_INDEX(value));
    if (!GRPC_MDISNULL(static_elem)) {
      return static_elem;
    }
  }

  uint32_t hash =
      GRPC_MDSTR_KV_HASH(grpc_slice_hash(key), grpc_slice_hash(value));
  interned_metadata* md;
  mdtab_shard* shard = &g_shards[SHARD_IDX(hash)];
  size_t idx;

  GPR_TIMER_SCOPE("grpc_mdelem_from_metadata_strings", 0);

  gpr_mu_lock(&shard->mu);

  idx = TABLE_IDX(hash, shard->capacity);
  /* search for an existing pair */
  for (md = shard->elems[idx]; md; md = md->bucket_next) {
    if (grpc_slice_eq(key, md->key) && grpc_slice_eq(value, md->value)) {
      REF_MD_LOCKED(shard, md);
      gpr_mu_unlock(&shard->mu);
      return GRPC_MAKE_MDELEM(md, GRPC_MDELEM_STORAGE_INTERNED);
    }
  }

  /* not found: create a new pair */
  md = static_cast<interned_metadata*>(gpr_malloc(sizeof(interned_metadata)));
  gpr_atm_rel_store(&md->refcnt, 1);
  md->key = grpc_slice_ref_internal(key);
  md->value = grpc_slice_ref_internal(value);
  md->user_data.user_data = 0;
  md->user_data.destroy_user_data = 0;
  md->bucket_next = shard->elems[idx];
  shard->elems[idx] = md;
  gpr_mu_init(&md->user_data.mu_user_data);
#ifndef NDEBUG
  if (grpc_trace_metadata.enabled()) {
    char* key_str = grpc_slice_to_c_string(md->key);
    char* value_str = grpc_slice_to_c_string(md->value);
    gpr_log(GPR_DEBUG, "ELM   NEW:%p:%" PRIdPTR ": '%s' = '%s'", (void*)md,
            gpr_atm_no_barrier_load(&md->refcnt), key_str, value_str);
    gpr_free(key_str);
    gpr_free(value_str);
  }
#endif
  shard->count++;

  if (shard->count > shard->capacity * 2) {
    rehash_mdtab(shard);
  }

  gpr_mu_unlock(&shard->mu);

  return GRPC_MAKE_MDELEM(md, GRPC_MDELEM_STORAGE_INTERNED);
}

grpc_mdelem grpc_mdelem_from_slices(const grpc_slice& key,
                                    const grpc_slice& value) {
  grpc_mdelem out = grpc_mdelem_create(key, value, nullptr);
  grpc_slice_unref_internal(key);
  grpc_slice_unref_internal(value);
  return out;
}

grpc_mdelem grpc_mdelem_from_grpc_metadata(grpc_metadata* metadata) {
  bool changed = false;
  grpc_slice key_slice =
      grpc_slice_maybe_static_intern(metadata->key, &changed);
  grpc_slice value_slice =
      grpc_slice_maybe_static_intern(metadata->value, &changed);
  return grpc_mdelem_create(
      key_slice, value_slice,
      changed ? nullptr : reinterpret_cast<grpc_mdelem_data*>(metadata));
}

grpc_mdelem grpc_mdelem_ref(grpc_mdelem gmd DEBUG_ARGS) {
  switch (GRPC_MDELEM_STORAGE(gmd)) {
    case GRPC_MDELEM_STORAGE_EXTERNAL:
    case GRPC_MDELEM_STORAGE_STATIC:
      break;
    case GRPC_MDELEM_STORAGE_INTERNED: {
      interned_metadata* md =
          reinterpret_cast<interned_metadata*> GRPC_MDELEM_DATA(gmd);
#ifndef NDEBUG
      if (grpc_trace_metadata.enabled()) {
        char* key_str = grpc_slice_to_c_string(md->key);
        char* value_str = grpc_slice_to_c_string(md->value);
        gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
                "ELM   REF:%p:%" PRIdPTR "->%" PRIdPTR ": '%s' = '%s'",
                (void*)md, gpr_atm_no_barrier_load(&md->refcnt),
                gpr_atm_no_barrier_load(&md->refcnt) + 1, key_str, value_str);
        gpr_free(key_str);
        gpr_free(value_str);
      }
#endif
      /* we can assume the ref count is >= 1 as the application is calling
         this function - meaning that no adjustment to mdtab_free is necessary,
         simplifying the logic here to be just an atomic increment */
      /* use C assert to have this removed in opt builds */
      GPR_ASSERT(gpr_atm_no_barrier_load(&md->refcnt) >= 1);
      gpr_atm_no_barrier_fetch_add(&md->refcnt, 1);
      break;
    }
    case GRPC_MDELEM_STORAGE_ALLOCATED: {
      allocated_metadata* md =
          reinterpret_cast<allocated_metadata*> GRPC_MDELEM_DATA(gmd);
#ifndef NDEBUG
      if (grpc_trace_metadata.enabled()) {
        char* key_str = grpc_slice_to_c_string(md->key);
        char* value_str = grpc_slice_to_c_string(md->value);
        gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
                "ELM   REF:%p:%" PRIdPTR "->%" PRIdPTR ": '%s' = '%s'",
                (void*)md, gpr_atm_no_barrier_load(&md->refcnt),
                gpr_atm_no_barrier_load(&md->refcnt) + 1, key_str, value_str);
        gpr_free(key_str);
        gpr_free(value_str);
      }
#endif
      /* we can assume the ref count is >= 1 as the application is calling
         this function - meaning that no adjustment to mdtab_free is necessary,
         simplifying the logic here to be just an atomic increment */
      /* use C assert to have this removed in opt builds */
      gpr_atm_no_barrier_fetch_add(&md->refcnt, 1);
      break;
    }
  }
  return gmd;
}

void grpc_mdelem_unref(grpc_mdelem gmd DEBUG_ARGS) {
  switch (GRPC_MDELEM_STORAGE(gmd)) {
    case GRPC_MDELEM_STORAGE_EXTERNAL:
    case GRPC_MDELEM_STORAGE_STATIC:
      break;
    case GRPC_MDELEM_STORAGE_INTERNED: {
      interned_metadata* md =
          reinterpret_cast<interned_metadata*> GRPC_MDELEM_DATA(gmd);
#ifndef NDEBUG
      if (grpc_trace_metadata.enabled()) {
        char* key_str = grpc_slice_to_c_string(md->key);
        char* value_str = grpc_slice_to_c_string(md->value);
        gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
                "ELM UNREF:%p:%" PRIdPTR "->%" PRIdPTR ": '%s' = '%s'",
                (void*)md, gpr_atm_no_barrier_load(&md->refcnt),
                gpr_atm_no_barrier_load(&md->refcnt) - 1, key_str, value_str);
        gpr_free(key_str);
        gpr_free(value_str);
      }
#endif
      uint32_t hash = GRPC_MDSTR_KV_HASH(grpc_slice_hash(md->key),
                                         grpc_slice_hash(md->value));
      const gpr_atm prev_refcount = gpr_atm_full_fetch_add(&md->refcnt, -1);
      GPR_ASSERT(prev_refcount >= 1);
      if (1 == prev_refcount) {
        /* once the refcount hits zero, some other thread can come along and
           free md at any time: it's unsafe from this point on to access it */
        mdtab_shard* shard = &g_shards[SHARD_IDX(hash)];
        gpr_atm_no_barrier_fetch_add(&shard->free_estimate, 1);
      }
      break;
    }
    case GRPC_MDELEM_STORAGE_ALLOCATED: {
      allocated_metadata* md =
          reinterpret_cast<allocated_metadata*> GRPC_MDELEM_DATA(gmd);
#ifndef NDEBUG
      if (grpc_trace_metadata.enabled()) {
        char* key_str = grpc_slice_to_c_string(md->key);
        char* value_str = grpc_slice_to_c_string(md->value);
        gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
                "ELM UNREF:%p:%" PRIdPTR "->%" PRIdPTR ": '%s' = '%s'",
                (void*)md, gpr_atm_no_barrier_load(&md->refcnt),
                gpr_atm_no_barrier_load(&md->refcnt) - 1, key_str, value_str);
        gpr_free(key_str);
        gpr_free(value_str);
      }
#endif
      const gpr_atm prev_refcount = gpr_atm_full_fetch_add(&md->refcnt, -1);
      GPR_ASSERT(prev_refcount >= 1);
      if (1 == prev_refcount) {
        grpc_slice_unref_internal(md->key);
        grpc_slice_unref_internal(md->value);
        if (md->user_data.user_data) {
          destroy_user_data_func destroy_user_data =
              (destroy_user_data_func)gpr_atm_no_barrier_load(
                  &md->user_data.destroy_user_data);
          destroy_user_data((void*)md->user_data.user_data);
        }
        gpr_mu_destroy(&md->user_data.mu_user_data);
        gpr_free(md);
      }
      break;
    }
  }
}

static void* get_user_data(UserData* user_data, void (*destroy_func)(void*)) {
  if (gpr_atm_acq_load(&user_data->destroy_user_data) ==
      (gpr_atm)destroy_func) {
    return (void*)gpr_atm_no_barrier_load(&user_data->user_data);
  } else {
    return nullptr;
  }
}

void* grpc_mdelem_get_user_data(grpc_mdelem md, void (*destroy_func)(void*)) {
  switch (GRPC_MDELEM_STORAGE(md)) {
    case GRPC_MDELEM_STORAGE_EXTERNAL:
      return nullptr;
    case GRPC_MDELEM_STORAGE_STATIC:
      return (void*)grpc_static_mdelem_user_data[GRPC_MDELEM_DATA(md) -
                                                 grpc_static_mdelem_table];
    case GRPC_MDELEM_STORAGE_ALLOCATED: {
      allocated_metadata* am =
          reinterpret_cast<allocated_metadata*>(GRPC_MDELEM_DATA(md));
      return get_user_data(&am->user_data, destroy_func);
    }
    case GRPC_MDELEM_STORAGE_INTERNED: {
      interned_metadata* im =
          reinterpret_cast<interned_metadata*> GRPC_MDELEM_DATA(md);
      return get_user_data(&im->user_data, destroy_func);
    }
  }
  GPR_UNREACHABLE_CODE(return nullptr);
}

static void* set_user_data(UserData* ud, void (*destroy_func)(void*),
                           void* user_data) {
  GPR_ASSERT((user_data == nullptr) == (destroy_func == nullptr));
  gpr_mu_lock(&ud->mu_user_data);
  if (gpr_atm_no_barrier_load(&ud->destroy_user_data)) {
    /* user data can only be set once */
    gpr_mu_unlock(&ud->mu_user_data);
    if (destroy_func != nullptr) {
      destroy_func(user_data);
    }
    return (void*)gpr_atm_no_barrier_load(&ud->user_data);
  }
  gpr_atm_no_barrier_store(&ud->user_data, (gpr_atm)user_data);
  gpr_atm_rel_store(&ud->destroy_user_data, (gpr_atm)destroy_func);
  gpr_mu_unlock(&ud->mu_user_data);
  return user_data;
}

void* grpc_mdelem_set_user_data(grpc_mdelem md, void (*destroy_func)(void*),
                                void* user_data) {
  switch (GRPC_MDELEM_STORAGE(md)) {
    case GRPC_MDELEM_STORAGE_EXTERNAL:
      destroy_func(user_data);
      return nullptr;
    case GRPC_MDELEM_STORAGE_STATIC:
      destroy_func(user_data);
      return (void*)grpc_static_mdelem_user_data[GRPC_MDELEM_DATA(md) -
                                                 grpc_static_mdelem_table];
    case GRPC_MDELEM_STORAGE_ALLOCATED: {
      allocated_metadata* am =
          reinterpret_cast<allocated_metadata*>(GRPC_MDELEM_DATA(md));
      return set_user_data(&am->user_data, destroy_func, user_data);
    }
    case GRPC_MDELEM_STORAGE_INTERNED: {
      interned_metadata* im =
          reinterpret_cast<interned_metadata*> GRPC_MDELEM_DATA(md);
      GPR_ASSERT(!is_mdelem_static(md));
      return set_user_data(&im->user_data, destroy_func, user_data);
    }
  }
  GPR_UNREACHABLE_CODE(return nullptr);
}

bool grpc_mdelem_eq(grpc_mdelem a, grpc_mdelem b) {
  if (a.payload == b.payload) return true;
  if (GRPC_MDELEM_IS_INTERNED(a) && GRPC_MDELEM_IS_INTERNED(b)) return false;
  if (GRPC_MDISNULL(a) || GRPC_MDISNULL(b)) return false;
  return grpc_slice_eq(GRPC_MDKEY(a), GRPC_MDKEY(b)) &&
         grpc_slice_eq(GRPC_MDVALUE(a), GRPC_MDVALUE(b));
}
