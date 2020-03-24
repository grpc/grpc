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

#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_utils.h"

#include <inttypes.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/murmur_hash.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/iomgr_internal.h" /* for iomgr_abort_on_leaks() */
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/static_metadata.h"

#define LOG2_SHARD_COUNT 5
#define SHARD_COUNT (1 << LOG2_SHARD_COUNT)
#define INITIAL_SHARD_CAPACITY 8

#define TABLE_IDX(hash, capacity) (((hash) >> LOG2_SHARD_COUNT) % (capacity))
#define SHARD_IDX(hash) ((hash) & ((1 << LOG2_SHARD_COUNT) - 1))

using grpc_core::InternedSliceRefcount;

typedef struct slice_shard {
  gpr_mu mu;
  InternedSliceRefcount** strs;
  size_t count;
  size_t capacity;
} slice_shard;

static slice_shard g_shards[SHARD_COUNT];

typedef struct {
  uint32_t hash;
  uint32_t idx;
} static_metadata_hash_ent;

static static_metadata_hash_ent
    static_metadata_hash[4 * GRPC_STATIC_MDSTR_COUNT];
static uint32_t max_static_metadata_hash_probe;
uint32_t grpc_static_metadata_hash_values[GRPC_STATIC_MDSTR_COUNT];

namespace grpc_core {

/* hash seed: decided at initialization time */
uint32_t g_hash_seed;
static bool g_forced_hash_seed = false;

InternedSliceRefcount::~InternedSliceRefcount() {
  slice_shard* shard = &g_shards[SHARD_IDX(this->hash)];
  MutexLock lock(&shard->mu);
  InternedSliceRefcount** prev_next;
  InternedSliceRefcount* cur;
  for (prev_next = &shard->strs[TABLE_IDX(this->hash, shard->capacity)],
      cur = *prev_next;
       cur != this; prev_next = &cur->bucket_next, cur = cur->bucket_next)
    ;
  *prev_next = cur->bucket_next;
  shard->count--;
}

}  // namespace grpc_core

static void grow_shard(slice_shard* shard) {
  GPR_TIMER_SCOPE("grow_strtab", 0);

  size_t capacity = shard->capacity * 2;
  size_t i;
  InternedSliceRefcount** strtab;
  InternedSliceRefcount *s, *next;

  strtab = static_cast<InternedSliceRefcount**>(
      gpr_zalloc(sizeof(InternedSliceRefcount*) * capacity));

  for (i = 0; i < shard->capacity; i++) {
    for (s = shard->strs[i]; s; s = next) {
      size_t idx = TABLE_IDX(s->hash, capacity);
      next = s->bucket_next;
      s->bucket_next = strtab[idx];
      strtab[idx] = s;
    }
  }
  gpr_free(shard->strs);
  shard->strs = strtab;
  shard->capacity = capacity;
}

grpc_core::InternedSlice::InternedSlice(InternedSliceRefcount* s) {
  refcount = &s->base;
  data.refcounted.bytes = reinterpret_cast<uint8_t*>(s + 1);
  data.refcounted.length = s->length;
}

uint32_t grpc_slice_default_hash_impl(grpc_slice s) {
  return gpr_murmur_hash3(GRPC_SLICE_START_PTR(s), GRPC_SLICE_LENGTH(s),
                          grpc_core::g_hash_seed);
}

uint32_t grpc_static_slice_hash(grpc_slice s) {
  return grpc_static_metadata_hash_values[GRPC_STATIC_METADATA_INDEX(s)];
}

int grpc_static_slice_eq(grpc_slice a, grpc_slice b) {
  return GRPC_STATIC_METADATA_INDEX(a) == GRPC_STATIC_METADATA_INDEX(b);
}

uint32_t grpc_slice_hash(grpc_slice s) { return grpc_slice_hash_internal(s); }

grpc_slice grpc_slice_maybe_static_intern(grpc_slice slice,
                                          bool* returned_slice_is_different) {
  if (GRPC_IS_STATIC_METADATA_STRING(slice)) {
    return slice;
  }

  uint32_t hash = grpc_slice_hash_internal(slice);
  for (uint32_t i = 0; i <= max_static_metadata_hash_probe; i++) {
    static_metadata_hash_ent ent =
        static_metadata_hash[(hash + i) % GPR_ARRAY_SIZE(static_metadata_hash)];
    const grpc_core::StaticMetadataSlice* static_slice_table =
        grpc_static_slice_table();
    if (ent.hash == hash && ent.idx < GRPC_STATIC_MDSTR_COUNT &&
        grpc_slice_eq_static_interned(slice, static_slice_table[ent.idx])) {
      *returned_slice_is_different = true;
      return static_slice_table[ent.idx];
    }
  }

  return slice;
}

grpc_slice grpc_slice_intern(grpc_slice slice) {
  /* TODO(arjunroy): At present, this is capable of returning either a static or
     an interned slice. This yields weirdness like the constructor for
     ManagedMemorySlice instantiating itself as an instance of a derived type
     (StaticMetadataSlice or InternedSlice). Should reexamine. */
  return grpc_core::ManagedMemorySlice(&slice);
}

// Attempt to see if the provided slice or string matches a static slice.
// SliceArgs is either a const grpc_slice& or const pair<const char*, size_t>&.
// In either case, hash is the pre-computed hash value.
//
// Returns: a matching static slice, or null.
template <typename SliceArgs>
static const grpc_core::StaticMetadataSlice* MatchStaticSlice(
    uint32_t hash, const SliceArgs& args) {
  for (uint32_t i = 0; i <= max_static_metadata_hash_probe; i++) {
    static_metadata_hash_ent ent =
        static_metadata_hash[(hash + i) % GPR_ARRAY_SIZE(static_metadata_hash)];
    const grpc_core::StaticMetadataSlice* static_slice_table =
        grpc_static_slice_table();
    if (ent.hash == hash && ent.idx < GRPC_STATIC_MDSTR_COUNT &&
        static_slice_table[ent.idx] == args) {
      return &static_slice_table[ent.idx];
    }
  }
  return nullptr;
}

// Helper methods to enable us to select appropriately overloaded slice methods
// whether we're dealing with a slice, or a buffer with length, when interning
// strings. Helpers for FindOrCreateInternedSlice().
static const char* GetBuffer(const std::pair<const char*, size_t>& buflen) {
  return buflen.first;
}
static size_t GetLength(const std::pair<const char*, size_t>& buflen) {
  return buflen.second;
}
static const void* GetBuffer(const grpc_slice& slice) {
  return GRPC_SLICE_START_PTR(slice);
}
static size_t GetLength(const grpc_slice& slice) {
  return GRPC_SLICE_LENGTH(slice);
}

// Creates an interned slice for a string that does not currently exist in the
// intern table. SliceArgs is either a const grpc_slice& or a const
// pair<const char*, size_t>&. Hash is the pre-computed hash value. We must
// already hold the shard lock. Helper for FindOrCreateInternedSlice().
//
// Returns: a newly interned slice.
template <typename SliceArgs>
static InternedSliceRefcount* InternNewStringLocked(slice_shard* shard,
                                                    size_t shard_idx,
                                                    uint32_t hash,
                                                    const SliceArgs& args) {
  /* string data goes after the internal_string header */
  size_t len = GetLength(args);
  const void* buffer = GetBuffer(args);
  InternedSliceRefcount* s =
      static_cast<InternedSliceRefcount*>(gpr_malloc(sizeof(*s) + len));
  new (s) grpc_core::InternedSliceRefcount(len, hash, shard->strs[shard_idx]);
  // TODO(arjunroy): Investigate why hpack tried to intern the nullptr string.
  // https://github.com/grpc/grpc/pull/20110#issuecomment-526729282
  if (len > 0) {
    memcpy(reinterpret_cast<char*>(s + 1), buffer, len);
  }
  shard->strs[shard_idx] = s;
  shard->count++;
  if (shard->count > shard->capacity * 2) {
    grow_shard(shard);
  }
  return s;
}

// Attempt to see if the provided slice or string matches an existing interned
// slice. SliceArgs... is either a const grpc_slice& or a string and length. In
// either case, hash is the pre-computed hash value.  We must already hold the
// shard lock. Helper for FindOrCreateInternedSlice().
//
// Returns: a pre-existing matching static slice, or null.
template <typename SliceArgs>
static InternedSliceRefcount* MatchInternedSliceLocked(uint32_t hash,
                                                       size_t idx,
                                                       const SliceArgs& args) {
  InternedSliceRefcount* s;
  slice_shard* shard = &g_shards[SHARD_IDX(hash)];
  /* search for an existing string */
  for (s = shard->strs[idx]; s; s = s->bucket_next) {
    if (s->hash == hash && grpc_core::InternedSlice(s) == args) {
      if (s->refcnt.RefIfNonZero()) {
        return s;
      }
    }
  }
  return nullptr;
}

// Attempt to see if the provided slice or string matches an existing interned
// slice, and failing that, create an interned slice with its contents. Returns
// either the existing matching interned slice or the newly created one.
// SliceArgs is either a const grpc_slice& or const pair<const char*, size_t>&.
// In either case, hash is the pre-computed hash value. We do not hold the
// shard lock here, but do take it.
//
// Returns: an interned slice, either pre-existing/matched or newly created.
template <typename SliceArgs>
static InternedSliceRefcount* FindOrCreateInternedSlice(uint32_t hash,
                                                        const SliceArgs& args) {
  slice_shard* shard = &g_shards[SHARD_IDX(hash)];
  gpr_mu_lock(&shard->mu);
  const size_t idx = TABLE_IDX(hash, shard->capacity);
  InternedSliceRefcount* s = MatchInternedSliceLocked(hash, idx, args);
  if (s == nullptr) {
    s = InternNewStringLocked(shard, idx, hash, args);
  }
  gpr_mu_unlock(&shard->mu);
  return s;
}

grpc_core::ManagedMemorySlice::ManagedMemorySlice(const char* string)
    : grpc_core::ManagedMemorySlice::ManagedMemorySlice(string,
                                                        strlen(string)) {}

grpc_core::ManagedMemorySlice::ManagedMemorySlice(const char* string,
                                                  size_t len) {
  GPR_TIMER_SCOPE("grpc_slice_intern", 0);
  const uint32_t hash = gpr_murmur_hash3(string, len, g_hash_seed);
  const StaticMetadataSlice* static_slice =
      MatchStaticSlice(hash, std::pair<const char*, size_t>(string, len));
  if (static_slice) {
    *this = *static_slice;
  } else {
    *this = grpc_core::InternedSlice(FindOrCreateInternedSlice(
        hash, std::pair<const char*, size_t>(string, len)));
  }
}

grpc_core::ManagedMemorySlice::ManagedMemorySlice(const grpc_slice* slice_ptr) {
  GPR_TIMER_SCOPE("grpc_slice_intern", 0);
  const grpc_slice& slice = *slice_ptr;
  if (GRPC_IS_STATIC_METADATA_STRING(slice)) {
    *this = static_cast<const grpc_core::StaticMetadataSlice&>(slice);
    return;
  }
  const uint32_t hash = grpc_slice_hash_internal(slice);
  const StaticMetadataSlice* static_slice = MatchStaticSlice(hash, slice);
  if (static_slice) {
    *this = *static_slice;
  } else {
    *this = grpc_core::InternedSlice(FindOrCreateInternedSlice(hash, slice));
  }
}

void grpc_test_only_set_slice_hash_seed(uint32_t seed) {
  grpc_core::g_hash_seed = seed;
  grpc_core::g_forced_hash_seed = true;
}

void grpc_slice_intern_init(void) {
  if (!grpc_core::g_forced_hash_seed) {
    grpc_core::g_hash_seed =
        static_cast<uint32_t>(gpr_now(GPR_CLOCK_REALTIME).tv_nsec);
  }
  for (size_t i = 0; i < SHARD_COUNT; i++) {
    slice_shard* shard = &g_shards[i];
    gpr_mu_init(&shard->mu);
    shard->count = 0;
    shard->capacity = INITIAL_SHARD_CAPACITY;
    shard->strs = static_cast<InternedSliceRefcount**>(
        gpr_zalloc(sizeof(*shard->strs) * shard->capacity));
  }
  for (size_t i = 0; i < GPR_ARRAY_SIZE(static_metadata_hash); i++) {
    static_metadata_hash[i].hash = 0;
    static_metadata_hash[i].idx = GRPC_STATIC_MDSTR_COUNT;
  }
  max_static_metadata_hash_probe = 0;
  const grpc_core::StaticMetadataSlice* static_slice_table =
      grpc_static_slice_table();
  for (size_t i = 0; i < GRPC_STATIC_MDSTR_COUNT; i++) {
    grpc_static_metadata_hash_values[i] =
        grpc_slice_default_hash_internal(static_slice_table[i]);
    for (size_t j = 0; j < GPR_ARRAY_SIZE(static_metadata_hash); j++) {
      size_t slot = (grpc_static_metadata_hash_values[i] + j) %
                    GPR_ARRAY_SIZE(static_metadata_hash);
      if (static_metadata_hash[slot].idx == GRPC_STATIC_MDSTR_COUNT) {
        static_metadata_hash[slot].hash = grpc_static_metadata_hash_values[i];
        static_metadata_hash[slot].idx = static_cast<uint32_t>(i);
        if (j > max_static_metadata_hash_probe) {
          max_static_metadata_hash_probe = static_cast<uint32_t>(j);
        }
        break;
      }
    }
  }
  // Handle KV hash for all static mdelems.
  for (size_t i = 0; i < GRPC_STATIC_MDELEM_COUNT; ++i) {
    grpc_static_mdelem_table()[i].HashInit();
  }
}

void grpc_slice_intern_shutdown(void) {
  for (size_t i = 0; i < SHARD_COUNT; i++) {
    slice_shard* shard = &g_shards[i];
    gpr_mu_destroy(&shard->mu);
    /* TODO(ctiller): GPR_ASSERT(shard->count == 0); */
    if (shard->count != 0) {
      gpr_log(GPR_DEBUG, "WARNING: %" PRIuPTR " metadata strings were leaked",
              shard->count);
      for (size_t j = 0; j < shard->capacity; j++) {
        for (InternedSliceRefcount* s = shard->strs[j]; s; s = s->bucket_next) {
          char* text = grpc_dump_slice(grpc_core::InternedSlice(s),
                                       GPR_DUMP_HEX | GPR_DUMP_ASCII);
          gpr_log(GPR_DEBUG, "LEAKED: %s", text);
          gpr_free(text);
        }
      }
      if (grpc_iomgr_abort_on_leaks()) {
        abort();
      }
    }
    gpr_free(shard->strs);
  }
}
