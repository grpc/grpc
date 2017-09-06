/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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

/** vtable for the AVL tree
 * The optional user_data is propagated from the top level gpr_avl_XXX API.
 * From the same API call, multiple vtable functions may be called multiple
 * times.
 */
typedef struct gpr_avl_vtable {
  /** destroy a key */
  void (*destroy_key)(void *key, void *user_data);
  /** copy a key, returning new value */
  void *(*copy_key)(void *key, void *user_data);
  /** compare key1, key2; return <0 if key1 < key2,
      >0 if key1 > key2, 0 if key1 == key2 */
  long (*compare_keys)(void *key1, void *key2, void *user_data);
  /** destroy a value */
  void (*destroy_value)(void *value, void *user_data);
  /** copy a value */
  void *(*copy_value)(void *value, void *user_data);
} gpr_avl_vtable;

/** "pointer" to an AVL tree - this is a reference
    counted object - use gpr_avl_ref to add a reference,
    gpr_avl_unref when done with a reference */
typedef struct gpr_avl {
  const gpr_avl_vtable *vtable;
  gpr_avl_node *root;
} gpr_avl;

/** Create an immutable AVL tree. */
GPRAPI gpr_avl gpr_avl_create(const gpr_avl_vtable *vtable);
/** Add a reference to an existing tree - returns
    the tree as a convenience. The optional user_data will be passed to vtable
    functions. */
GPRAPI gpr_avl gpr_avl_ref(gpr_avl avl, void *user_data);
/** Remove a reference to a tree - destroying it if there
    are no references left. The optional user_data will be passed to vtable
    functions. */
GPRAPI void gpr_avl_unref(gpr_avl avl, void *user_data);
/** Return a new tree with (key, value) added to avl.
    implicitly unrefs avl to allow easy chaining.
    if key exists in avl, the new tree's key entry updated
    (i.e. a duplicate is not created). The optional user_data will be passed to
    vtable functions. */
GPRAPI gpr_avl gpr_avl_add(gpr_avl avl, void *key, void *value,
                           void *user_data);
/** Return a new tree with key deleted
    implicitly unrefs avl to allow easy chaining. The optional user_data will be
    passed to vtable functions. */
GPRAPI gpr_avl gpr_avl_remove(gpr_avl avl, void *key, void *user_data);
/** Lookup key, and return the associated value.
    Does not mutate avl.
    Returns NULL if key is not found. The optional user_data will be passed to
    vtable functions.*/
GPRAPI void *gpr_avl_get(gpr_avl avl, void *key, void *user_data);
/** Return 1 if avl contains key, 0 otherwise; if it has the key, sets *value to
    its value. THe optional user_data will be passed to vtable functions. */
GPRAPI int gpr_avl_maybe_get(gpr_avl avl, void *key, void **value,
                             void *user_data);
/** Return 1 if avl is empty, 0 otherwise */
GPRAPI int gpr_avl_is_empty(gpr_avl avl);

#endif /* GRPC_SUPPORT_AVL_H */
