/*
 *
 * Copyright 2015, Google Inc.
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

#include "src/core/lib/transport/metadata.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/support/murmur_hash.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/transport/static_metadata.h"

/* There are two kinds of mdelem and mdstr instances.
 * Static instances are declared in static_metadata.{h,c} and
 * are initialized by grpc_mdctx_global_init().
 * Dynamic instances are stored in hash tables on grpc_mdctx, and are backed
 * by internal_string and internal_element structures.
 * Internal helper functions here-in (is_mdstr_static, is_mdelem_static) are
 * used to determine which kind of element a pointer refers to.
 */

#ifdef GRPC_METADATA_REFCOUNT_DEBUG
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

typedef void (*destroy_user_data_func)(void *user_data);

/* Shadow structure for grpc_mdelem for non-static elements */
typedef struct internal_metadata {
  /* must be byte compatible with grpc_mdelem */
  grpc_slice key;
  grpc_slice value;

  /* private only data */
  gpr_atm refcnt;

  gpr_mu mu_user_data;
  gpr_atm destroy_user_data;
  gpr_atm user_data;

  struct internal_metadata *bucket_next;
} internal_metadata;

typedef struct mdtab_shard {
  gpr_mu mu;
  internal_metadata **elems;
  size_t count;
  size_t capacity;
  /** Estimate of the number of unreferenced mdelems in the hash table.
      This will eventually converge to the exact number, but it's instantaneous
      accuracy is not guaranteed */
  gpr_atm free_estimate;
} mdtab_shard;

/* hash seed: decided at initialization time */
static uint32_t g_hash_seed;
static int g_forced_hash_seed = 0;

/* linearly probed hash tables for static element lookup */
static grpc_mdelem *g_static_mdtab[GRPC_STATIC_MDELEM_COUNT * 2];
static size_t g_static_strtab_maxprobe;
static size_t g_static_mdtab_maxprobe;

static mdtab_shard g_shards[SHARD_COUNT];

static void gc_mdtab(grpc_exec_ctx *exec_ctx, mdtab_shard *shard);

void grpc_test_only_set_metadata_hash_seed(uint32_t seed) {
  g_hash_seed = seed;
  g_forced_hash_seed = 1;
}

void grpc_mdctx_global_init(void) {
  size_t i;
  if (!g_forced_hash_seed) {
    g_hash_seed = (uint32_t)gpr_now(GPR_CLOCK_REALTIME).tv_nsec;
  }
  g_static_strtab_maxprobe = 0;
  g_static_mdtab_maxprobe = 0;
#if 0
  /* build static tables */
  memset(g_static_mdtab, 0, sizeof(g_static_mdtab));
  memset(g_static_strtab, 0, sizeof(g_static_strtab));
  for (i = 0; i < GRPC_STATIC_MDSTR_COUNT; i++) {
    grpc_slice elem = &grpc_static_mdstr_table[i];
    const char *str = grpc_static_metadata_strings[i];
    uint32_t hash = gpr_murmur_hash3(str, strlen(str), g_hash_seed);
    *(grpc_slice *)&elem->slice = grpc_slice_from_static_string(str);
    *(uint32_t *)&elem->hash = hash;
    for (j = 0;; j++) {
      size_t idx = (hash + j) % GPR_ARRAY_SIZE(g_static_strtab);
      if (g_static_strtab[idx] == NULL) {
        g_static_strtab[idx] = &grpc_static_mdstr_table[i];
        break;
      }
    }
    if (j > g_static_strtab_maxprobe) {
      g_static_strtab_maxprobe = j;
    }
  }
  for (i = 0; i < GRPC_STATIC_MDELEM_COUNT; i++) {
    grpc_mdelem *elem = &grpc_static_mdelem_table[i];
    grpc_slice key =
        &grpc_static_mdstr_table[grpc_static_metadata_elem_indices[2 * i + 0]];
    grpc_slice value =
        &grpc_static_mdstr_table[grpc_static_metadata_elem_indices[2 * i + 1]];
    uint32_t hash = GRPC_MDSTR_KV_HASH(key->hash, value->hash);
    *(grpc_slice *)&elem->key = key;
    *(grpc_slice *)&elem->value = value;
    for (j = 0;; j++) {
      size_t idx = (hash + j) % GPR_ARRAY_SIZE(g_static_mdtab);
      if (g_static_mdtab[idx] == NULL) {
        g_static_mdtab[idx] = elem;
        break;
      }
    }
    if (j > g_static_mdtab_maxprobe) {
      g_static_mdtab_maxprobe = j;
    }
  }
#endif
  /* initialize shards */
  for (i = 0; i < SHARD_COUNT; i++) {
    mdtab_shard *shard = &g_shards[i];
    gpr_mu_init(&shard->mu);
    shard->count = 0;
    gpr_atm_no_barrier_store(&shard->free_estimate, 0);
    shard->capacity = INITIAL_SHARD_CAPACITY;
    shard->elems = gpr_malloc(sizeof(*shard->elems) * shard->capacity);
    memset(shard->elems, 0, sizeof(*shard->elems) * shard->capacity);
  }
}

void grpc_mdctx_global_shutdown(grpc_exec_ctx *exec_ctx) {
  size_t i;
  for (i = 0; i < SHARD_COUNT; i++) {
    mdtab_shard *shard = &g_shards[i];
    gpr_mu_destroy(&shard->mu);
    gc_mdtab(exec_ctx, shard);
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

static int is_mdelem_static(grpc_mdelem *e) {
  return e >= &grpc_static_mdelem_table[0] &&
         e < &grpc_static_mdelem_table[GRPC_STATIC_MDELEM_COUNT];
}

static void ref_md_locked(mdtab_shard *shard,
                          internal_metadata *md DEBUG_ARGS) {
#ifdef GRPC_METADATA_REFCOUNT_DEBUG
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
          "ELM   REF:%p:%zu->%zu: '%s' = '%s'", (void *)md,
          gpr_atm_no_barrier_load(&md->refcnt),
          gpr_atm_no_barrier_load(&md->refcnt) + 1,
          grpc_mdstr_as_c_string((grpc_slice)md->key),
          grpc_mdstr_as_c_string((grpc_slice)md->value));
#endif
  if (0 == gpr_atm_no_barrier_fetch_add(&md->refcnt, 1)) {
    gpr_atm_no_barrier_fetch_add(&shard->free_estimate, -1);
  }
}

static void gc_mdtab(grpc_exec_ctx *exec_ctx, mdtab_shard *shard) {
  size_t i;
  internal_metadata **prev_next;
  internal_metadata *md, *next;
  gpr_atm num_freed = 0;

  GPR_TIMER_BEGIN("gc_mdtab", 0);
  for (i = 0; i < shard->capacity; i++) {
    prev_next = &shard->elems[i];
    for (md = shard->elems[i]; md; md = next) {
      void *user_data = (void *)gpr_atm_no_barrier_load(&md->user_data);
      next = md->bucket_next;
      if (gpr_atm_acq_load(&md->refcnt) == 0) {
        grpc_slice_unref_internal(exec_ctx, md->key);
        grpc_slice_unref_internal(exec_ctx, md->value);
        if (md->user_data) {
          ((destroy_user_data_func)gpr_atm_no_barrier_load(
              &md->destroy_user_data))(user_data);
        }
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
  GPR_TIMER_END("gc_mdtab", 0);
}

static void grow_mdtab(mdtab_shard *shard) {
  size_t capacity = shard->capacity * 2;
  size_t i;
  internal_metadata **mdtab;
  internal_metadata *md, *next;
  uint32_t hash;

  GPR_TIMER_BEGIN("grow_mdtab", 0);

  mdtab = gpr_malloc(sizeof(internal_metadata *) * capacity);
  memset(mdtab, 0, sizeof(internal_metadata *) * capacity);

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

  GPR_TIMER_END("grow_mdtab", 0);
}

static void rehash_mdtab(grpc_exec_ctx *exec_ctx, mdtab_shard *shard) {
  if (gpr_atm_no_barrier_load(&shard->free_estimate) >
      (gpr_atm)(shard->capacity / 4)) {
    gc_mdtab(exec_ctx, shard);
  } else {
    grow_mdtab(shard);
  }
}

grpc_mdelem *grpc_mdelem_from_slices(grpc_exec_ctx *exec_ctx, grpc_slice key,
                                     grpc_slice value) {
  uint32_t hash =
      GRPC_MDSTR_KV_HASH(grpc_slice_hash(key), grpc_slice_hash(value));
  internal_metadata *md;
  mdtab_shard *shard = &g_shards[SHARD_IDX(hash)];
  size_t i;
  size_t idx;

  GPR_TIMER_BEGIN("grpc_mdelem_from_metadata_strings", 0);

  if (grpc_is_static_metadata_string(key) &&
      grpc_is_static_metadata_string(value)) {
    for (i = 0; i <= g_static_mdtab_maxprobe; i++) {
      grpc_mdelem *smd;
      idx = (hash + i) % GPR_ARRAY_SIZE(g_static_mdtab);
      smd = g_static_mdtab[idx];
      if (smd == NULL) break;
      if (grpc_slice_cmp(key, smd->key) == 0 &&
          grpc_slice_cmp(value, smd->value) == 0) {
        GPR_TIMER_END("grpc_mdelem_from_metadata_strings", 0);
        return smd;
      }
    }
  }

  gpr_mu_lock(&shard->mu);

  idx = TABLE_IDX(hash, shard->capacity);
  /* search for an existing pair */
  for (md = shard->elems[idx]; md; md = md->bucket_next) {
    if (grpc_slice_cmp(key, md->key) == 0 &&
        grpc_slice_cmp(value, md->value) == 0) {
      REF_MD_LOCKED(shard, md);
      gpr_mu_unlock(&shard->mu);
      grpc_slice_unref_internal(exec_ctx, key);
      grpc_slice_unref_internal(exec_ctx, value);
      GPR_TIMER_END("grpc_mdelem_from_metadata_strings", 0);
      return (grpc_mdelem *)md;
    }
  }

  /* not found: create a new pair */
  md = gpr_malloc(sizeof(internal_metadata));
  gpr_atm_rel_store(&md->refcnt, 1);
  md->key = key;
  md->value = value;
  md->user_data = 0;
  md->destroy_user_data = 0;
  md->bucket_next = shard->elems[idx];
  shard->elems[idx] = md;
  gpr_mu_init(&md->mu_user_data);
#ifdef GRPC_METADATA_REFCOUNT_DEBUG
  gpr_log(GPR_DEBUG, "ELM   NEW:%p:%zu: '%s' = '%s'", (void *)md,
          gpr_atm_no_barrier_load(&md->refcnt),
          grpc_mdstr_as_c_string((grpc_slice)md->key),
          grpc_mdstr_as_c_string((grpc_slice)md->value));
#endif
  shard->count++;

  if (shard->count > shard->capacity * 2) {
    rehash_mdtab(exec_ctx, shard);
  }

  gpr_mu_unlock(&shard->mu);

  GPR_TIMER_END("grpc_mdelem_from_metadata_strings", 0);

  return (grpc_mdelem *)md;
}

static size_t get_base64_encoded_size(size_t raw_length) {
  static const uint8_t tail_xtra[3] = {0, 2, 3};
  return raw_length / 3 * 4 + tail_xtra[raw_length % 3];
}

size_t grpc_mdelem_get_size_in_hpack_table(grpc_mdelem *elem) {
  size_t overhead_and_key = 32 + GRPC_SLICE_LENGTH(elem->key);
  size_t value_len = GRPC_SLICE_LENGTH(elem->value);
  if (grpc_is_binary_header((const char *)GRPC_SLICE_START_PTR(elem->key),
                            GRPC_SLICE_LENGTH(elem->key))) {
    return overhead_and_key + get_base64_encoded_size(value_len);
  } else {
    return overhead_and_key + value_len;
  }
}

grpc_mdelem *grpc_mdelem_ref(grpc_mdelem *gmd DEBUG_ARGS) {
  internal_metadata *md = (internal_metadata *)gmd;
  if (is_mdelem_static(gmd)) return gmd;
#ifdef GRPC_METADATA_REFCOUNT_DEBUG
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
          "ELM   REF:%p:%zu->%zu: '%s' = '%s'", (void *)md,
          gpr_atm_no_barrier_load(&md->refcnt),
          gpr_atm_no_barrier_load(&md->refcnt) + 1,
          grpc_mdstr_as_c_string((grpc_slice)md->key),
          grpc_mdstr_as_c_string((grpc_slice)md->value));
#endif
  /* we can assume the ref count is >= 1 as the application is calling
     this function - meaning that no adjustment to mdtab_free is necessary,
     simplifying the logic here to be just an atomic increment */
  /* use C assert to have this removed in opt builds */
  GPR_ASSERT(gpr_atm_no_barrier_load(&md->refcnt) >= 1);
  gpr_atm_no_barrier_fetch_add(&md->refcnt, 1);
  return gmd;
}

void grpc_mdelem_unref(grpc_exec_ctx *exec_ctx, grpc_mdelem *gmd DEBUG_ARGS) {
  internal_metadata *md = (internal_metadata *)gmd;
  if (!md) return;
  if (is_mdelem_static(gmd)) return;
#ifdef GRPC_METADATA_REFCOUNT_DEBUG
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
          "ELM UNREF:%p:%zu->%zu: '%s' = '%s'", (void *)md,
          gpr_atm_no_barrier_load(&md->refcnt),
          gpr_atm_no_barrier_load(&md->refcnt) - 1,
          grpc_mdstr_as_c_string((grpc_slice)md->key),
          grpc_mdstr_as_c_string((grpc_slice)md->value));
#endif
  uint32_t hash =
      GRPC_MDSTR_KV_HASH(grpc_slice_hash(md->key), grpc_slice_hash(md->value));
  const gpr_atm prev_refcount = gpr_atm_full_fetch_add(&md->refcnt, -1);
  GPR_ASSERT(prev_refcount >= 1);
  if (1 == prev_refcount) {
    /* once the refcount hits zero, some other thread can come along and
       free md at any time: it's unsafe from this point on to access it */
    mdtab_shard *shard = &g_shards[SHARD_IDX(hash)];
    gpr_atm_no_barrier_fetch_add(&shard->free_estimate, 1);
  }
}

void *grpc_mdelem_get_user_data(grpc_mdelem *md, void (*destroy_func)(void *)) {
  internal_metadata *im = (internal_metadata *)md;
  void *result;
  if (is_mdelem_static(md)) {
    return (void *)grpc_static_mdelem_user_data[md - grpc_static_mdelem_table];
  }
  if (gpr_atm_acq_load(&im->destroy_user_data) == (gpr_atm)destroy_func) {
    return (void *)gpr_atm_no_barrier_load(&im->user_data);
  } else {
    return NULL;
  }
  return result;
}

void grpc_mdelem_set_user_data(grpc_mdelem *md, void (*destroy_func)(void *),
                               void *user_data) {
  internal_metadata *im = (internal_metadata *)md;
  GPR_ASSERT(!is_mdelem_static(md));
  GPR_ASSERT((user_data == NULL) == (destroy_func == NULL));
  gpr_mu_lock(&im->mu_user_data);
  if (gpr_atm_no_barrier_load(&im->destroy_user_data)) {
    /* user data can only be set once */
    gpr_mu_unlock(&im->mu_user_data);
    if (destroy_func != NULL) {
      destroy_func(user_data);
    }
    return;
  }
  gpr_atm_no_barrier_store(&im->user_data, (gpr_atm)user_data);
  gpr_atm_rel_store(&im->destroy_user_data, (gpr_atm)destroy_func);
  gpr_mu_unlock(&im->mu_user_data);
}
