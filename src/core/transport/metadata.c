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

#include "src/core/iomgr/sockaddr.h"
#include "src/core/transport/metadata.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include "src/core/support/murmur_hash.h"
#include "src/core/transport/chttp2/bin_encoder.h"
#include <grpc/support/time.h>

#define INITIAL_STRTAB_CAPACITY 4
#define INITIAL_MDTAB_CAPACITY 4

#ifdef GRPC_METADATA_REFCOUNT_DEBUG
#define DEBUG_ARGS , const char *file, int line
#define FWD_DEBUG_ARGS , file, line
#define INTERNAL_STRING_REF(s) internal_string_ref((s), __FILE__, __LINE__)
#define INTERNAL_STRING_UNREF(s) internal_string_unref((s), __FILE__, __LINE__)
#define REF_MD_LOCKED(s) ref_md_locked((s), __FILE__, __LINE__)
#else
#define DEBUG_ARGS
#define FWD_DEBUG_ARGS
#define INTERNAL_STRING_REF(s) internal_string_ref((s))
#define INTERNAL_STRING_UNREF(s) internal_string_unref((s))
#define REF_MD_LOCKED(s) ref_md_locked((s))
#endif

typedef void (*destroy_user_data_func)(void *user_data);

typedef struct internal_string {
  /* must be byte compatible with grpc_mdstr */
  gpr_slice slice;
  gpr_uint32 hash;

  /* private only data */
  gpr_uint32 refs;
  gpr_uint8 has_base64_and_huffman_encoded;
  gpr_slice_refcount refcount;

  gpr_slice base64_and_huffman;

  grpc_mdctx *context;

  struct internal_string *bucket_next;
} internal_string;

typedef struct internal_metadata {
  /* must be byte compatible with grpc_mdelem */
  internal_string *key;
  internal_string *value;

  gpr_atm refcnt;

  /* private only data */
  gpr_mu mu_user_data;
  gpr_atm destroy_user_data;
  gpr_atm user_data;

  grpc_mdctx *context;
  struct internal_metadata *bucket_next;
} internal_metadata;

struct grpc_mdctx {
  gpr_uint32 hash_seed;
  int refs;

  gpr_mu mu;

  internal_string **strtab;
  size_t strtab_count;
  size_t strtab_capacity;

  internal_metadata **mdtab;
  size_t mdtab_count;
  size_t mdtab_free;
  size_t mdtab_capacity;
};

static void internal_string_ref(internal_string *s DEBUG_ARGS);
static void internal_string_unref(internal_string *s DEBUG_ARGS);
static void discard_metadata(grpc_mdctx *ctx);
static void gc_mdtab(grpc_mdctx *ctx);
static void metadata_context_destroy_locked(grpc_mdctx *ctx);

static void lock(grpc_mdctx *ctx) { gpr_mu_lock(&ctx->mu); }

static void unlock(grpc_mdctx *ctx) {
  /* If the context has been orphaned we'd like to delete it soon. We check
     conditions in unlock as it signals the end of mutations on a context.

     We need to ensure all grpc_mdelem and grpc_mdstr elements have been deleted
     first. This is equivalent to saying that both tables have zero counts,
     which is equivalent to saying that strtab_count is zero (as mdelem's MUST
     reference an mdstr for their key and value slots).

     To encourage that to happen, we start discarding zero reference count
     mdelems on every unlock (instead of the usual 'I'm too loaded' trigger
     case), since otherwise we can be stuck waiting for a garbage collection
     that will never happen. */
  if (ctx->refs == 0) {
/* uncomment if you're having trouble diagnosing an mdelem leak to make
   things clearer (slows down destruction a lot, however) */
#ifdef GRPC_METADATA_REFCOUNT_DEBUG
    gc_mdtab(ctx);
#endif
    if (ctx->mdtab_count && ctx->mdtab_count == ctx->mdtab_free) {
      discard_metadata(ctx);
    }
    if (ctx->strtab_count == 0) {
      metadata_context_destroy_locked(ctx);
      return;
    }
  }
  gpr_mu_unlock(&ctx->mu);
}

static void ref_md_locked(internal_metadata *md DEBUG_ARGS) {
#ifdef GRPC_METADATA_REFCOUNT_DEBUG
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
          "ELM   REF:%p:%d->%d: '%s' = '%s'", md,
          gpr_atm_no_barrier_load(&md->refcnt),
          gpr_atm_no_barrier_load(&md->refcnt) + 1,
          grpc_mdstr_as_c_string((grpc_mdstr *)md->key),
          grpc_mdstr_as_c_string((grpc_mdstr *)md->value));
#endif
  if (0 == gpr_atm_no_barrier_fetch_add(&md->refcnt, 1)) {
    /* This ref is dropped if grpc_mdelem_unref reaches 1,
       but allows us to safely unref without taking the mdctx lock
       until such time */
    gpr_atm_no_barrier_fetch_add(&md->refcnt, 1);
    md->context->mdtab_free--;
  }
}

grpc_mdctx *grpc_mdctx_create_with_seed(gpr_uint32 seed) {
  grpc_mdctx *ctx = gpr_malloc(sizeof(grpc_mdctx));

  ctx->refs = 1;
  ctx->hash_seed = seed;
  gpr_mu_init(&ctx->mu);
  ctx->strtab = gpr_malloc(sizeof(internal_string *) * INITIAL_STRTAB_CAPACITY);
  memset(ctx->strtab, 0, sizeof(grpc_mdstr *) * INITIAL_STRTAB_CAPACITY);
  ctx->strtab_count = 0;
  ctx->strtab_capacity = INITIAL_STRTAB_CAPACITY;
  ctx->mdtab = gpr_malloc(sizeof(internal_metadata *) * INITIAL_MDTAB_CAPACITY);
  memset(ctx->mdtab, 0, sizeof(grpc_mdelem *) * INITIAL_MDTAB_CAPACITY);
  ctx->mdtab_count = 0;
  ctx->mdtab_capacity = INITIAL_MDTAB_CAPACITY;
  ctx->mdtab_free = 0;

  return ctx;
}

grpc_mdctx *grpc_mdctx_create(void) {
  /* This seed is used to prevent remote connections from controlling hash table
   * collisions. It needs to be somewhat unpredictable to a remote connection.
   */
  return grpc_mdctx_create_with_seed(
      (gpr_uint32)gpr_now(GPR_CLOCK_REALTIME).tv_nsec);
}

static void discard_metadata(grpc_mdctx *ctx) {
  size_t i;
  internal_metadata *next, *cur;

  for (i = 0; i < ctx->mdtab_capacity; i++) {
    cur = ctx->mdtab[i];
    while (cur) {
      void *user_data = (void *)gpr_atm_no_barrier_load(&cur->user_data);
      GPR_ASSERT(gpr_atm_acq_load(&cur->refcnt) == 0);
      next = cur->bucket_next;
      INTERNAL_STRING_UNREF(cur->key);
      INTERNAL_STRING_UNREF(cur->value);
      if (user_data != NULL) {
        ((destroy_user_data_func)gpr_atm_no_barrier_load(
            &cur->destroy_user_data))(user_data);
      }
      gpr_mu_destroy(&cur->mu_user_data);
      gpr_free(cur);
      cur = next;
      ctx->mdtab_free--;
      ctx->mdtab_count--;
    }
    ctx->mdtab[i] = NULL;
  }
}

static void metadata_context_destroy_locked(grpc_mdctx *ctx) {
  GPR_ASSERT(ctx->strtab_count == 0);
  GPR_ASSERT(ctx->mdtab_count == 0);
  GPR_ASSERT(ctx->mdtab_free == 0);
  gpr_free(ctx->strtab);
  gpr_free(ctx->mdtab);
  gpr_mu_unlock(&ctx->mu);
  gpr_mu_destroy(&ctx->mu);
  gpr_free(ctx);
}

void grpc_mdctx_ref(grpc_mdctx *ctx) {
  lock(ctx);
  GPR_ASSERT(ctx->refs > 0);
  ctx->refs++;
  unlock(ctx);
}

void grpc_mdctx_unref(grpc_mdctx *ctx) {
  lock(ctx);
  GPR_ASSERT(ctx->refs > 0);
  ctx->refs--;
  unlock(ctx);
}

static void grow_strtab(grpc_mdctx *ctx) {
  size_t capacity = ctx->strtab_capacity * 2;
  size_t i;
  internal_string **strtab = gpr_malloc(sizeof(internal_string *) * capacity);
  internal_string *s, *next;
  memset(strtab, 0, sizeof(internal_string *) * capacity);

  for (i = 0; i < ctx->strtab_capacity; i++) {
    for (s = ctx->strtab[i]; s; s = next) {
      next = s->bucket_next;
      s->bucket_next = strtab[s->hash % capacity];
      strtab[s->hash % capacity] = s;
    }
  }

  gpr_free(ctx->strtab);
  ctx->strtab = strtab;
  ctx->strtab_capacity = capacity;
}

static void internal_destroy_string(internal_string *is) {
  internal_string **prev_next;
  internal_string *cur;
  grpc_mdctx *ctx = is->context;
  if (is->has_base64_and_huffman_encoded) {
    gpr_slice_unref(is->base64_and_huffman);
  }
  for (prev_next = &ctx->strtab[is->hash % ctx->strtab_capacity],
      cur = *prev_next;
       cur != is; prev_next = &cur->bucket_next, cur = cur->bucket_next)
    ;
  *prev_next = cur->bucket_next;
  ctx->strtab_count--;
  gpr_free(is);
}

static void internal_string_ref(internal_string *s DEBUG_ARGS) {
#ifdef GRPC_METADATA_REFCOUNT_DEBUG
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "STR   REF:%p:%d->%d: '%s'", s,
          s->refs, s->refs + 1, grpc_mdstr_as_c_string((grpc_mdstr *)s));
#endif
  ++s->refs;
}

static void internal_string_unref(internal_string *s DEBUG_ARGS) {
#ifdef GRPC_METADATA_REFCOUNT_DEBUG
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "STR UNREF:%p:%d->%d: '%s'", s,
          s->refs, s->refs - 1, grpc_mdstr_as_c_string((grpc_mdstr *)s));
#endif
  GPR_ASSERT(s->refs > 0);
  if (0 == --s->refs) {
    internal_destroy_string(s);
  }
}

static void slice_ref(void *p) {
  internal_string *is =
      (internal_string *)((char *)p - offsetof(internal_string, refcount));
  grpc_mdctx *ctx = is->context;
  lock(ctx);
  INTERNAL_STRING_REF(is);
  unlock(ctx);
}

static void slice_unref(void *p) {
  internal_string *is =
      (internal_string *)((char *)p - offsetof(internal_string, refcount));
  grpc_mdctx *ctx = is->context;
  lock(ctx);
  INTERNAL_STRING_UNREF(is);
  unlock(ctx);
}

grpc_mdstr *grpc_mdstr_from_string(grpc_mdctx *ctx, const char *str) {
  return grpc_mdstr_from_buffer(ctx, (const gpr_uint8 *)str, strlen(str));
}

grpc_mdstr *grpc_mdstr_from_slice(grpc_mdctx *ctx, gpr_slice slice) {
  grpc_mdstr *result = grpc_mdstr_from_buffer(ctx, GPR_SLICE_START_PTR(slice),
                                              GPR_SLICE_LENGTH(slice));
  gpr_slice_unref(slice);
  return result;
}

grpc_mdstr *grpc_mdstr_from_buffer(grpc_mdctx *ctx, const gpr_uint8 *buf,
                                   size_t length) {
  gpr_uint32 hash = gpr_murmur_hash3(buf, length, ctx->hash_seed);
  internal_string *s;

  lock(ctx);

  /* search for an existing string */
  for (s = ctx->strtab[hash % ctx->strtab_capacity]; s; s = s->bucket_next) {
    if (s->hash == hash && GPR_SLICE_LENGTH(s->slice) == length &&
        0 == memcmp(buf, GPR_SLICE_START_PTR(s->slice), length)) {
      INTERNAL_STRING_REF(s);
      unlock(ctx);
      return (grpc_mdstr *)s;
    }
  }

  /* not found: create a new string */
  if (length + 1 < GPR_SLICE_INLINED_SIZE) {
    /* string data goes directly into the slice */
    s = gpr_malloc(sizeof(internal_string));
    s->refs = 1;
    s->slice.refcount = NULL;
    memcpy(s->slice.data.inlined.bytes, buf, length);
    s->slice.data.inlined.bytes[length] = 0;
    s->slice.data.inlined.length = (gpr_uint8)length;
  } else {
    /* string data goes after the internal_string header, and we +1 for null
       terminator */
    s = gpr_malloc(sizeof(internal_string) + length + 1);
    s->refs = 1;
    s->refcount.ref = slice_ref;
    s->refcount.unref = slice_unref;
    s->slice.refcount = &s->refcount;
    s->slice.data.refcounted.bytes = (gpr_uint8 *)(s + 1);
    s->slice.data.refcounted.length = length;
    memcpy(s->slice.data.refcounted.bytes, buf, length);
    /* add a null terminator for cheap c string conversion when desired */
    s->slice.data.refcounted.bytes[length] = 0;
  }
  s->has_base64_and_huffman_encoded = 0;
  s->hash = hash;
  s->context = ctx;
  s->bucket_next = ctx->strtab[hash % ctx->strtab_capacity];
  ctx->strtab[hash % ctx->strtab_capacity] = s;

  ctx->strtab_count++;

  if (ctx->strtab_count > ctx->strtab_capacity * 2) {
    grow_strtab(ctx);
  }

  unlock(ctx);

  return (grpc_mdstr *)s;
}

static void gc_mdtab(grpc_mdctx *ctx) {
  size_t i;
  internal_metadata **prev_next;
  internal_metadata *md, *next;

  for (i = 0; i < ctx->mdtab_capacity; i++) {
    prev_next = &ctx->mdtab[i];
    for (md = ctx->mdtab[i]; md; md = next) {
      void *user_data = (void *)gpr_atm_no_barrier_load(&md->user_data);
      next = md->bucket_next;
      if (gpr_atm_acq_load(&md->refcnt) == 0) {
        INTERNAL_STRING_UNREF(md->key);
        INTERNAL_STRING_UNREF(md->value);
        if (md->user_data) {
          ((destroy_user_data_func)gpr_atm_no_barrier_load(
              &md->destroy_user_data))(user_data);
        }
        gpr_free(md);
        *prev_next = next;
        ctx->mdtab_free--;
        ctx->mdtab_count--;
      } else {
        prev_next = &md->bucket_next;
      }
    }
  }

  GPR_ASSERT(ctx->mdtab_free == 0);
}

static void grow_mdtab(grpc_mdctx *ctx) {
  size_t capacity = ctx->mdtab_capacity * 2;
  size_t i;
  internal_metadata **mdtab =
      gpr_malloc(sizeof(internal_metadata *) * capacity);
  internal_metadata *md, *next;
  gpr_uint32 hash;
  memset(mdtab, 0, sizeof(internal_metadata *) * capacity);

  for (i = 0; i < ctx->mdtab_capacity; i++) {
    for (md = ctx->mdtab[i]; md; md = next) {
      hash = GRPC_MDSTR_KV_HASH(md->key->hash, md->value->hash);
      next = md->bucket_next;
      md->bucket_next = mdtab[hash % capacity];
      mdtab[hash % capacity] = md;
    }
  }

  gpr_free(ctx->mdtab);
  ctx->mdtab = mdtab;
  ctx->mdtab_capacity = capacity;
}

static void rehash_mdtab(grpc_mdctx *ctx) {
  if (ctx->mdtab_free > ctx->mdtab_capacity / 4) {
    gc_mdtab(ctx);
  } else {
    grow_mdtab(ctx);
  }
}

grpc_mdelem *grpc_mdelem_from_metadata_strings(grpc_mdctx *ctx,
                                               grpc_mdstr *mkey,
                                               grpc_mdstr *mvalue) {
  internal_string *key = (internal_string *)mkey;
  internal_string *value = (internal_string *)mvalue;
  gpr_uint32 hash = GRPC_MDSTR_KV_HASH(mkey->hash, mvalue->hash);
  internal_metadata *md;

  GPR_ASSERT(key->context == ctx);
  GPR_ASSERT(value->context == ctx);

  lock(ctx);

  /* search for an existing pair */
  for (md = ctx->mdtab[hash % ctx->mdtab_capacity]; md; md = md->bucket_next) {
    if (md->key == key && md->value == value) {
      REF_MD_LOCKED(md);
      INTERNAL_STRING_UNREF(key);
      INTERNAL_STRING_UNREF(value);
      unlock(ctx);
      return (grpc_mdelem *)md;
    }
  }

  /* not found: create a new pair */
  md = gpr_malloc(sizeof(internal_metadata));
  gpr_atm_rel_store(&md->refcnt, 2);
  md->context = ctx;
  md->key = key;
  md->value = value;
  md->user_data = 0;
  md->destroy_user_data = 0;
  md->bucket_next = ctx->mdtab[hash % ctx->mdtab_capacity];
  gpr_mu_init(&md->mu_user_data);
#ifdef GRPC_METADATA_REFCOUNT_DEBUG
  gpr_log(GPR_DEBUG, "ELM   NEW:%p:%d: '%s' = '%s'", md,
          gpr_atm_no_barrier_load(&md->refcnt),
          grpc_mdstr_as_c_string((grpc_mdstr *)md->key),
          grpc_mdstr_as_c_string((grpc_mdstr *)md->value));
#endif
  ctx->mdtab[hash % ctx->mdtab_capacity] = md;
  ctx->mdtab_count++;

  if (ctx->mdtab_count > ctx->mdtab_capacity * 2) {
    rehash_mdtab(ctx);
  }

  unlock(ctx);

  return (grpc_mdelem *)md;
}

grpc_mdelem *grpc_mdelem_from_strings(grpc_mdctx *ctx, const char *key,
                                      const char *value) {
  return grpc_mdelem_from_metadata_strings(ctx,
                                           grpc_mdstr_from_string(ctx, key),
                                           grpc_mdstr_from_string(ctx, value));
}

grpc_mdelem *grpc_mdelem_from_slices(grpc_mdctx *ctx, gpr_slice key,
                                     gpr_slice value) {
  return grpc_mdelem_from_metadata_strings(ctx, grpc_mdstr_from_slice(ctx, key),
                                           grpc_mdstr_from_slice(ctx, value));
}

grpc_mdelem *grpc_mdelem_from_string_and_buffer(grpc_mdctx *ctx,
                                                const char *key,
                                                const gpr_uint8 *value,
                                                size_t value_length) {
  return grpc_mdelem_from_metadata_strings(
      ctx, grpc_mdstr_from_string(ctx, key),
      grpc_mdstr_from_buffer(ctx, value, value_length));
}

grpc_mdelem *grpc_mdelem_ref(grpc_mdelem *gmd DEBUG_ARGS) {
  internal_metadata *md = (internal_metadata *)gmd;
#ifdef GRPC_METADATA_REFCOUNT_DEBUG
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
          "ELM   REF:%p:%d->%d: '%s' = '%s'", md,
          gpr_atm_no_barrier_load(&md->refcnt),
          gpr_atm_no_barrier_load(&md->refcnt) + 1,
          grpc_mdstr_as_c_string((grpc_mdstr *)md->key),
          grpc_mdstr_as_c_string((grpc_mdstr *)md->value));
#endif
  /* we can assume the ref count is >= 1 as the application is calling
     this function - meaning that no adjustment to mdtab_free is necessary,
     simplifying the logic here to be just an atomic increment */
  /* use C assert to have this removed in opt builds */
  assert(gpr_atm_no_barrier_load(&md->refcnt) >= 1);
  gpr_atm_no_barrier_fetch_add(&md->refcnt, 1);
  return gmd;
}

void grpc_mdelem_unref(grpc_mdelem *gmd DEBUG_ARGS) {
  internal_metadata *md = (internal_metadata *)gmd;
#ifdef GRPC_METADATA_REFCOUNT_DEBUG
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
          "ELM UNREF:%p:%d->%d: '%s' = '%s'", md,
          gpr_atm_no_barrier_load(&md->refcnt),
          gpr_atm_no_barrier_load(&md->refcnt) - 1,
          grpc_mdstr_as_c_string((grpc_mdstr *)md->key),
          grpc_mdstr_as_c_string((grpc_mdstr *)md->value));
#endif
  if (2 == gpr_atm_full_fetch_add(&md->refcnt, -1)) {
    grpc_mdctx *ctx = md->context;
    lock(ctx);
    GPR_ASSERT(1 == gpr_atm_full_fetch_add(&md->refcnt, -1));
    ctx->mdtab_free++;
    unlock(ctx);
  }
}

const char *grpc_mdstr_as_c_string(grpc_mdstr *s) {
  return (const char *)GPR_SLICE_START_PTR(s->slice);
}

grpc_mdstr *grpc_mdstr_ref(grpc_mdstr *gs DEBUG_ARGS) {
  internal_string *s = (internal_string *)gs;
  grpc_mdctx *ctx = s->context;
  lock(ctx);
  internal_string_ref(s FWD_DEBUG_ARGS);
  unlock(ctx);
  return gs;
}

void grpc_mdstr_unref(grpc_mdstr *gs DEBUG_ARGS) {
  internal_string *s = (internal_string *)gs;
  grpc_mdctx *ctx = s->context;
  lock(ctx);
  internal_string_unref(s FWD_DEBUG_ARGS);
  unlock(ctx);
}

size_t grpc_mdctx_get_mdtab_capacity_test_only(grpc_mdctx *ctx) {
  return ctx->mdtab_capacity;
}

size_t grpc_mdctx_get_mdtab_count_test_only(grpc_mdctx *ctx) {
  return ctx->mdtab_count;
}

size_t grpc_mdctx_get_mdtab_free_test_only(grpc_mdctx *ctx) {
  return ctx->mdtab_free;
}

void *grpc_mdelem_get_user_data(grpc_mdelem *md, void (*destroy_func)(void *)) {
  internal_metadata *im = (internal_metadata *)md;
  void *result;
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

gpr_slice grpc_mdstr_as_base64_encoded_and_huffman_compressed(grpc_mdstr *gs) {
  internal_string *s = (internal_string *)gs;
  gpr_slice slice;
  grpc_mdctx *ctx = s->context;
  lock(ctx);
  if (!s->has_base64_and_huffman_encoded) {
    s->base64_and_huffman =
        grpc_chttp2_base64_encode_and_huffman_compress(s->slice);
    s->has_base64_and_huffman_encoded = 1;
  }
  slice = s->base64_and_huffman;
  unlock(ctx);
  return slice;
}

static int conforms_to(grpc_mdstr *s, const gpr_uint8 *legal_bits) {
  const gpr_uint8 *p = GPR_SLICE_START_PTR(s->slice);
  const gpr_uint8 *e = GPR_SLICE_END_PTR(s->slice);
  for (; p != e; p++) {
    int idx = *p;
    int byte = idx / 8;
    int bit = idx % 8;
    if ((legal_bits[byte] & (1 << bit)) == 0) return 0;
  }
  return 1;
}

int grpc_mdstr_is_legal_header(grpc_mdstr *s) {
  static const gpr_uint8 legal_header_bits[256 / 8] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0xff, 0x03, 0x00, 0x00, 0x00,
      0x80, 0xfe, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  return conforms_to(s, legal_header_bits);
}

int grpc_mdstr_is_legal_nonbin_header(grpc_mdstr *s) {
  static const gpr_uint8 legal_header_bits[256 / 8] = {
      0x00, 0x00, 0x00, 0x00, 0xff, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  return conforms_to(s, legal_header_bits);
}

int grpc_mdstr_is_bin_suffixed(grpc_mdstr *s) {
  /* TODO(ctiller): consider caching this */
  return grpc_is_binary_header((const char *)GPR_SLICE_START_PTR(s->slice),
                               GPR_SLICE_LENGTH(s->slice));
}
