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

#include <inttypes.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/murmur_hash.h"
#include "src/core/lib/iomgr/iomgr_internal.h" /* for iomgr_abort_on_leaks() */
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/static_metadata.h"

#define LOG2_SHARD_COUNT 5
#define SHARD_COUNT (1 << LOG2_SHARD_COUNT)
#define INITIAL_SHARD_CAPACITY 8

#define TABLE_IDX(hash, capacity) (((hash) >> LOG2_SHARD_COUNT) % (capacity))
#define SHARD_IDX(hash) ((hash) & ((1 << LOG2_SHARD_COUNT) - 1))

typedef struct interned_slice_refcount {
  grpc_slice_refcount base;
  grpc_slice_refcount sub;
  size_t length;
  gpr_atm refcnt;
  uint32_t hash;
  struct interned_slice_refcount* bucket_next;
} interned_slice_refcount;

typedef struct slice_shard {
  gpr_mu mu;
  interned_slice_refcount** strs;
  size_t count;
  size_t capacity;
} slice_shard;

/* hash seed: decided at initialization time */
static uint32_t g_hash_seed;
static int g_forced_hash_seed = 0;

static slice_shard g_shards[SHARD_COUNT];

typedef struct {
  uint32_t hash;
  uint32_t idx;
} static_metadata_hash_ent;

static static_metadata_hash_ent
    static_metadata_hash[4 * GRPC_STATIC_MDSTR_COUNT];
static uint32_t max_static_metadata_hash_probe;
static uint32_t static_metadata_hash_values[GRPC_STATIC_MDSTR_COUNT];

static void interned_slice_ref(void* p) {
  interned_slice_refcount* s = static_cast<interned_slice_refcount*>(p);
  GPR_ASSERT(gpr_atm_no_barrier_fetch_add(&s->refcnt, 1) > 0);
}

static void interned_slice_destroy(interned_slice_refcount* s) {
  slice_shard* shard = &g_shards[SHARD_IDX(s->hash)];
  gpr_mu_lock(&shard->mu);
  GPR_ASSERT(0 == gpr_atm_no_barrier_load(&s->refcnt));
  interned_slice_refcount** prev_next;
  interned_slice_refcount* cur;
  for (prev_next = &shard->strs[TABLE_IDX(s->hash, shard->capacity)],
      cur = *prev_next;
       cur != s; prev_next = &cur->bucket_next, cur = cur->bucket_next)
    ;
  *prev_next = cur->bucket_next;
  shard->count--;
  gpr_free(s);
  gpr_mu_unlock(&shard->mu);
}

static void interned_slice_unref(void* p) {
  interned_slice_refcount* s = static_cast<interned_slice_refcount*>(p);
  if (1 == gpr_atm_full_fetch_add(&s->refcnt, -1)) {
    interned_slice_destroy(s);
  }
}

static void interned_slice_sub_ref(void* p) {
  interned_slice_ref((static_cast<char*>(p)) -
                     offsetof(interned_slice_refcount, sub));
}

static void interned_slice_sub_unref(void* p) {
  interned_slice_unref((static_cast<char*>(p)) -
                       offsetof(interned_slice_refcount, sub));
}

static uint32_t interned_slice_hash(grpc_slice slice) {
  interned_slice_refcount* s =
      reinterpret_cast<interned_slice_refcount*>(slice.refcount);
  return s->hash;
}

static int interned_slice_eq(grpc_slice a, grpc_slice b) {
  return a.refcount == b.refcount;
}

static const grpc_slice_refcount_vtable interned_slice_vtable = {
    interned_slice_ref, interned_slice_unref, interned_slice_eq,
    interned_slice_hash};
static const grpc_slice_refcount_vtable interned_slice_sub_vtable = {
    interned_slice_sub_ref, interned_slice_sub_unref,
    grpc_slice_default_eq_impl, grpc_slice_default_hash_impl};

static void grow_shard(slice_shard* shard) {
  GPR_TIMER_SCOPE("grow_strtab", 0);

  size_t capacity = shard->capacity * 2;
  size_t i;
  interned_slice_refcount** strtab;
  interned_slice_refcount *s, *next;

  strtab = static_cast<interned_slice_refcount**>(
      gpr_zalloc(sizeof(interned_slice_refcount*) * capacity));

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

static grpc_slice materialize(interned_slice_refcount* s) {
  grpc_slice slice;
  slice.refcount = &s->base;
  slice.data.refcounted.bytes = reinterpret_cast<uint8_t*>(s + 1);
  slice.data.refcounted.length = s->length;
  return slice;
}

uint32_t grpc_slice_default_hash_impl(grpc_slice s) {
  return gpr_murmur_hash3(GRPC_SLICE_START_PTR(s), GRPC_SLICE_LENGTH(s),
                          g_hash_seed);
}

uint32_t grpc_static_slice_hash(grpc_slice s) {
  return static_metadata_hash_values[GRPC_STATIC_METADATA_INDEX(s)];
}

int grpc_static_slice_eq(grpc_slice a, grpc_slice b) {
  return GRPC_STATIC_METADATA_INDEX(a) == GRPC_STATIC_METADATA_INDEX(b);
}

uint32_t grpc_slice_hash(grpc_slice s) {
  return s.refcount == nullptr ? grpc_slice_default_hash_impl(s)
                               : s.refcount->vtable->hash(s);
}

grpc_slice grpc_slice_maybe_static_intern(grpc_slice slice,
                                          bool* returned_slice_is_different) {
  if (GRPC_IS_STATIC_METADATA_STRING(slice)) {
    return slice;
  }

  uint32_t hash = grpc_slice_hash(slice);
  for (uint32_t i = 0; i <= max_static_metadata_hash_probe; i++) {
    static_metadata_hash_ent ent =
        static_metadata_hash[(hash + i) % GPR_ARRAY_SIZE(static_metadata_hash)];
    if (ent.hash == hash && ent.idx < GRPC_STATIC_MDSTR_COUNT &&
        grpc_slice_eq(grpc_static_slice_table[ent.idx], slice)) {
      *returned_slice_is_different = true;
      return grpc_static_slice_table[ent.idx];
    }
  }

  return slice;
}

bool grpc_slice_is_interned(grpc_slice slice) {
  return (slice.refcount && slice.refcount->vtable == &interned_slice_vtable) ||
         GRPC_IS_STATIC_METADATA_STRING(slice);
}

grpc_slice grpc_slice_intern(grpc_slice slice) {
  GPR_TIMER_SCOPE("grpc_slice_intern", 0);
  if (GRPC_IS_STATIC_METADATA_STRING(slice)) {
    return slice;
  }

  uint32_t hash = grpc_slice_hash(slice);
  for (uint32_t i = 0; i <= max_static_metadata_hash_probe; i++) {
    static_metadata_hash_ent ent =
        static_metadata_hash[(hash + i) % GPR_ARRAY_SIZE(static_metadata_hash)];
    if (ent.hash == hash && ent.idx < GRPC_STATIC_MDSTR_COUNT &&
        grpc_slice_eq(grpc_static_slice_table[ent.idx], slice)) {
      return grpc_static_slice_table[ent.idx];
    }
  }

  interned_slice_refcount* s;
  slice_shard* shard = &g_shards[SHARD_IDX(hash)];

  gpr_mu_lock(&shard->mu);

  /* search for an existing string */
  size_t idx = TABLE_IDX(hash, shard->capacity);
  for (s = shard->strs[idx]; s; s = s->bucket_next) {
    if (s->hash == hash && grpc_slice_eq(slice, materialize(s))) {
      if (gpr_atm_no_barrier_fetch_add(&s->refcnt, 1) == 0) {
        /* If we get here, we've added a ref to something that was about to
         * die - drop it immediately.
         * The *only* possible path here (given the shard mutex) should be to
         * drop from one ref back to zero - assert that with a CAS */
        GPR_ASSERT(gpr_atm_rel_cas(&s->refcnt, 1, 0));
        /* and treat this as if we were never here... sshhh */
      } else {
        gpr_mu_unlock(&shard->mu);
        return materialize(s);
      }
    }
  }

  /* not found: create a new string */
  /* string data goes after the internal_string header */
  s = static_cast<interned_slice_refcount*>(
      gpr_malloc(sizeof(*s) + GRPC_SLICE_LENGTH(slice)));
  gpr_atm_rel_store(&s->refcnt, 1);
  s->length = GRPC_SLICE_LENGTH(slice);
  s->hash = hash;
  s->base.vtable = &interned_slice_vtable;
  s->base.sub_refcount = &s->sub;
  s->sub.vtable = &interned_slice_sub_vtable;
  s->sub.sub_refcount = &s->sub;
  s->bucket_next = shard->strs[idx];
  shard->strs[idx] = s;
  memcpy(s + 1, GRPC_SLICE_START_PTR(slice), GRPC_SLICE_LENGTH(slice));

  shard->count++;

  if (shard->count > shard->capacity * 2) {
    grow_shard(shard);
  }

  gpr_mu_unlock(&shard->mu);

  return materialize(s);
}

void grpc_test_only_set_slice_hash_seed(uint32_t seed) {
  g_hash_seed = seed;
  g_forced_hash_seed = 1;
}

void grpc_slice_intern_init(void) {
  if (!g_forced_hash_seed) {
    g_hash_seed = static_cast<uint32_t>(gpr_now(GPR_CLOCK_REALTIME).tv_nsec);
  }
  for (size_t i = 0; i < SHARD_COUNT; i++) {
    slice_shard* shard = &g_shards[i];
    gpr_mu_init(&shard->mu);
    shard->count = 0;
    shard->capacity = INITIAL_SHARD_CAPACITY;
    shard->strs = static_cast<interned_slice_refcount**>(
        gpr_zalloc(sizeof(*shard->strs) * shard->capacity));
  }
  for (size_t i = 0; i < GPR_ARRAY_SIZE(static_metadata_hash); i++) {
    static_metadata_hash[i].hash = 0;
    static_metadata_hash[i].idx = GRPC_STATIC_MDSTR_COUNT;
  }
  max_static_metadata_hash_probe = 0;
  for (size_t i = 0; i < GRPC_STATIC_MDSTR_COUNT; i++) {
    static_metadata_hash_values[i] =
        grpc_slice_default_hash_impl(grpc_static_slice_table[i]);
    for (size_t j = 0; j < GPR_ARRAY_SIZE(static_metadata_hash); j++) {
      size_t slot = (static_metadata_hash_values[i] + j) %
                    GPR_ARRAY_SIZE(static_metadata_hash);
      if (static_metadata_hash[slot].idx == GRPC_STATIC_MDSTR_COUNT) {
        static_metadata_hash[slot].hash = static_metadata_hash_values[i];
        static_metadata_hash[slot].idx = static_cast<uint32_t>(i);
        if (j > max_static_metadata_hash_probe) {
          max_static_metadata_hash_probe = static_cast<uint32_t>(j);
        }
        break;
      }
    }
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
        for (interned_slice_refcount* s = shard->strs[j]; s;
             s = s->bucket_next) {
          char* text =
              grpc_dump_slice(materialize(s), GPR_DUMP_HEX | GPR_DUMP_ASCII);
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
