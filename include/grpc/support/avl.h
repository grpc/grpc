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
