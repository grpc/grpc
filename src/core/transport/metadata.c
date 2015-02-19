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

#include <stddef.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "src/core/support/murmur_hash.h"
#include "src/core/transport/chttp2/bin_encoder.h"
#include <grpc/support/time.h>

#define INITIAL_STRTAB_CAPACITY 4
#define INITIAL_MDTAB_CAPACITY 4

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

  /* private only data */
  void *user_data;
  void (*destroy_user_data)(void *user_data);

  gpr_uint32 refs;
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

static void internal_string_ref(internal_string *s);
static void internal_string_unref(internal_string *s);
static void discard_metadata(grpc_mdctx *ctx);
static void gc_mdtab(grpc_mdctx *ctx);
static void metadata_context_destroy(grpc_mdctx *ctx);

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
    /* gc_mdtab(ctx); */
    if (ctx->mdtab_count && ctx->mdtab_count == ctx->mdtab_free) {
      discard_metadata(ctx);
    }
    if (ctx->strtab_count == 0) {
      gpr_mu_unlock(&ctx->mu);
      metadata_context_destroy(ctx);
      return;
    }
  }
  gpr_mu_unlock(&ctx->mu);
}

static void ref_md(internal_metadata *md) {
  if (0 == md->refs++) {
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
  return grpc_mdctx_create_with_seed(gpr_now().tv_nsec);
}

static void discard_metadata(grpc_mdctx *ctx) {
  size_t i;
  internal_metadata *next, *cur;

  for (i = 0; i < ctx->mdtab_capacity; i++) {
    cur = ctx->mdtab[i];
    while (cur) {
      GPR_ASSERT(cur->refs == 0);
      next = cur->bucket_next;
      internal_string_unref(cur->key);
      internal_string_unref(cur->value);
      if (cur->user_data) {
        cur->destroy_user_data(cur->user_data);
      }
      gpr_free(cur);
      cur = next;
      ctx->mdtab_free--;
      ctx->mdtab_count--;
    }
    ctx->mdtab[i] = NULL;
  }
}

static void metadata_context_destroy(grpc_mdctx *ctx) {
  gpr_mu_lock(&ctx->mu);
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

static void internal_string_ref(internal_string *s) { ++s->refs; }

static void internal_string_unref(internal_string *s) {
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
  internal_string_ref(is);
  unlock(ctx);
}

static void slice_unref(void *p) {
  internal_string *is =
      (internal_string *)((char *)p - offsetof(internal_string, refcount));
  grpc_mdctx *ctx = is->context;
  lock(ctx);
  internal_string_unref(is);
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
      internal_string_ref(s);
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
    s->slice.data.inlined.length = length;
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
      next = md->bucket_next;
      if (md->refs == 0) {
        internal_string_unref(md->key);
        internal_string_unref(md->value);
        if (md->user_data) {
          md->destroy_user_data(md->user_data);
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
      ref_md(md);
      internal_string_unref(key);
      internal_string_unref(value);
      unlock(ctx);
      return (grpc_mdelem *)md;
    }
  }

  /* not found: create a new pair */
  md = gpr_malloc(sizeof(internal_metadata));
  md->refs = 1;
  md->context = ctx;
  md->key = key;
  md->value = value;
  md->user_data = NULL;
  md->destroy_user_data = NULL;
  md->bucket_next = ctx->mdtab[hash % ctx->mdtab_capacity];
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

grpc_mdelem *grpc_mdelem_ref(grpc_mdelem *gmd) {
  internal_metadata *md = (internal_metadata *)gmd;
  grpc_mdctx *ctx = md->context;
  lock(ctx);
  ref_md(md);
  unlock(ctx);
  return gmd;
}

void grpc_mdelem_unref(grpc_mdelem *gmd) {
  internal_metadata *md = (internal_metadata *)gmd;
  grpc_mdctx *ctx = md->context;
  lock(ctx);
  GPR_ASSERT(md->refs);
  if (0 == --md->refs) {
    ctx->mdtab_free++;
  }
  unlock(ctx);
}

const char *grpc_mdstr_as_c_string(grpc_mdstr *s) {
  return (const char *)GPR_SLICE_START_PTR(s->slice);
}

grpc_mdstr *grpc_mdstr_ref(grpc_mdstr *gs) {
  internal_string *s = (internal_string *)gs;
  grpc_mdctx *ctx = s->context;
  lock(ctx);
  internal_string_ref(s);
  unlock(ctx);
  return gs;
}

void grpc_mdstr_unref(grpc_mdstr *gs) {
  internal_string *s = (internal_string *)gs;
  grpc_mdctx *ctx = s->context;
  lock(ctx);
  internal_string_unref(s);
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

void *grpc_mdelem_get_user_data(grpc_mdelem *md,
                                void (*if_destroy_func)(void *)) {
  internal_metadata *im = (internal_metadata *)md;
  return im->destroy_user_data == if_destroy_func ? im->user_data : NULL;
}

void grpc_mdelem_set_user_data(grpc_mdelem *md, void (*destroy_func)(void *),
                               void *user_data) {
  internal_metadata *im = (internal_metadata *)md;
  GPR_ASSERT((user_data == NULL) == (destroy_func == NULL));
  if (im->destroy_user_data) {
    im->destroy_user_data(im->user_data);
  }
  im->destroy_user_data = destroy_func;
  im->user_data = user_data;
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
