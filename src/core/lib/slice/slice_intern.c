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
#include "src/core/lib/transport/static_metadata.h"

#define LOG2_SHARD_COUNT 5
#define SHARD_COUNT (1 << LOG2_SHARD_COUNT)

#define TABLE_IDX(hash, capacity) (((hash) >> LOG2_SHARD_COUNT) % (capacity))
#define SHARD_IDX(hash) ((hash) & ((1 << LOG2_SHARD_COUNT) - 1))

typedef struct interned_slice_refcount {
  grpc_slice_refcount base;
  uint32_t hash;
  gpr_atm refcnt;
  struct interned_slice_refcount *bucket_next;
  grpc_slice_refcount *source_refcount;
} interned_slice_refcount;

typedef struct slice_shard {
  gpr_mu mu;
  interned_slice_refcount **strs;
  size_t count;
  size_t capacity;
} slice_shard;

#define REFCOUNT_TO_SLICE(rc) (*(grpc_slice *)((rc) + 1))

/* hash seed: decided at initialization time */
static uint32_t g_hash_seed;
static int g_forced_hash_seed = 0;

static slice_shard g_shards[SHARD_COUNT];

/* linearly probed hash tables for static string lookup */
static grpc_slice g_static_slices[GRPC_STATIC_MDSTR_COUNT * 2];

grpc_slice grpc_slice_intern(grpc_slice slice) {
  uint32_t hash =
      gpr_murmur_hash3(GRPC_SLICE_START_PTR(slice), GRPC_SLICE_LENGTH(slice));
  slice_shard *shard = &g_shards[SHARD_IDX(hash)];

  gpr_mu_lock(&shard->mu);

  /* search for an existing string */
  size_t idx = TABLE_IDX(hash, shard->capacity);
  for (interned_slice_refcount *s = shard->strs[idx]; s; s = s->bucket_next) {
    if (s->hash == hash && grpc_slice_cmp(slice, REFCOUNT_TO_SLICE(s))) {
      if (gpr_atm_full_fetch_add(&s->refcnt, 1) == 0) {
        /* If we get here, we've added a ref to something that was about to
         * die - drop it immediately.
         * The *only* possible path here (given the shard mutex) should be to
         * drop from one ref back to zero - assert that with a CAS */
        GPR_ASSERT(gpr_atm_rel_cas(&s->refcnt, 1, 0));
        /* and treat this as if we were never here... sshhh */
      } else {
        gpr_mu_unlock(&shard->mu);
        GPR_TIMER_END("grpc_mdstr_from_buffer", 0);
        return REFCOUNT_TO_SLICE(s);
      }
    }
  }

  /* not found: create a new string */
  if (length + 1 < GRPC_SLICE_INLINED_SIZE) {
    /* string data goes directly into the slice */
    s = gpr_malloc(sizeof(internal_string));
    gpr_atm_rel_store(&s->refcnt, 1);
    s->slice.refcount = NULL;
    memcpy(s->slice.data.inlined.bytes, buf, length);
    s->slice.data.inlined.bytes[length] = 0;
    s->slice.data.inlined.length = (uint8_t)length;
  } else {
    /* string data goes after the internal_string header, and we +1 for null
       terminator */
    s = gpr_malloc(sizeof(internal_string) + length + 1);
    gpr_atm_rel_store(&s->refcnt, 1);
    s->refcount.ref = slice_ref;
    s->refcount.unref = slice_unref;
    s->slice.refcount = &s->refcount;
    s->slice.data.refcounted.bytes = (uint8_t *)(s + 1);
    s->slice.data.refcounted.length = length;
    memcpy(s->slice.data.refcounted.bytes, buf, length);
    /* add a null terminator for cheap c string conversion when desired */
    s->slice.data.refcounted.bytes[length] = 0;
  }
  s->has_base64_and_huffman_encoded = 0;
  s->hash = hash;
  s->size_in_decoder_table = SIZE_IN_DECODER_TABLE_NOT_SET;
  s->bucket_next = shard->strs[idx];
  shard->strs[idx] = s;

  shard->count++;

  if (shard->count > shard->capacity * 2) {
    grow_strtab(shard);
  }

  gpr_mu_unlock(&shard->mu);
}
