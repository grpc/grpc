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

#ifndef GRPC_INTERNAL_CORE_TRANSPORT_METADATA_H
#define GRPC_INTERNAL_CORE_TRANSPORT_METADATA_H

#include <grpc/support/slice.h>
#include <grpc/support/useful.h>

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
   single request. */

/* Forward declarations */
typedef struct grpc_mdctx grpc_mdctx;
typedef struct grpc_mdstr grpc_mdstr;
typedef struct grpc_mdelem grpc_mdelem;

/* if changing this, make identical changes in internal_string in metadata.c */
struct grpc_mdstr {
  const gpr_slice slice;
  const gpr_uint32 hash;
  /* there is a private part to this in metadata.c */
};

/* if changing this, make identical changes in internal_metadata in
   metadata.c */
struct grpc_mdelem {
  grpc_mdstr *const key;
  grpc_mdstr *const value;
  /* there is a private part to this in metadata.c */
};

/* Create/orphan a metadata context */
grpc_mdctx *grpc_mdctx_create(void);
grpc_mdctx *grpc_mdctx_create_with_seed(gpr_uint32 seed);
void grpc_mdctx_ref(grpc_mdctx *mdctx);
void grpc_mdctx_unref(grpc_mdctx *mdctx);

/* Test only accessors to internal state - only for testing this code - do not
   rely on it outside of metadata_test.c */
size_t grpc_mdctx_get_mdtab_capacity_test_only(grpc_mdctx *mdctx);
size_t grpc_mdctx_get_mdtab_count_test_only(grpc_mdctx *mdctx);
size_t grpc_mdctx_get_mdtab_free_test_only(grpc_mdctx *mdctx);

/* Constructors for grpc_mdstr instances; take a variety of data types that
   clients may have handy */
grpc_mdstr *grpc_mdstr_from_string(grpc_mdctx *ctx, const char *str);
/* Unrefs the slice. */
grpc_mdstr *grpc_mdstr_from_slice(grpc_mdctx *ctx, gpr_slice slice);
grpc_mdstr *grpc_mdstr_from_buffer(grpc_mdctx *ctx, const gpr_uint8 *str,
                                   size_t length);

/* Returns a borrowed slice from the mdstr with its contents base64 encoded
   and huffman compressed */
gpr_slice grpc_mdstr_as_base64_encoded_and_huffman_compressed(grpc_mdstr *str);

/* Constructors for grpc_mdelem instances; take a variety of data types that
   clients may have handy */
grpc_mdelem *grpc_mdelem_from_metadata_strings(grpc_mdctx *ctx, grpc_mdstr *key,
                                               grpc_mdstr *value);
grpc_mdelem *grpc_mdelem_from_strings(grpc_mdctx *ctx, const char *key,
                                      const char *value);
/* Unrefs the slices. */
grpc_mdelem *grpc_mdelem_from_slices(grpc_mdctx *ctx, gpr_slice key,
                                     gpr_slice value);
grpc_mdelem *grpc_mdelem_from_string_and_buffer(grpc_mdctx *ctx,
                                                const char *key,
                                                const gpr_uint8 *value,
                                                size_t value_length);

/* Mutator and accessor for grpc_mdelem user data. The destructor function
   is used as a type tag and is checked during user_data fetch. */
void *grpc_mdelem_get_user_data(grpc_mdelem *md,
                                void (*if_destroy_func)(void *));
void grpc_mdelem_set_user_data(grpc_mdelem *md, void (*destroy_func)(void *),
                               void *user_data);

/* Reference counting */
#ifdef GRPC_METADATA_REFCOUNT_DEBUG
#define GRPC_MDSTR_REF(s) grpc_mdstr_ref((s), __FILE__, __LINE__)
#define GRPC_MDSTR_UNREF(s) grpc_mdstr_unref((s), __FILE__, __LINE__)
#define GRPC_MDELEM_REF(s) grpc_mdelem_ref((s), __FILE__, __LINE__)
#define GRPC_MDELEM_UNREF(s) grpc_mdelem_unref((s), __FILE__, __LINE__)
grpc_mdstr *grpc_mdstr_ref(grpc_mdstr *s, const char *file, int line);
void grpc_mdstr_unref(grpc_mdstr *s, const char *file, int line);
grpc_mdelem *grpc_mdelem_ref(grpc_mdelem *md, const char *file, int line);
void grpc_mdelem_unref(grpc_mdelem *md, const char *file, int line);
#else
#define GRPC_MDSTR_REF(s) grpc_mdstr_ref((s))
#define GRPC_MDSTR_UNREF(s) grpc_mdstr_unref((s))
#define GRPC_MDELEM_REF(s) grpc_mdelem_ref((s))
#define GRPC_MDELEM_UNREF(s) grpc_mdelem_unref((s))
grpc_mdstr *grpc_mdstr_ref(grpc_mdstr *s);
void grpc_mdstr_unref(grpc_mdstr *s);
grpc_mdelem *grpc_mdelem_ref(grpc_mdelem *md);
void grpc_mdelem_unref(grpc_mdelem *md);
#endif

/* Recover a char* from a grpc_mdstr. The returned string is null terminated.
   Does not promise that the returned string has no embedded nulls however. */
const char *grpc_mdstr_as_c_string(grpc_mdstr *s);

int grpc_mdstr_is_legal_header(grpc_mdstr *s);
int grpc_mdstr_is_legal_nonbin_header(grpc_mdstr *s);
int grpc_mdstr_is_bin_suffixed(grpc_mdstr *s);

#define GRPC_MDSTR_KV_HASH(k_hash, v_hash) (GPR_ROTL((k_hash), 2) ^ (v_hash))

#endif /* GRPC_INTERNAL_CORE_TRANSPORT_METADATA_H */
