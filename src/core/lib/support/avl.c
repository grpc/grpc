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

#include <grpc/support/avl.h>

#include <assert.h>
#include <stdlib.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

gpr_avl gpr_avl_create(const gpr_avl_vtable *vtable) {
  gpr_avl out;
  out.vtable = vtable;
  out.root = NULL;
  return out;
}

static gpr_avl_node *ref_node(gpr_avl_node *node) {
  if (node) {
    gpr_ref(&node->refs);
  }
  return node;
}

static void unref_node(const gpr_avl_vtable *vtable, gpr_avl_node *node) {
  if (node == NULL) {
    return;
  }
  if (gpr_unref(&node->refs)) {
    vtable->destroy_key(node->key);
    vtable->destroy_value(node->value);
    unref_node(vtable, node->left);
    unref_node(vtable, node->right);
    gpr_free(node);
  }
}

static long node_height(gpr_avl_node *node) {
  return node == NULL ? 0 : node->height;
}

#ifndef NDEBUG
static long calculate_height(gpr_avl_node *node) {
  return node == NULL ? 0 : 1 + GPR_MAX(calculate_height(node->left),
                                        calculate_height(node->right));
}

static gpr_avl_node *assert_invariants(gpr_avl_node *n) {
  if (n == NULL) return NULL;
  assert_invariants(n->left);
  assert_invariants(n->right);
  assert(calculate_height(n) == n->height);
  assert(labs(node_height(n->left) - node_height(n->right)) <= 1);
  return n;
}
#else
static gpr_avl_node *assert_invariants(gpr_avl_node *n) { return n; }
#endif

gpr_avl_node *new_node(void *key, void *value, gpr_avl_node *left,
                       gpr_avl_node *right) {
  gpr_avl_node *node = gpr_malloc(sizeof(*node));
  gpr_ref_init(&node->refs, 1);
  node->key = key;
  node->value = value;
  node->left = assert_invariants(left);
  node->right = assert_invariants(right);
  node->height = 1 + GPR_MAX(node_height(left), node_height(right));
  return node;
}

static gpr_avl_node *get(const gpr_avl_vtable *vtable, gpr_avl_node *node,
                         void *key) {
  long cmp;

  if (node == NULL) {
    return NULL;
  }

  cmp = vtable->compare_keys(node->key, key);
  if (cmp == 0) {
    return node;
  } else if (cmp > 0) {
    return get(vtable, node->left, key);
  } else {
    return get(vtable, node->right, key);
  }
}

void *gpr_avl_get(gpr_avl avl, void *key) {
  gpr_avl_node *node = get(avl.vtable, avl.root, key);
  return node ? node->value : NULL;
}

int gpr_avl_maybe_get(gpr_avl avl, void *key, void **value) {
  gpr_avl_node *node = get(avl.vtable, avl.root, key);
  if (node != NULL) {
    *value = node->value;
    return 1;
  }
  return 0;
}

static gpr_avl_node *rotate_left(const gpr_avl_vtable *vtable, void *key,
                                 void *value, gpr_avl_node *left,
                                 gpr_avl_node *right) {
  gpr_avl_node *n =
      new_node(vtable->copy_key(right->key), vtable->copy_value(right->value),
               new_node(key, value, left, ref_node(right->left)),
               ref_node(right->right));
  unref_node(vtable, right);
  return n;
}

static gpr_avl_node *rotate_right(const gpr_avl_vtable *vtable, void *key,
                                  void *value, gpr_avl_node *left,
                                  gpr_avl_node *right) {
  gpr_avl_node *n = new_node(
      vtable->copy_key(left->key), vtable->copy_value(left->value),
      ref_node(left->left), new_node(key, value, ref_node(left->right), right));
  unref_node(vtable, left);
  return n;
}

static gpr_avl_node *rotate_left_right(const gpr_avl_vtable *vtable, void *key,
                                       void *value, gpr_avl_node *left,
                                       gpr_avl_node *right) {
  /* rotate_right(..., rotate_left(left), right) */
  gpr_avl_node *n = new_node(
      vtable->copy_key(left->right->key),
      vtable->copy_value(left->right->value),
      new_node(vtable->copy_key(left->key), vtable->copy_value(left->value),
               ref_node(left->left), ref_node(left->right->left)),
      new_node(key, value, ref_node(left->right->right), right));
  unref_node(vtable, left);
  return n;
}

static gpr_avl_node *rotate_right_left(const gpr_avl_vtable *vtable, void *key,
                                       void *value, gpr_avl_node *left,
                                       gpr_avl_node *right) {
  /* rotate_left(..., left, rotate_right(right)) */
  gpr_avl_node *n = new_node(
      vtable->copy_key(right->left->key),
      vtable->copy_value(right->left->value),
      new_node(key, value, left, ref_node(right->left->left)),
      new_node(vtable->copy_key(right->key), vtable->copy_value(right->value),
               ref_node(right->left->right), ref_node(right->right)));
  unref_node(vtable, right);
  return n;
}

static gpr_avl_node *rebalance(const gpr_avl_vtable *vtable, void *key,
                               void *value, gpr_avl_node *left,
                               gpr_avl_node *right) {
  switch (node_height(left) - node_height(right)) {
    case 2:
      if (node_height(left->left) - node_height(left->right) == -1) {
        return assert_invariants(
            rotate_left_right(vtable, key, value, left, right));
      } else {
        return assert_invariants(rotate_right(vtable, key, value, left, right));
      }
    case -2:
      if (node_height(right->left) - node_height(right->right) == 1) {
        return assert_invariants(
            rotate_right_left(vtable, key, value, left, right));
      } else {
        return assert_invariants(rotate_left(vtable, key, value, left, right));
      }
    default:
      return assert_invariants(new_node(key, value, left, right));
  }
}

static gpr_avl_node *add_key(const gpr_avl_vtable *vtable, gpr_avl_node *node,
                             void *key, void *value) {
  long cmp;
  if (node == NULL) {
    return new_node(key, value, NULL, NULL);
  }
  cmp = vtable->compare_keys(node->key, key);
  if (cmp == 0) {
    return new_node(key, value, ref_node(node->left), ref_node(node->right));
  } else if (cmp > 0) {
    return rebalance(
        vtable, vtable->copy_key(node->key), vtable->copy_value(node->value),
        add_key(vtable, node->left, key, value), ref_node(node->right));
  } else {
    return rebalance(vtable, vtable->copy_key(node->key),
                     vtable->copy_value(node->value), ref_node(node->left),
                     add_key(vtable, node->right, key, value));
  }
}

gpr_avl gpr_avl_add(gpr_avl avl, void *key, void *value) {
  gpr_avl_node *old_root = avl.root;
  avl.root = add_key(avl.vtable, avl.root, key, value);
  assert_invariants(avl.root);
  unref_node(avl.vtable, old_root);
  return avl;
}

static gpr_avl_node *in_order_head(gpr_avl_node *node) {
  while (node->left != NULL) {
    node = node->left;
  }
  return node;
}

static gpr_avl_node *in_order_tail(gpr_avl_node *node) {
  while (node->right != NULL) {
    node = node->right;
  }
  return node;
}

static gpr_avl_node *remove_key(const gpr_avl_vtable *vtable,
                                gpr_avl_node *node, void *key) {
  long cmp;
  if (node == NULL) {
    return NULL;
  }
  cmp = vtable->compare_keys(node->key, key);
  if (cmp == 0) {
    if (node->left == NULL) {
      return ref_node(node->right);
    } else if (node->right == NULL) {
      return ref_node(node->left);
    } else if (node->left->height < node->right->height) {
      gpr_avl_node *h = in_order_head(node->right);
      return rebalance(vtable, vtable->copy_key(h->key),
                       vtable->copy_value(h->value), ref_node(node->left),
                       remove_key(vtable, node->right, h->key));
    } else {
      gpr_avl_node *h = in_order_tail(node->left);
      return rebalance(
          vtable, vtable->copy_key(h->key), vtable->copy_value(h->value),
          remove_key(vtable, node->left, h->key), ref_node(node->right));
    }
  } else if (cmp > 0) {
    return rebalance(
        vtable, vtable->copy_key(node->key), vtable->copy_value(node->value),
        remove_key(vtable, node->left, key), ref_node(node->right));
  } else {
    return rebalance(vtable, vtable->copy_key(node->key),
                     vtable->copy_value(node->value), ref_node(node->left),
                     remove_key(vtable, node->right, key));
  }
}

gpr_avl gpr_avl_remove(gpr_avl avl, void *key) {
  gpr_avl_node *old_root = avl.root;
  avl.root = remove_key(avl.vtable, avl.root, key);
  assert_invariants(avl.root);
  unref_node(avl.vtable, old_root);
  return avl;
}

gpr_avl gpr_avl_ref(gpr_avl avl) {
  ref_node(avl.root);
  return avl;
}

void gpr_avl_unref(gpr_avl avl) { unref_node(avl.vtable, avl.root); }

int gpr_avl_is_empty(gpr_avl avl) { return avl.root == NULL; }
