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

#ifndef GRPC_SLICE_H
#define GRPC_SLICE_H

#include <grpc/impl/codegen/slice.h>
#include <grpc/support/sync.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Increment the refcount of s. Requires slice is initialized.
   Returns s. */
GPRAPI grpc_slice grpc_slice_ref(grpc_slice s);

/* Decrement the ref count of s.  If the ref count of s reaches zero, all
   slices sharing the ref count are destroyed, and considered no longer
   initialized.  If s is ultimately derived from a call to grpc_slice_new(start,
   len, dest) where dest!=NULL , then (*dest)(start) is called, else if s is
   ultimately derived from a call to grpc_slice_new_with_len(start, len, dest)
   where dest!=NULL , then (*dest)(start, len).  Requires s initialized.  */
GPRAPI void grpc_slice_unref(grpc_slice s);

/* Create a slice pointing at some data. Calls malloc to allocate a refcount
   for the object, and arranges that destroy will be called with the pointer
   passed in at destruction. */
GPRAPI grpc_slice grpc_slice_new(void *p, size_t len, void (*destroy)(void *));

/* Equivalent to grpc_slice_new, but with a separate pointer that is
   passed to the destroy function.  This function can be useful when
   the data is part of a larger structure that must be destroyed when
   the data is no longer needed. */
GPRAPI grpc_slice grpc_slice_new_with_user_data(void *p, size_t len,
                                                void (*destroy)(void *),
                                                void *user_data);

/* Equivalent to grpc_slice_new, but with a two argument destroy function that
   also takes the slice length. */
GPRAPI grpc_slice grpc_slice_new_with_len(void *p, size_t len,
                                          void (*destroy)(void *, size_t));

/* Equivalent to grpc_slice_new(malloc(len), len, free), but saves one malloc()
   call.
   Aborts if malloc() fails. */
GPRAPI grpc_slice grpc_slice_malloc(size_t length);

/* Intern a slice:

   The return value for two invocations of this function with  the same sequence
   of bytes is a slice which points to the same memory. */
GPRAPI grpc_slice grpc_slice_intern(grpc_slice slice);

/* Create a slice by copying a string.
   Does not preserve null terminators.
   Equivalent to:
     size_t len = strlen(source);
     grpc_slice slice = grpc_slice_malloc(len);
     memcpy(slice->data, source, len); */
GPRAPI grpc_slice grpc_slice_from_copied_string(const char *source);

/* Create a slice by copying a buffer.
   Equivalent to:
     grpc_slice slice = grpc_slice_malloc(len);
     memcpy(slice->data, source, len); */
GPRAPI grpc_slice grpc_slice_from_copied_buffer(const char *source, size_t len);

/* Create a slice pointing to constant memory */
GPRAPI grpc_slice grpc_slice_from_static_string(const char *source);

/* Create a slice pointing to constant memory */
GPRAPI grpc_slice grpc_slice_from_static_buffer(const void *source, size_t len);

/* Return a result slice derived from s, which shares a ref count with s, where
   result.data==s.data+begin, and result.length==end-begin.
   The ref count of s is increased by one.
   Requires s initialized, begin <= end, begin <= s.length, and
   end <= source->length. */
GPRAPI grpc_slice grpc_slice_sub(grpc_slice s, size_t begin, size_t end);

/* The same as grpc_slice_sub, but without altering the ref count */
GPRAPI grpc_slice grpc_slice_sub_no_ref(grpc_slice s, size_t begin, size_t end);

/* Splits s into two: modifies s to be s[0:split], and returns a new slice,
   sharing a refcount with s, that contains s[split:s.length].
   Requires s intialized, split <= s.length */
GPRAPI grpc_slice grpc_slice_split_tail(grpc_slice *s, size_t split);

/* Splits s into two: modifies s to be s[split:s.length], and returns a new
   slice, sharing a refcount with s, that contains s[0:split].
   Requires s intialized, split <= s.length */
GPRAPI grpc_slice grpc_slice_split_head(grpc_slice *s, size_t split);

GPRAPI grpc_slice grpc_empty_slice(void);

GPRAPI uint32_t grpc_slice_default_hash_impl(grpc_slice s);
GPRAPI int grpc_slice_default_eq_impl(grpc_slice a, grpc_slice b);

GPRAPI int grpc_slice_eq(grpc_slice a, grpc_slice b);

/* Returns <0 if a < b, ==0 if a == b, >0 if a > b
   The order is arbitrary, and is not guaranteed to be stable across different
   versions of the API. */
GPRAPI int grpc_slice_cmp(grpc_slice a, grpc_slice b);
GPRAPI int grpc_slice_str_cmp(grpc_slice a, const char *b);
GPRAPI int grpc_slice_buf_cmp(grpc_slice a, const void *b, size_t blen);

/* return non-zero if the first blen bytes of a are equal to b */
GPRAPI int grpc_slice_buf_start_eq(grpc_slice a, const void *b, size_t blen);

/* return the index of the last instance of \a c in \a s, or -1 if not found */
GPRAPI int grpc_slice_rchr(grpc_slice s, char c);
GPRAPI int grpc_slice_chr(grpc_slice s, char c);

/* return the index of the first occurance of \a needle in \a haystack, or -1 if
 * it's not found */
GPRAPI int grpc_slice_slice(grpc_slice haystack, grpc_slice needle);

GPRAPI uint32_t grpc_slice_hash(grpc_slice s);

/* Do two slices point at the same memory, with the same length
   If a or b is inlined, actually compares data */
GPRAPI int grpc_slice_is_equivalent(grpc_slice a, grpc_slice b);

/* Return a slice pointing to newly allocated memory that has the same contents
 * as \a s */
GPRAPI grpc_slice grpc_slice_dup(grpc_slice a);

/* Return a copy of slice as a C string. Offers no protection against embedded
   NULL's. Returned string must be freed with gpr_free. */
GPRAPI char *grpc_slice_to_c_string(grpc_slice s);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_SLICE_H */
