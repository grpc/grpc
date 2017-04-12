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

#include "src/core/lib/slice/slice_internal.h"

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include <string.h>

#include "src/core/lib/iomgr/exec_ctx.h"

char *grpc_slice_to_c_string(grpc_slice slice) {
  char *out = gpr_malloc(GRPC_SLICE_LENGTH(slice) + 1);
  memcpy(out, GRPC_SLICE_START_PTR(slice), GRPC_SLICE_LENGTH(slice));
  out[GRPC_SLICE_LENGTH(slice)] = 0;
  return out;
}

grpc_slice grpc_empty_slice(void) {
  grpc_slice out;
  out.refcount = NULL;
  out.data.inlined.length = 0;
  return out;
}

grpc_slice grpc_slice_ref_internal(grpc_slice slice) {
  if (slice.refcount) {
    slice.refcount->vtable->ref(slice.refcount);
  }
  return slice;
}

void grpc_slice_unref_internal(grpc_exec_ctx *exec_ctx, grpc_slice slice) {
  if (slice.refcount) {
    slice.refcount->vtable->unref(exec_ctx, slice.refcount);
  }
}

/* Public API */
grpc_slice grpc_slice_ref(grpc_slice slice) {
  return grpc_slice_ref_internal(slice);
}

/* Public API */
void grpc_slice_unref(grpc_slice slice) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_slice_unref_internal(&exec_ctx, slice);
  grpc_exec_ctx_finish(&exec_ctx);
}

/* grpc_slice_from_static_string support structure - a refcount that does
   nothing */
static void noop_ref(void *unused) {}
static void noop_unref(grpc_exec_ctx *exec_ctx, void *unused) {}

static const grpc_slice_refcount_vtable noop_refcount_vtable = {
    noop_ref, noop_unref, grpc_slice_default_eq_impl,
    grpc_slice_default_hash_impl};
static grpc_slice_refcount noop_refcount = {&noop_refcount_vtable,
                                            &noop_refcount};

grpc_slice grpc_slice_from_static_buffer(const void *s, size_t len) {
  grpc_slice slice;
  slice.refcount = &noop_refcount;
  slice.data.refcounted.bytes = (uint8_t *)s;
  slice.data.refcounted.length = len;
  return slice;
}

grpc_slice grpc_slice_from_static_string(const char *s) {
  return grpc_slice_from_static_buffer(s, strlen(s));
}

/* grpc_slice_new support structures - we create a refcount object extended
   with the user provided data pointer & destroy function */
typedef struct new_slice_refcount {
  grpc_slice_refcount rc;
  gpr_refcount refs;
  void (*user_destroy)(void *);
  void *user_data;
} new_slice_refcount;

static void new_slice_ref(void *p) {
  new_slice_refcount *r = p;
  gpr_ref(&r->refs);
}

static void new_slice_unref(grpc_exec_ctx *exec_ctx, void *p) {
  new_slice_refcount *r = p;
  if (gpr_unref(&r->refs)) {
    r->user_destroy(r->user_data);
    gpr_free(r);
  }
}

static const grpc_slice_refcount_vtable new_slice_vtable = {
    new_slice_ref, new_slice_unref, grpc_slice_default_eq_impl,
    grpc_slice_default_hash_impl};

grpc_slice grpc_slice_new_with_user_data(void *p, size_t len,
                                         void (*destroy)(void *),
                                         void *user_data) {
  grpc_slice slice;
  new_slice_refcount *rc = gpr_malloc(sizeof(new_slice_refcount));
  gpr_ref_init(&rc->refs, 1);
  rc->rc.vtable = &new_slice_vtable;
  rc->rc.sub_refcount = &rc->rc;
  rc->user_destroy = destroy;
  rc->user_data = user_data;

  slice.refcount = &rc->rc;
  slice.data.refcounted.bytes = p;
  slice.data.refcounted.length = len;
  return slice;
}

grpc_slice grpc_slice_new(void *p, size_t len, void (*destroy)(void *)) {
  /* Pass "p" to *destroy when the slice is no longer needed. */
  return grpc_slice_new_with_user_data(p, len, destroy, p);
}

/* grpc_slice_new_with_len support structures - we create a refcount object
   extended with the user provided data pointer & destroy function */
typedef struct new_with_len_slice_refcount {
  grpc_slice_refcount rc;
  gpr_refcount refs;
  void *user_data;
  size_t user_length;
  void (*user_destroy)(void *, size_t);
} new_with_len_slice_refcount;

static void new_with_len_ref(void *p) {
  new_with_len_slice_refcount *r = p;
  gpr_ref(&r->refs);
}

static void new_with_len_unref(grpc_exec_ctx *exec_ctx, void *p) {
  new_with_len_slice_refcount *r = p;
  if (gpr_unref(&r->refs)) {
    r->user_destroy(r->user_data, r->user_length);
    gpr_free(r);
  }
}

static const grpc_slice_refcount_vtable new_with_len_vtable = {
    new_with_len_ref, new_with_len_unref, grpc_slice_default_eq_impl,
    grpc_slice_default_hash_impl};

grpc_slice grpc_slice_new_with_len(void *p, size_t len,
                                   void (*destroy)(void *, size_t)) {
  grpc_slice slice;
  new_with_len_slice_refcount *rc =
      gpr_malloc(sizeof(new_with_len_slice_refcount));
  gpr_ref_init(&rc->refs, 1);
  rc->rc.vtable = &new_with_len_vtable;
  rc->rc.sub_refcount = &rc->rc;
  rc->user_destroy = destroy;
  rc->user_data = p;
  rc->user_length = len;

  slice.refcount = &rc->rc;
  slice.data.refcounted.bytes = p;
  slice.data.refcounted.length = len;
  return slice;
}

grpc_slice grpc_slice_from_copied_buffer(const char *source, size_t length) {
  if (length == 0) return grpc_empty_slice();
  grpc_slice slice = grpc_slice_malloc(length);
  memcpy(GRPC_SLICE_START_PTR(slice), source, length);
  return slice;
}

grpc_slice grpc_slice_from_copied_string(const char *source) {
  return grpc_slice_from_copied_buffer(source, strlen(source));
}

typedef struct {
  grpc_slice_refcount base;
  gpr_refcount refs;
} malloc_refcount;

static void malloc_ref(void *p) {
  malloc_refcount *r = p;
  gpr_ref(&r->refs);
}

static void malloc_unref(grpc_exec_ctx *exec_ctx, void *p) {
  malloc_refcount *r = p;
  if (gpr_unref(&r->refs)) {
    gpr_free(r);
  }
}

static const grpc_slice_refcount_vtable malloc_vtable = {
    malloc_ref, malloc_unref, grpc_slice_default_eq_impl,
    grpc_slice_default_hash_impl};

grpc_slice grpc_slice_malloc(size_t length) {
  grpc_slice slice;

  if (length > sizeof(slice.data.inlined.bytes)) {
    /* Memory layout used by the slice created here:

       +-----------+----------------------------------------------------------+
       | refcount  | bytes                                                    |
       +-----------+----------------------------------------------------------+

       refcount is a malloc_refcount
       bytes is an array of bytes of the requested length
       Both parts are placed in the same allocation returned from gpr_malloc */
    malloc_refcount *rc = gpr_malloc(sizeof(malloc_refcount) + length);

    /* Initial refcount on rc is 1 - and it's up to the caller to release
       this reference. */
    gpr_ref_init(&rc->refs, 1);

    rc->base.vtable = &malloc_vtable;
    rc->base.sub_refcount = &rc->base;

    /* Build up the slice to be returned. */
    /* The slices refcount points back to the allocated block. */
    slice.refcount = &rc->base;
    /* The data bytes are placed immediately after the refcount struct */
    slice.data.refcounted.bytes = (uint8_t *)(rc + 1);
    /* And the length of the block is set to the requested length */
    slice.data.refcounted.length = length;
  } else {
    /* small slice: just inline the data */
    slice.refcount = NULL;
    slice.data.inlined.length = (uint8_t)length;
  }
  return slice;
}

grpc_slice grpc_slice_sub_no_ref(grpc_slice source, size_t begin, size_t end) {
  grpc_slice subset;

  GPR_ASSERT(end >= begin);

  if (source.refcount) {
    /* Enforce preconditions */
    GPR_ASSERT(source.data.refcounted.length >= end);

    /* Build the result */
    subset.refcount = source.refcount->sub_refcount;
    /* Point into the source array */
    subset.data.refcounted.bytes = source.data.refcounted.bytes + begin;
    subset.data.refcounted.length = end - begin;
  } else {
    /* Enforce preconditions */
    GPR_ASSERT(source.data.inlined.length >= end);
    subset.refcount = NULL;
    subset.data.inlined.length = (uint8_t)(end - begin);
    memcpy(subset.data.inlined.bytes, source.data.inlined.bytes + begin,
           end - begin);
  }
  return subset;
}

grpc_slice grpc_slice_sub(grpc_slice source, size_t begin, size_t end) {
  grpc_slice subset;

  if (end - begin <= sizeof(subset.data.inlined.bytes)) {
    subset.refcount = NULL;
    subset.data.inlined.length = (uint8_t)(end - begin);
    memcpy(subset.data.inlined.bytes, GRPC_SLICE_START_PTR(source) + begin,
           end - begin);
  } else {
    subset = grpc_slice_sub_no_ref(source, begin, end);
    /* Bump the refcount */
    subset.refcount->vtable->ref(subset.refcount);
  }
  return subset;
}

grpc_slice grpc_slice_split_tail(grpc_slice *source, size_t split) {
  grpc_slice tail;

  if (source->refcount == NULL) {
    /* inlined data, copy it out */
    GPR_ASSERT(source->data.inlined.length >= split);
    tail.refcount = NULL;
    tail.data.inlined.length = (uint8_t)(source->data.inlined.length - split);
    memcpy(tail.data.inlined.bytes, source->data.inlined.bytes + split,
           tail.data.inlined.length);
    source->data.inlined.length = (uint8_t)split;
  } else {
    size_t tail_length = source->data.refcounted.length - split;
    GPR_ASSERT(source->data.refcounted.length >= split);
    if (tail_length < sizeof(tail.data.inlined.bytes)) {
      /* Copy out the bytes - it'll be cheaper than refcounting */
      tail.refcount = NULL;
      tail.data.inlined.length = (uint8_t)tail_length;
      memcpy(tail.data.inlined.bytes, source->data.refcounted.bytes + split,
             tail_length);
    } else {
      /* Build the result */
      tail.refcount = source->refcount->sub_refcount;
      /* Bump the refcount */
      tail.refcount->vtable->ref(tail.refcount);
      /* Point into the source array */
      tail.data.refcounted.bytes = source->data.refcounted.bytes + split;
      tail.data.refcounted.length = tail_length;
    }
    source->refcount = source->refcount->sub_refcount;
    source->data.refcounted.length = split;
  }

  return tail;
}

grpc_slice grpc_slice_split_head(grpc_slice *source, size_t split) {
  grpc_slice head;

  if (source->refcount == NULL) {
    GPR_ASSERT(source->data.inlined.length >= split);

    head.refcount = NULL;
    head.data.inlined.length = (uint8_t)split;
    memcpy(head.data.inlined.bytes, source->data.inlined.bytes, split);
    source->data.inlined.length =
        (uint8_t)(source->data.inlined.length - split);
    memmove(source->data.inlined.bytes, source->data.inlined.bytes + split,
            source->data.inlined.length);
  } else if (split < sizeof(head.data.inlined.bytes)) {
    GPR_ASSERT(source->data.refcounted.length >= split);

    head.refcount = NULL;
    head.data.inlined.length = (uint8_t)split;
    memcpy(head.data.inlined.bytes, source->data.refcounted.bytes, split);
    source->refcount = source->refcount->sub_refcount;
    source->data.refcounted.bytes += split;
    source->data.refcounted.length -= split;
  } else {
    GPR_ASSERT(source->data.refcounted.length >= split);

    /* Build the result */
    head.refcount = source->refcount->sub_refcount;
    /* Bump the refcount */
    head.refcount->vtable->ref(head.refcount);
    /* Point into the source array */
    head.data.refcounted.bytes = source->data.refcounted.bytes;
    head.data.refcounted.length = split;
    source->refcount = source->refcount->sub_refcount;
    source->data.refcounted.bytes += split;
    source->data.refcounted.length -= split;
  }

  return head;
}

int grpc_slice_default_eq_impl(grpc_slice a, grpc_slice b) {
  if (GRPC_SLICE_LENGTH(a) != GRPC_SLICE_LENGTH(b)) return false;
  if (GRPC_SLICE_LENGTH(a) == 0) return true;
  return 0 == memcmp(GRPC_SLICE_START_PTR(a), GRPC_SLICE_START_PTR(b),
                     GRPC_SLICE_LENGTH(a));
}

int grpc_slice_eq(grpc_slice a, grpc_slice b) {
  if (a.refcount && b.refcount && a.refcount->vtable == b.refcount->vtable) {
    return a.refcount->vtable->eq(a, b);
  }
  return grpc_slice_default_eq_impl(a, b);
}

int grpc_slice_cmp(grpc_slice a, grpc_slice b) {
  int d = (int)(GRPC_SLICE_LENGTH(a) - GRPC_SLICE_LENGTH(b));
  if (d != 0) return d;
  return memcmp(GRPC_SLICE_START_PTR(a), GRPC_SLICE_START_PTR(b),
                GRPC_SLICE_LENGTH(a));
}

int grpc_slice_str_cmp(grpc_slice a, const char *b) {
  size_t b_length = strlen(b);
  int d = (int)(GRPC_SLICE_LENGTH(a) - b_length);
  if (d != 0) return d;
  return memcmp(GRPC_SLICE_START_PTR(a), b, b_length);
}

int grpc_slice_is_equivalent(grpc_slice a, grpc_slice b) {
  if (a.refcount == NULL || b.refcount == NULL) {
    return grpc_slice_eq(a, b);
  }
  return a.data.refcounted.length == b.data.refcounted.length &&
         a.data.refcounted.bytes == b.data.refcounted.bytes;
}

int grpc_slice_buf_start_eq(grpc_slice a, const void *b, size_t len) {
  if (GRPC_SLICE_LENGTH(a) < len) return 0;
  return 0 == memcmp(GRPC_SLICE_START_PTR(a), b, len);
}

int grpc_slice_rchr(grpc_slice s, char c) {
  const char *b = (const char *)GRPC_SLICE_START_PTR(s);
  int i;
  for (i = (int)GRPC_SLICE_LENGTH(s) - 1; i != -1 && b[i] != c; i--)
    ;
  return i;
}

int grpc_slice_chr(grpc_slice s, char c) {
  const char *b = (const char *)GRPC_SLICE_START_PTR(s);
  const char *p = memchr(b, c, GRPC_SLICE_LENGTH(s));
  return p == NULL ? -1 : (int)(p - b);
}

int grpc_slice_slice(grpc_slice haystack, grpc_slice needle) {
  size_t haystack_len = GRPC_SLICE_LENGTH(haystack);
  const uint8_t *haystack_bytes = GRPC_SLICE_START_PTR(haystack);
  size_t needle_len = GRPC_SLICE_LENGTH(needle);
  const uint8_t *needle_bytes = GRPC_SLICE_START_PTR(needle);

  if (haystack_len == 0 || needle_len == 0) return -1;
  if (haystack_len < needle_len) return -1;
  if (haystack_len == needle_len)
    return grpc_slice_eq(haystack, needle) ? 0 : -1;
  if (needle_len == 1) return grpc_slice_chr(haystack, (char)*needle_bytes);

  const uint8_t *last = haystack_bytes + haystack_len - needle_len;
  for (const uint8_t *cur = haystack_bytes; cur != last; ++cur) {
    if (0 == memcmp(cur, needle_bytes, needle_len)) {
      return (int)(cur - haystack_bytes);
    }
  }
  return -1;
}

grpc_slice grpc_slice_dup(grpc_slice a) {
  grpc_slice copy = grpc_slice_malloc(GRPC_SLICE_LENGTH(a));
  memcpy(GRPC_SLICE_START_PTR(copy), GRPC_SLICE_START_PTR(a),
         GRPC_SLICE_LENGTH(a));
  return copy;
}
