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

using grpc_core::AllocatedMetadata;
using grpc_core::InternedMetadata;
using grpc_core::StaticMetadata;
using grpc_core::UserData;

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
#define FWD_DEBUG_ARGS file, line

void grpc_mdelem_trace_ref(void* md, const grpc_slice& key,
                           const grpc_slice& value, intptr_t refcnt,
                           const char* file, int line) {
  if (grpc_trace_metadata.enabled()) {
    char* key_str = grpc_slice_to_c_string(key);
    char* value_str = grpc_slice_to_c_string(value);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "ELM   REF:%p:%" PRIdPTR "->%" PRIdPTR ": '%s' = '%s'", md, refcnt,
            refcnt + 1, key_str, value_str);
    gpr_free(key_str);
    gpr_free(value_str);
  }
}

void grpc_mdelem_trace_unref(void* md, const grpc_slice& key,
                             const grpc_slice& value, intptr_t refcnt,
                             const char* file, int line) {
  if (grpc_trace_metadata.enabled()) {
    char* key_str = grpc_slice_to_c_string(key);
    char* value_str = grpc_slice_to_c_string(value);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "ELM   UNREF:%p:%" PRIdPTR "->%" PRIdPTR ": '%s' = '%s'", md,
            refcnt, refcnt - 1, key_str, value_str);
    gpr_free(key_str);
    gpr_free(value_str);
  }
}

#else  // ifndef NDEBUG
#define DEBUG_ARGS
#define FWD_DEBUG_ARGS
#endif  // ifndef NDEBUG

#define INITIAL_SHARD_CAPACITY 8
#define LOG2_SHARD_COUNT 4
#define SHARD_COUNT ((size_t)(1 << LOG2_SHARD_COUNT))

#define TABLE_IDX(hash, capacity) (((hash) >> (LOG2_SHARD_COUNT)) % (capacity))
#define SHARD_IDX(hash) ((hash) & ((1 << (LOG2_SHARD_COUNT)) - 1))

void StaticMetadata::HashInit() {
  uint32_t k_hash = grpc_slice_hash_internal(kv_.key);
  uint32_t v_hash = grpc_slice_hash_internal(kv_.value);
  hash_ = GRPC_MDSTR_KV_HASH(k_hash, v_hash);
}

AllocatedMetadata::AllocatedMetadata(const grpc_slice& key,
                                     const grpc_slice& value)
    : RefcountedMdBase(grpc_slice_ref_internal(key),
                       grpc_slice_ref_internal(value)) {
#ifndef NDEBUG
  if (grpc_trace_metadata.enabled()) {
    char* key_str = grpc_slice_to_c_string(key);
    char* value_str = grpc_slice_to_c_string(value);
    gpr_log(GPR_DEBUG, "ELM ALLOC:%p:%" PRIdPTR ": '%s' = '%s'", this,
            RefValue(), key_str, value_str);
    gpr_free(key_str);
    gpr_free(value_str);
  }
#endif
}

AllocatedMetadata::~AllocatedMetadata() {
  grpc_slice_unref_internal(key());
  grpc_slice_unref_internal(value());
  void* user_data = user_data_.data.Load(grpc_core::MemoryOrder::RELAXED);
  if (user_data) {
    destroy_user_data_func destroy_user_data =
        user_data_.destroy_user_data.Load(grpc_core::MemoryOrder::RELAXED);
    destroy_user_data(user_data);
  }
}

InternedMetadata::InternedMetadata(const grpc_slice& key,
                                   const grpc_slice& value, uint32_t hash,
                                   InternedMetadata* next)
    : RefcountedMdBase(grpc_slice_ref_internal(key),
                       grpc_slice_ref_internal(value), hash),
      link_(next) {
#ifndef NDEBUG
  if (grpc_trace_metadata.enabled()) {
    char* key_str = grpc_slice_to_c_string(key);
    char* value_str = grpc_slice_to_c_string(value);
    gpr_log(GPR_DEBUG, "ELM   NEW:%p:%" PRIdPTR ": '%s' = '%s'", this,
            RefValue(), key_str, value_str);
    gpr_free(key_str);
    gpr_free(value_str);
  }
#endif
}

InternedMetadata::~InternedMetadata() {
  grpc_slice_unref_internal(key());
  grpc_slice_unref_internal(value());
  void* user_data = user_data_.data.Load(grpc_core::MemoryOrder::RELAXED);
  if (user_data) {
    destroy_user_data_func destroy_user_data =
        user_data_.destroy_user_data.Load(grpc_core::MemoryOrder::RELAXED);
    destroy_user_data(user_data);
  }
}

size_t InternedMetadata::CleanupLinkedMetadata(
    InternedMetadata::BucketLink* head) {
  size_t num_freed = 0;
  InternedMetadata::BucketLink* prev_next = head;
  InternedMetadata *md, *next;

  for (md = head->next; md; md = next) {
    next = md->link_.next;
    if (md->AllRefsDropped()) {
      prev_next->next = next;
      grpc_core::Delete(md);
      num_freed++;
    } else {
      prev_next = &md->link_;
    }
  }
  return num_freed;
}

typedef struct mdtab_shard {
  gpr_mu mu;
  InternedMetadata::BucketLink* elems;
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
    shard->elems = static_cast<InternedMetadata::BucketLink*>(
        gpr_zalloc(sizeof(*shard->elems) * shard->capacity));
  }
}

void grpc_mdctx_global_shutdown() {
  for (size_t i = 0; i < SHARD_COUNT; i++) {
    mdtab_shard* shard = &g_shards[i];
    gpr_mu_destroy(&shard->mu);
    gc_mdtab(shard);
    if (shard->count != 0) {
      gpr_log(GPR_DEBUG, "WARNING: %" PRIuPTR " metadata elements were leaked",
              shard->count);
      if (grpc_iomgr_abort_on_leaks()) {
        abort();
      }
    }
    GPR_DEBUG_ASSERT(shard->count == 0);
    gpr_free(shard->elems);
  }
}

#ifndef NDEBUG
static int is_mdelem_static(grpc_mdelem e) {
  return reinterpret_cast<grpc_core::StaticMetadata*>(GRPC_MDELEM_DATA(e)) >=
             &grpc_static_mdelem_table[0] &&
         reinterpret_cast<grpc_core::StaticMetadata*>(GRPC_MDELEM_DATA(e)) <
             &grpc_static_mdelem_table[GRPC_STATIC_MDELEM_COUNT];
}
#endif

void InternedMetadata::RefWithShardLocked(mdtab_shard* shard) {
#ifndef NDEBUG
  if (grpc_trace_metadata.enabled()) {
    char* key_str = grpc_slice_to_c_string(key());
    char* value_str = grpc_slice_to_c_string(value());
    intptr_t value = RefValue();
    gpr_log(__FILE__, __LINE__, GPR_LOG_SEVERITY_DEBUG,
            "ELM   REF:%p:%" PRIdPTR "->%" PRIdPTR ": '%s' = '%s'", this, value,
            value + 1, key_str, value_str);
    gpr_free(key_str);
    gpr_free(value_str);
  }
#endif
  if (FirstRef()) {
    gpr_atm_no_barrier_fetch_add(&shard->free_estimate, -1);
  }
}

static void gc_mdtab(mdtab_shard* shard) {
  GPR_TIMER_SCOPE("gc_mdtab", 0);
  size_t num_freed = 0;
  for (size_t i = 0; i < shard->capacity; ++i) {
    intptr_t freed = InternedMetadata::CleanupLinkedMetadata(&shard->elems[i]);
    num_freed += freed;
    shard->count -= freed;
  }
  gpr_atm_no_barrier_fetch_add(&shard->free_estimate,
                               -static_cast<intptr_t>(num_freed));
}

static void grow_mdtab(mdtab_shard* shard) {
  GPR_TIMER_SCOPE("grow_mdtab", 0);

  size_t capacity = shard->capacity * 2;
  size_t i;
  InternedMetadata::BucketLink* mdtab;
  InternedMetadata *md, *next;
  uint32_t hash;

  mdtab = static_cast<InternedMetadata::BucketLink*>(
      gpr_zalloc(sizeof(InternedMetadata::BucketLink) * capacity));

  for (i = 0; i < shard->capacity; i++) {
    for (md = shard->elems[i].next; md; md = next) {
      size_t idx;
      hash = md->hash();
      next = md->bucket_next();
      idx = TABLE_IDX(hash, capacity);
      md->set_bucket_next(mdtab[idx].next);
      mdtab[idx].next = md;
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
  // External storage if either slice is not interned and the caller already
  // created a backing store. If no backing store, we allocate one.
  if (!grpc_slice_is_interned(key) || !grpc_slice_is_interned(value)) {
    if (compatible_external_backing_store != nullptr) {
      // Caller provided backing store.
      return GRPC_MAKE_MDELEM(compatible_external_backing_store,
                              GRPC_MDELEM_STORAGE_EXTERNAL);
    } else {
      // We allocate backing store.
      return GRPC_MAKE_MDELEM(grpc_core::New<AllocatedMetadata>(key, value),
                              GRPC_MDELEM_STORAGE_ALLOCATED);
    }
  }

  // Not all static slice input yields a statically stored metadata element.
  // It may be worth documenting why.
  if (GRPC_IS_STATIC_METADATA_STRING(key) &&
      GRPC_IS_STATIC_METADATA_STRING(value)) {
    grpc_mdelem static_elem = grpc_static_mdelem_for_static_strings(
        GRPC_STATIC_METADATA_INDEX(key), GRPC_STATIC_METADATA_INDEX(value));
    if (!GRPC_MDISNULL(static_elem)) {
      return static_elem;
    }
  }

  uint32_t hash = GRPC_MDSTR_KV_HASH(grpc_slice_hash_refcounted(key),
                                     grpc_slice_hash_refcounted(value));
  InternedMetadata* md;
  mdtab_shard* shard = &g_shards[SHARD_IDX(hash)];
  size_t idx;

  GPR_TIMER_SCOPE("grpc_mdelem_from_metadata_strings", 0);

  gpr_mu_lock(&shard->mu);

  idx = TABLE_IDX(hash, shard->capacity);
  /* search for an existing pair */
  for (md = shard->elems[idx].next; md; md = md->bucket_next()) {
    if (grpc_slice_eq(key, md->key()) && grpc_slice_eq(value, md->value())) {
      md->RefWithShardLocked(shard);
      gpr_mu_unlock(&shard->mu);
      return GRPC_MAKE_MDELEM(md, GRPC_MDELEM_STORAGE_INTERNED);
    }
  }

  /* not found: create a new pair */
  md = grpc_core::New<InternedMetadata>(key, value, hash,
                                        shard->elems[idx].next);
  shard->elems[idx].next = md;
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

static void* get_user_data(UserData* user_data, void (*destroy_func)(void*)) {
  if (user_data->destroy_user_data.Load(grpc_core::MemoryOrder::ACQUIRE) ==
      destroy_func) {
    return user_data->data.Load(grpc_core::MemoryOrder::RELAXED);
  } else {
    return nullptr;
  }
}

void* grpc_mdelem_get_user_data(grpc_mdelem md, void (*destroy_func)(void*)) {
  switch (GRPC_MDELEM_STORAGE(md)) {
    case GRPC_MDELEM_STORAGE_EXTERNAL:
      return nullptr;
    case GRPC_MDELEM_STORAGE_STATIC:
      return reinterpret_cast<void*>(
          grpc_static_mdelem_user_data
              [reinterpret_cast<grpc_core::StaticMetadata*>(
                   GRPC_MDELEM_DATA(md)) -
               grpc_static_mdelem_table]);
    case GRPC_MDELEM_STORAGE_ALLOCATED: {
      auto* am = reinterpret_cast<AllocatedMetadata*>(GRPC_MDELEM_DATA(md));
      return get_user_data(am->user_data(), destroy_func);
    }
    case GRPC_MDELEM_STORAGE_INTERNED: {
      auto* im = reinterpret_cast<InternedMetadata*> GRPC_MDELEM_DATA(md);
      return get_user_data(im->user_data(), destroy_func);
    }
  }
  GPR_UNREACHABLE_CODE(return nullptr);
}

static void* set_user_data(UserData* ud, void (*destroy_func)(void*),
                           void* data) {
  GPR_ASSERT((data == nullptr) == (destroy_func == nullptr));
  grpc_core::ReleasableMutexLock lock(&ud->mu_user_data);
  if (ud->destroy_user_data.Load(grpc_core::MemoryOrder::RELAXED)) {
    /* user data can only be set once */
    lock.Unlock();
    if (destroy_func != nullptr) {
      destroy_func(data);
    }
    return ud->data.Load(grpc_core::MemoryOrder::RELAXED);
  }
  ud->data.Store(data, grpc_core::MemoryOrder::RELAXED);
  ud->destroy_user_data.Store(destroy_func, grpc_core::MemoryOrder::RELEASE);
  return data;
}

void* grpc_mdelem_set_user_data(grpc_mdelem md, void (*destroy_func)(void*),
                                void* data) {
  switch (GRPC_MDELEM_STORAGE(md)) {
    case GRPC_MDELEM_STORAGE_EXTERNAL:
      destroy_func(data);
      return nullptr;
    case GRPC_MDELEM_STORAGE_STATIC:
      destroy_func(data);
      return reinterpret_cast<void*>(
          grpc_static_mdelem_user_data
              [reinterpret_cast<grpc_core::StaticMetadata*>(
                   GRPC_MDELEM_DATA(md)) -
               grpc_static_mdelem_table]);
    case GRPC_MDELEM_STORAGE_ALLOCATED: {
      auto* am = reinterpret_cast<AllocatedMetadata*>(GRPC_MDELEM_DATA(md));
      return set_user_data(am->user_data(), destroy_func, data);
    }
    case GRPC_MDELEM_STORAGE_INTERNED: {
      auto* im = reinterpret_cast<InternedMetadata*> GRPC_MDELEM_DATA(md);
      GPR_DEBUG_ASSERT(!is_mdelem_static(md));
      return set_user_data(im->user_data(), destroy_func, data);
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

static void note_disposed_interned_metadata(uint32_t hash) {
  mdtab_shard* shard = &g_shards[SHARD_IDX(hash)];
  gpr_atm_no_barrier_fetch_add(&shard->free_estimate, 1);
}

void grpc_mdelem_do_unref(grpc_mdelem gmd DEBUG_ARGS) {
  switch (GRPC_MDELEM_STORAGE(gmd)) {
    case GRPC_MDELEM_STORAGE_EXTERNAL:
    case GRPC_MDELEM_STORAGE_STATIC:
      return;
    case GRPC_MDELEM_STORAGE_INTERNED: {
      auto* md = reinterpret_cast<InternedMetadata*> GRPC_MDELEM_DATA(gmd);
      uint32_t hash = md->hash();
      if (GPR_UNLIKELY(md->Unref(FWD_DEBUG_ARGS))) {
        /* once the refcount hits zero, some other thread can come along and
           free md at any time: it's unsafe from this point on to access it */
        note_disposed_interned_metadata(hash);
      }
      break;
    }
    case GRPC_MDELEM_STORAGE_ALLOCATED: {
      auto* md = reinterpret_cast<AllocatedMetadata*> GRPC_MDELEM_DATA(gmd);
      if (GPR_UNLIKELY(md->Unref(FWD_DEBUG_ARGS))) {
        grpc_core::Delete(md);
      }
      break;
    }
  }
}

void grpc_mdelem_on_final_unref(grpc_mdelem_data_storage storage, void* ptr,
                                uint32_t hash DEBUG_ARGS) {
  switch (storage) {
    case GRPC_MDELEM_STORAGE_EXTERNAL:
    case GRPC_MDELEM_STORAGE_STATIC:
      return;
    case GRPC_MDELEM_STORAGE_INTERNED: {
      note_disposed_interned_metadata(hash);
      break;
    }
    case GRPC_MDELEM_STORAGE_ALLOCATED: {
      grpc_core::Delete(reinterpret_cast<AllocatedMetadata*>(ptr));
      break;
    }
  }
}
