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
            "mdelem   REF:%p:%" PRIdPTR "->%" PRIdPTR ": '%s' = '%s'", md,
            refcnt, refcnt + 1, key_str, value_str);
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
            "mdelem   UNREF:%p:%" PRIdPTR "->%" PRIdPTR ": '%s' = '%s'", md,
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
  TraceAtStart("ALLOC_MD");
#endif
}

AllocatedMetadata::AllocatedMetadata(const grpc_slice& key,
                                     const grpc_slice& value, const NoRefKey*)
    : RefcountedMdBase(key, grpc_slice_ref_internal(value)) {
#ifndef NDEBUG
  TraceAtStart("ALLOC_MD_NOREF_KEY");
#endif
}

AllocatedMetadata::AllocatedMetadata(
    const grpc_core::ManagedMemorySlice& key,
    const grpc_core::UnmanagedMemorySlice& value)
    : RefcountedMdBase(key, value) {
#ifndef NDEBUG
  TraceAtStart("ALLOC_MD_NOREF_KEY_VAL");
#endif
}

AllocatedMetadata::AllocatedMetadata(
    const grpc_core::ExternallyManagedSlice& key,
    const grpc_core::UnmanagedMemorySlice& value)
    : RefcountedMdBase(key, value) {
#ifndef NDEBUG
  TraceAtStart("ALLOC_MD_NOREF_KEY_VAL");
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

#ifndef NDEBUG
void grpc_core::RefcountedMdBase::TraceAtStart(const char* tag) {
  if (grpc_trace_metadata.enabled()) {
    char* key_str = grpc_slice_to_c_string(key());
    char* value_str = grpc_slice_to_c_string(value());
    gpr_log(GPR_DEBUG, "mdelem   %s:%p:%" PRIdPTR ": '%s' = '%s'", tag, this,
            RefValue(), key_str, value_str);
    gpr_free(key_str);
    gpr_free(value_str);
  }
}
#endif

InternedMetadata::InternedMetadata(const grpc_slice& key,
                                   const grpc_slice& value, uint32_t hash,
                                   InternedMetadata* next)
    : RefcountedMdBase(grpc_slice_ref_internal(key),
                       grpc_slice_ref_internal(value), hash),
      link_(next) {
#ifndef NDEBUG
  TraceAtStart("INTERNED_MD");
#endif
}

InternedMetadata::InternedMetadata(const grpc_slice& key,
                                   const grpc_slice& value, uint32_t hash,
                                   InternedMetadata* next, const NoRefKey*)
    : RefcountedMdBase(key, grpc_slice_ref_internal(value), hash), link_(next) {
#ifndef NDEBUG
  TraceAtStart("INTERNED_MD_NOREF_KEY");
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
      delete md;
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
      // For ASAN builds, we don't want to crash here, because that will
      // prevent ASAN from providing leak detection information, which is
      // far more useful than this simple assertion.
#ifndef GRPC_ASAN_ENABLED
    GPR_DEBUG_ASSERT(shard->count == 0);
#endif
    gpr_free(shard->elems);
  }
}

#ifndef NDEBUG
static int is_mdelem_static(grpc_mdelem e) {
  return reinterpret_cast<grpc_core::StaticMetadata*>(GRPC_MDELEM_DATA(e)) >=
             &grpc_static_mdelem_table()[0] &&
         reinterpret_cast<grpc_core::StaticMetadata*>(GRPC_MDELEM_DATA(e)) <
             &grpc_static_mdelem_table()[GRPC_STATIC_MDELEM_COUNT];
}
#endif

void InternedMetadata::RefWithShardLocked(mdtab_shard* shard) {
#ifndef NDEBUG
  if (grpc_trace_metadata.enabled()) {
    char* key_str = grpc_slice_to_c_string(key());
    char* value_str = grpc_slice_to_c_string(value());
    intptr_t value = RefValue();
    gpr_log(__FILE__, __LINE__, GPR_LOG_SEVERITY_DEBUG,
            "mdelem   REF:%p:%" PRIdPTR "->%" PRIdPTR ": '%s' = '%s'", this,
            value, value + 1, key_str, value_str);
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

template <bool key_definitely_static, bool value_definitely_static = false>
static grpc_mdelem md_create_maybe_static(const grpc_slice& key,
                                          const grpc_slice& value);
template <bool key_definitely_static>
static grpc_mdelem md_create_must_intern(const grpc_slice& key,
                                         const grpc_slice& value,
                                         uint32_t hash);

template <bool key_definitely_static, bool value_definitely_static = false>
static grpc_mdelem md_create(
    const grpc_slice& key, const grpc_slice& value,
    grpc_mdelem_data* compatible_external_backing_store) {
  // Ensure slices are, in fact, static if we claimed they were.
  GPR_DEBUG_ASSERT(!key_definitely_static ||
                   GRPC_IS_STATIC_METADATA_STRING(key));
  GPR_DEBUG_ASSERT(!value_definitely_static ||
                   GRPC_IS_STATIC_METADATA_STRING(value));
  const bool key_is_interned =
      key_definitely_static || grpc_slice_is_interned(key);
  const bool value_is_interned =
      value_definitely_static || grpc_slice_is_interned(value);
  // External storage if either slice is not interned and the caller already
  // created a backing store. If no backing store, we allocate one.
  if (!key_is_interned || !value_is_interned) {
    if (compatible_external_backing_store != nullptr) {
      // Caller provided backing store.
      return GRPC_MAKE_MDELEM(compatible_external_backing_store,
                              GRPC_MDELEM_STORAGE_EXTERNAL);
    } else {
      // We allocate backing store.
      return key_definitely_static
                 ? GRPC_MAKE_MDELEM(
                       new AllocatedMetadata(
                           key, value,
                           static_cast<const AllocatedMetadata::NoRefKey*>(
                               nullptr)),
                       GRPC_MDELEM_STORAGE_ALLOCATED)
                 : GRPC_MAKE_MDELEM(new AllocatedMetadata(key, value),
                                    GRPC_MDELEM_STORAGE_ALLOCATED);
    }
  }
  return md_create_maybe_static<key_definitely_static, value_definitely_static>(
      key, value);
}

template <bool key_definitely_static, bool value_definitely_static>
static grpc_mdelem md_create_maybe_static(const grpc_slice& key,
                                          const grpc_slice& value) {
  // Ensure slices are, in fact, static if we claimed they were.
  GPR_DEBUG_ASSERT(!key_definitely_static ||
                   GRPC_IS_STATIC_METADATA_STRING(key));
  GPR_DEBUG_ASSERT(!value_definitely_static ||
                   GRPC_IS_STATIC_METADATA_STRING(value));
  GPR_DEBUG_ASSERT(key.refcount != nullptr);
  GPR_DEBUG_ASSERT(value.refcount != nullptr);

  const bool key_is_static_mdstr =
      key_definitely_static ||
      key.refcount->GetType() == grpc_slice_refcount::Type::STATIC;
  const bool value_is_static_mdstr =
      value_definitely_static ||
      value.refcount->GetType() == grpc_slice_refcount::Type::STATIC;

  const intptr_t kidx = GRPC_STATIC_METADATA_INDEX(key);

  // Not all static slice input yields a statically stored metadata element.
  if (key_is_static_mdstr && value_is_static_mdstr) {
    grpc_mdelem static_elem = grpc_static_mdelem_for_static_strings(
        kidx, GRPC_STATIC_METADATA_INDEX(value));
    if (!GRPC_MDISNULL(static_elem)) {
      return static_elem;
    }
  }

  uint32_t khash = key_definitely_static
                       ? grpc_static_metadata_hash_values[kidx]
                       : grpc_slice_hash_refcounted(key);

  uint32_t hash = GRPC_MDSTR_KV_HASH(khash, grpc_slice_hash_refcounted(value));
  return md_create_must_intern<key_definitely_static>(key, value, hash);
}

template <bool key_definitely_static>
static grpc_mdelem md_create_must_intern(const grpc_slice& key,
                                         const grpc_slice& value,
                                         uint32_t hash) {
  // Here, we know both key and value are both at least interned, and both
  // possibly static. We know that anything inside the shared interned table is
  // also at least interned (and maybe static). Note that equality for a static
  // and interned slice implies that they are both the same exact slice.
  // The same applies to a pair of interned slices, or a pair of static slices.
  // Rather than run the full equality check, we can therefore just do a pointer
  // comparison of the refcounts.
  InternedMetadata* md;
  mdtab_shard* shard = &g_shards[SHARD_IDX(hash)];
  size_t idx;

  GPR_TIMER_SCOPE("grpc_mdelem_from_metadata_strings", 0);

  gpr_mu_lock(&shard->mu);

  idx = TABLE_IDX(hash, shard->capacity);
  /* search for an existing pair */
  for (md = shard->elems[idx].next; md; md = md->bucket_next()) {
    if (grpc_slice_static_interned_equal(key, md->key()) &&
        grpc_slice_static_interned_equal(value, md->value())) {
      md->RefWithShardLocked(shard);
      gpr_mu_unlock(&shard->mu);
      return GRPC_MAKE_MDELEM(md, GRPC_MDELEM_STORAGE_INTERNED);
    }
  }

  /* not found: create a new pair */
  md = key_definitely_static
           ? new InternedMetadata(
                 key, value, hash, shard->elems[idx].next,
                 static_cast<const InternedMetadata::NoRefKey*>(nullptr))
           : new InternedMetadata(key, value, hash, shard->elems[idx].next);
  shard->elems[idx].next = md;
  shard->count++;

  if (shard->count > shard->capacity * 2) {
    rehash_mdtab(shard);
  }

  gpr_mu_unlock(&shard->mu);

  return GRPC_MAKE_MDELEM(md, GRPC_MDELEM_STORAGE_INTERNED);
}

grpc_mdelem grpc_mdelem_create(
    const grpc_slice& key, const grpc_slice& value,
    grpc_mdelem_data* compatible_external_backing_store) {
  return md_create<false>(key, value, compatible_external_backing_store);
}

grpc_mdelem grpc_mdelem_create(
    const grpc_core::StaticMetadataSlice& key, const grpc_slice& value,
    grpc_mdelem_data* compatible_external_backing_store) {
  return md_create<true>(key, value, compatible_external_backing_store);
}

/* Create grpc_mdelem from provided slices. We specify via template parameter
   whether we know that the input key is static or not. If it is, we short
   circuit various comparisons and a no-op unref. */
template <bool key_definitely_static>
static grpc_mdelem md_from_slices(const grpc_slice& key,
                                  const grpc_slice& value) {
  // Ensure key is, in fact, static if we claimed it was.
  GPR_DEBUG_ASSERT(!key_definitely_static ||
                   GRPC_IS_STATIC_METADATA_STRING(key));
  grpc_mdelem out = md_create<key_definitely_static>(key, value, nullptr);
  if (!key_definitely_static) {
    grpc_slice_unref_internal(key);
  }
  grpc_slice_unref_internal(value);
  return out;
}

grpc_mdelem grpc_mdelem_from_slices(const grpc_slice& key,
                                    const grpc_slice& value) {
  return md_from_slices</*key_definitely_static=*/false>(key, value);
}

grpc_mdelem grpc_mdelem_from_slices(const grpc_core::StaticMetadataSlice& key,
                                    const grpc_slice& value) {
  return md_from_slices</*key_definitely_static=*/true>(key, value);
}

grpc_mdelem grpc_mdelem_from_slices(
    const grpc_core::StaticMetadataSlice& key,
    const grpc_core::StaticMetadataSlice& value) {
  grpc_mdelem out = md_create_maybe_static<true, true>(key, value);
  return out;
}

grpc_mdelem grpc_mdelem_from_slices(
    const grpc_core::StaticMetadataSlice& key,
    const grpc_core::ManagedMemorySlice& value) {
  // TODO(arjunroy): We can save the unref if md_create_maybe_static ended up
  // creating a new interned metadata. But otherwise - we need this here.
  grpc_mdelem out = md_create_maybe_static<true>(key, value);
  grpc_slice_unref_internal(value);
  return out;
}

grpc_mdelem grpc_mdelem_from_slices(
    const grpc_core::ManagedMemorySlice& key,
    const grpc_core::ManagedMemorySlice& value) {
  grpc_mdelem out = md_create_maybe_static<false>(key, value);
  // TODO(arjunroy): We can save the unref if md_create_maybe_static ended up
  // creating a new interned metadata. But otherwise - we need this here.
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
               grpc_static_mdelem_table()]);
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
               grpc_static_mdelem_table()]);
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
        delete md;
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
      delete reinterpret_cast<AllocatedMetadata*>(ptr);
      break;
    }
  }
}
