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

#ifndef GRPC_SUPPORT_AVL_H
#define GRPC_SUPPORT_AVL_H

#include <grpc/support/sync.h>

/** internal node of an AVL tree */
typedef struct gpr_avl_node {
  gpr_refcount refs;
  void *key;
  void *value;
  struct gpr_avl_node *left;
  struct gpr_avl_node *right;
  long height;
} gpr_avl_node;

typedef struct gpr_avl_vtable {
  /** destroy a key */
  void (*destroy_key)(void *key);
  /** copy a key, returning new value */
  void *(*copy_key)(void *key);
  /** compare key1, key2; return <0 if key1 < key2,
      >0 if key1 > key2, 0 if key1 == key2 */
  long (*compare_keys)(void *key1, void *key2);
  /** destroy a value */
  void (*destroy_value)(void *value);
  /** copy a value */
  void *(*copy_value)(void *value);
} gpr_avl_vtable;

/** "pointer" to an AVL tree - this is a reference
    counted object - use gpr_avl_ref to add a reference,
    gpr_avl_unref when done with a reference */
typedef struct gpr_avl {
  const gpr_avl_vtable *vtable;
  gpr_avl_node *root;
} gpr_avl;

/** create an immutable AVL tree */
GPRAPI gpr_avl gpr_avl_create(const gpr_avl_vtable *vtable);
/** add a reference to an existing tree - returns
    the tree as a convenience */
GPRAPI gpr_avl gpr_avl_ref(gpr_avl avl);
/** remove a reference to a tree - destroying it if there
    are no references left */
GPRAPI void gpr_avl_unref(gpr_avl avl);
/** return a new tree with (key, value) added to avl.
    implicitly unrefs avl to allow easy chaining.
    if key exists in avl, the new tree's key entry updated
    (i.e. a duplicate is not created) */
GPRAPI gpr_avl gpr_avl_add(gpr_avl avl, void *key, void *value);
/** return a new tree with key deleted
    implicitly unrefs avl to allow easy chaining. */
GPRAPI gpr_avl gpr_avl_remove(gpr_avl avl, void *key);
/** lookup key, and return the associated value.
    does not mutate avl.
    returns NULL if key is not found. */
GPRAPI void *gpr_avl_get(gpr_avl avl, void *key);
/** Return 1 if avl contains key, 0 otherwise; if it has the key, sets *value to
    its value*/
GPRAPI int gpr_avl_maybe_get(gpr_avl avl, void *key, void **value);
/** Return 1 if avl is empty, 0 otherwise */
GPRAPI int gpr_avl_is_empty(gpr_avl avl);

#endif /* GRPC_SUPPORT_AVL_H */
