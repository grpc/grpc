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

#ifndef GRPC_CORE_LIB_TRANSPORT_METADATA_H
#define GRPC_CORE_LIB_TRANSPORT_METADATA_H

#include <grpc/slice.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/exec_ctx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* This file provides a mechanism for tracking metadata through the grpc stack.
   It's not intended for consumption outside of the library.

   Metadata is tracked in the context of a grpc_mdctx. For the time being there
   is one of these per-channel, avoiding cross channel interference with memory
   use and lock contention.

   The context tracks unique strings (grpc_mdstr) and pairs of strings
   (grpc_mdelem). Any of these objects can be checked for equality by comparing
   their pointers. These objects are reference counted.

   grpc_mdelem can additionally store a (non-NULL) user data pointer. This
   pointer is intended to be used to cache semantic meaning of a metadata
   element. For example, an OAuth token may cache the credentials it represents
   and the time at which it expires in the mdelem user data.

   Combining this metadata cache and the hpack compression table allows us to
   simply lookup complete preparsed objects quickly, incurring a few atomic
   ops per metadata element on the fast path.

   grpc_mdelem instances MAY live longer than their refcount implies, and are
   garbage collected periodically, meaning cached data can easily outlive a
   single request.

   STATIC METADATA: in static_metadata.h we declare a set of static metadata.
   These mdelems and mdstrs are available via pre-declared code generated macros
   and are available to code anywhere between grpc_init() and grpc_shutdown().
   They are not refcounted, but can be passed to _ref and _unref functions
   declared here - in which case those functions are effectively no-ops. */

/* Forward declarations */
typedef struct grpc_mdstr grpc_mdstr;
typedef struct grpc_mdelem grpc_mdelem;

/* if changing this, make identical changes in internal_string in metadata.c */
struct grpc_mdstr {
  const grpc_slice slice;
  const uint32_t hash;
  /* there is a private part to this in metadata.c */
};

/* if changing this, make identical changes in internal_metadata in
   metadata.c */
struct grpc_mdelem {
  grpc_mdstr *const key;
  grpc_mdstr *const value;
  /* there is a private part to this in metadata.c */
};

void grpc_test_only_set_metadata_hash_seed(uint32_t seed);

/* Constructors for grpc_mdstr instances; take a variety of data types that
   clients may have handy */
grpc_mdstr *grpc_mdstr_from_string(const char *str);
/* Unrefs the slice. */
grpc_mdstr *grpc_mdstr_from_slice(grpc_exec_ctx *exec_ctx, grpc_slice slice);
grpc_mdstr *grpc_mdstr_from_buffer(const uint8_t *str, size_t length);

/* Returns a borrowed slice from the mdstr with its contents base64 encoded
   and huffman compressed */
grpc_slice grpc_mdstr_as_base64_encoded_and_huffman_compressed(grpc_mdstr *str);

/* Constructors for grpc_mdelem instances; take a variety of data types that
   clients may have handy */
grpc_mdelem *grpc_mdelem_from_metadata_strings(grpc_exec_ctx *exec_ctx,
                                               grpc_mdstr *key,
                                               grpc_mdstr *value);
grpc_mdelem *grpc_mdelem_from_strings(grpc_exec_ctx *exec_ctx, const char *key,
                                      const char *value);
/* Unrefs the slices. */
grpc_mdelem *grpc_mdelem_from_slices(grpc_exec_ctx *exec_ctx, grpc_slice key,
                                     grpc_slice value);
grpc_mdelem *grpc_mdelem_from_string_and_buffer(grpc_exec_ctx *exec_ctx,
                                                const char *key,
                                                const uint8_t *value,
                                                size_t value_length);

size_t grpc_mdelem_get_size_in_hpack_table(grpc_mdelem *elem);

/* Mutator and accessor for grpc_mdelem user data. The destructor function
   is used as a type tag and is checked during user_data fetch. */
void *grpc_mdelem_get_user_data(grpc_mdelem *md,
                                void (*if_destroy_func)(void *));
void *grpc_mdelem_set_user_data(grpc_mdelem *md, void (*destroy_func)(void *),
                                void *user_data);

/* Reference counting */
//#define GRPC_METADATA_REFCOUNT_DEBUG
#ifdef GRPC_METADATA_REFCOUNT_DEBUG
#define GRPC_MDSTR_REF(s) grpc_mdstr_ref((s), __FILE__, __LINE__)
#define GRPC_MDSTR_UNREF(exec_ctx, s) \
  grpc_mdstr_unref((exec_ctx), (s), __FILE__, __LINE__)
#define GRPC_MDELEM_REF(s) grpc_mdelem_ref((s), __FILE__, __LINE__)
#define GRPC_MDELEM_UNREF(exec_ctx, s) \
  grpc_mdelem_unref((exec_ctx), (s), __FILE__, __LINE__)
grpc_mdstr *grpc_mdstr_ref(grpc_mdstr *s, const char *file, int line);
void grpc_mdstr_unref(grpc_exec_ctx *exec_ctx, grpc_mdstr *s, const char *file,
                      int line);
grpc_mdelem *grpc_mdelem_ref(grpc_mdelem *md, const char *file, int line);
void grpc_mdelem_unref(grpc_exec_ctx *exec_ctx, grpc_mdelem *md,
                       const char *file, int line);
#else
#define GRPC_MDSTR_REF(s) grpc_mdstr_ref((s))
#define GRPC_MDSTR_UNREF(exec_ctx, s) grpc_mdstr_unref((exec_ctx), (s))
#define GRPC_MDELEM_REF(s) grpc_mdelem_ref((s))
#define GRPC_MDELEM_UNREF(exec_ctx, s) grpc_mdelem_unref((exec_ctx), (s))
grpc_mdstr *grpc_mdstr_ref(grpc_mdstr *s);
void grpc_mdstr_unref(grpc_exec_ctx *exec_ctx, grpc_mdstr *s);
grpc_mdelem *grpc_mdelem_ref(grpc_mdelem *md);
void grpc_mdelem_unref(grpc_exec_ctx *exec_ctx, grpc_mdelem *md);
#endif

/* Recover a char* from a grpc_mdstr. The returned string is null terminated.
   Does not promise that the returned string has no embedded nulls however. */
const char *grpc_mdstr_as_c_string(const grpc_mdstr *s);

#define GRPC_MDSTR_LENGTH(s) (GRPC_SLICE_LENGTH(s->slice))

/* We add 32 bytes of padding as per RFC-7540 section 6.5.2. */
#define GRPC_MDELEM_LENGTH(e) \
  (GRPC_MDSTR_LENGTH((e)->key) + GRPC_MDSTR_LENGTH((e)->value) + 32)

int grpc_mdstr_is_legal_header(grpc_mdstr *s);
int grpc_mdstr_is_legal_nonbin_header(grpc_mdstr *s);
int grpc_mdstr_is_bin_suffixed(grpc_mdstr *s);

#define GRPC_MDSTR_KV_HASH(k_hash, v_hash) (GPR_ROTL((k_hash), 2) ^ (v_hash))

void grpc_mdctx_global_init(void);
void grpc_mdctx_global_shutdown(grpc_exec_ctx *exec_ctx);

/* Implementation provided by chttp2_transport */
extern grpc_slice (*grpc_chttp2_base64_encode_and_huffman_compress)(
    grpc_slice input);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_TRANSPORT_METADATA_H */
