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

#include "src/core/lib/slice/slice_internal.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/iomgr_internal.h" /* for iomgr_abort_on_leaks() */
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/murmur_hash.h"
#include "src/core/lib/transport/static_metadata.h"

#define LOG2_SHARD_COUNT 5
#define SHARD_COUNT (1 << LOG2_SHARD_COUNT)
#define INITIAL_SHARD_CAPACITY 8

#define TABLE_IDX(hash, capacity) (((hash) >> LOG2_SHARD_COUNT) % (capacity))
#define SHARD_IDX(hash) ((hash) & ((1 << LOG2_SHARD_COUNT) - 1))

typedef struct interned_slice_refcount {
  grpc_slice_refcount base;
  size_t length;
  gpr_atm refcnt;
  uint32_t hash;
  struct interned_slice_refcount *bucket_next;
} interned_slice_refcount;

typedef struct slice_shard {
  gpr_mu mu;
  interned_slice_refcount **strs;
  size_t count;
  size_t capacity;
} slice_shard;

/* hash seed: decided at initialization time */
static uint32_t g_hash_seed;
static int g_forced_hash_seed = 0;

static slice_shard g_shards[SHARD_COUNT];

static void interned_slice_ref(void *p) {
  interned_slice_refcount *s = p;
  GPR_ASSERT(gpr_atm_no_barrier_fetch_add(&s->refcnt, 1) > 0);
}

static void interned_slice_destroy(interned_slice_refcount *s) {
  slice_shard *shard = &g_shards[SHARD_IDX(s->hash)];
  gpr_mu_lock(&shard->mu);
  GPR_ASSERT(0 == gpr_atm_no_barrier_load(&s->refcnt));
  interned_slice_refcount **prev_next;
  interned_slice_refcount *cur;
  for (prev_next = &shard->strs[TABLE_IDX(s->hash, shard->capacity)],
      cur = *prev_next;
       cur != s; prev_next = &cur->bucket_next, cur = cur->bucket_next)
    ;
  *prev_next = cur->bucket_next;
  shard->count--;
  gpr_free(s);
  gpr_mu_unlock(&shard->mu);
}

static void interned_slice_unref(grpc_exec_ctx *exec_ctx, void *p) {
  interned_slice_refcount *s = p;
  if (1 == gpr_atm_full_fetch_add(&s->refcnt, -1)) {
    interned_slice_destroy(s);
  }
}

static void grow_shard(slice_shard *shard) {
  size_t capacity = shard->capacity * 2;
  size_t i;
  interned_slice_refcount **strtab;
  interned_slice_refcount *s, *next;

  GPR_TIMER_BEGIN("grow_strtab", 0);

  strtab = gpr_malloc(sizeof(interned_slice_refcount *) * capacity);
  memset(strtab, 0, sizeof(interned_slice_refcount *) * capacity);

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

  GPR_TIMER_END("grow_strtab", 0);
}

static grpc_slice materialize(interned_slice_refcount *s) {
  grpc_slice slice;
  slice.refcount = &s->base;
  slice.data.refcounted.bytes = (uint8_t *)(s + 1);
  slice.data.refcounted.length = s->length;
  return slice;
}

grpc_slice grpc_slice_intern(grpc_slice slice) {
  interned_slice_refcount *s;
  uint32_t hash = gpr_murmur_hash3(GRPC_SLICE_START_PTR(slice),
                                   GRPC_SLICE_LENGTH(slice), g_hash_seed);
  slice_shard *shard = &g_shards[SHARD_IDX(hash)];

  gpr_mu_lock(&shard->mu);

  /* search for an existing string */
  size_t idx = TABLE_IDX(hash, shard->capacity);
  for (s = shard->strs[idx]; s; s = s->bucket_next) {
    if (s->hash == hash && grpc_slice_cmp(slice, materialize(s)) == 0) {
      if (gpr_atm_no_barrier_fetch_add(&s->refcnt, 1) == 0) {
        /* If we get here, we've added a ref to something that was about to
         * die - drop it immediately.
         * The *only* possible path here (given the shard mutex) should be to
         * drop from one ref back to zero - assert that with a CAS */
        GPR_ASSERT(gpr_atm_rel_cas(&s->refcnt, 1, 0));
        /* and treat this as if we were never here... sshhh */
      } else {
        gpr_mu_unlock(&shard->mu);
        GPR_TIMER_END("grpc_mdstr_from_buffer", 0);
        return materialize(s);
      }
    }
  }

  /* not found: create a new string */
  /* string data goes after the internal_string header */
  s = gpr_malloc(sizeof(*s) + GRPC_SLICE_LENGTH(slice));
  gpr_atm_rel_store(&s->refcnt, 1);
  s->length = GRPC_SLICE_LENGTH(slice);
  s->hash = hash;
  s->base.ref = interned_slice_ref;
  s->base.unref = interned_slice_unref;
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

void grpc_test_only_set_slice_interning_hash_seed(uint32_t seed) {
  g_hash_seed = seed;
  g_forced_hash_seed = 1;
}

void grpc_slice_intern_init(void) {
  for (size_t i = 0; i < SHARD_COUNT; i++) {
    slice_shard *shard = &g_shards[i];
    gpr_mu_init(&shard->mu);
    shard->count = 0;
    shard->capacity = INITIAL_SHARD_CAPACITY;
    shard->strs = gpr_malloc(sizeof(*shard->strs) * shard->capacity);
    memset(shard->strs, 0, sizeof(*shard->strs) * shard->capacity);
  }
}

void grpc_slice_intern_shutdown(void) {
  for (size_t i = 0; i < SHARD_COUNT; i++) {
    slice_shard *shard = &g_shards[i];
    gpr_mu_destroy(&shard->mu);
    /* TODO(ctiller): GPR_ASSERT(shard->count == 0); */
    if (shard->count != 0) {
      gpr_log(GPR_DEBUG, "WARNING: %" PRIuPTR " metadata strings were leaked",
              shard->count);
      for (size_t j = 0; j < shard->capacity; j++) {
        for (interned_slice_refcount *s = shard->strs[j]; s;
             s = s->bucket_next) {
          char *text =
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
