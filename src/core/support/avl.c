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

static long calculate_height(gpr_avl_node *node) {
  return node == NULL ? 0 : 1 + GPR_MAX(calculate_height(node->left),
                                        calculate_height(node->right));
}

static void assert_invariants(gpr_avl_node *n) {
  if (n == NULL) return;
  assert_invariants(n->left);
  assert_invariants(n->right);
  assert(calculate_height(n) == n->height);
  assert(labs(node_height(n->left) - node_height(n->right)) <= 1);
}

gpr_avl_node *new_node(void *key, void *value, gpr_avl_node *left,
                       gpr_avl_node *right) {
  gpr_avl_node *node = gpr_malloc(sizeof(*node));
  gpr_ref_init(&node->refs, 1);
  node->key = key;
  node->value = value;
  node->left = left;
  node->right = right;
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
  /* TODO(ctiller): elide first allocation */
  gpr_avl_node *leftp = new_node(
      vtable->copy_key(left->right->key),
      vtable->copy_value(left->right->value),
      new_node(vtable->copy_key(left->key), vtable->copy_value(left->value),
               ref_node(left->left), ref_node(left->right->left)),
      ref_node(left->right->right));
  gpr_avl_node *n =
      new_node(vtable->copy_key(leftp->key), vtable->copy_value(leftp->value),
               ref_node(leftp->left),
               new_node(key, value, ref_node(leftp->right), right));
  unref_node(vtable, left);
  unref_node(vtable, leftp);
  return n;
}

static gpr_avl_node *rotate_right_left(const gpr_avl_vtable *vtable, void *key,
                                       void *value, gpr_avl_node *left,
                                       gpr_avl_node *right) {
  /* rotate_left(..., left, rotate_right(right)) */
  /* TODO(ctiller): elide first allocation */
  gpr_avl_node *rightp = new_node(
      vtable->copy_key(right->left->key),
      vtable->copy_value(right->left->value), ref_node(right->left->left),
      new_node(vtable->copy_key(right->key), vtable->copy_key(right->value),
               ref_node(right->left->right), ref_node(right->right)));
  gpr_avl_node *n =
      new_node(vtable->copy_key(rightp->key), vtable->copy_value(rightp->value),
               new_node(key, value, left, ref_node(rightp->left)),
               ref_node(rightp->right));
  unref_node(vtable, right);
  unref_node(vtable, rightp);
  return n;
}

static gpr_avl_node *add(const gpr_avl_vtable *vtable, gpr_avl_node *node,
                         void *key, void *value) {
  long cmp;
  gpr_avl_node *l;
  gpr_avl_node *r;
  if (node == NULL) {
    return new_node(key, value, NULL, NULL);
  }
  cmp = vtable->compare_keys(node->key, key);
  if (cmp == 0) {
    return new_node(key, value, NULL, NULL);
  }
  l = node->left;
  r = node->right;
  if (cmp > 0) {
    l = add(vtable, l, key, value);
    ref_node(r);
  } else {
    r = add(vtable, r, key, value);
    ref_node(l);
  }

  key = vtable->copy_key(node->key);
  value = vtable->copy_value(node->value);

  switch (node_height(l) - node_height(r)) {
    case 2:
      if (node_height(l->left) - node_height(l->right) == 1) {
        return rotate_right(vtable, key, value, l, r);
      } else {
        return rotate_left_right(vtable, key, value, l, r);
      }
    case -2:
      if (node_height(r->left) - node_height(r->right) == 1) {
        return rotate_right_left(vtable, key, value, l, r);
      } else {
        return rotate_left(vtable, key, value, l, r);
      }
    default:
      return new_node(key, value, l, r);
  }
}

gpr_avl gpr_avl_add(gpr_avl avl, void *key, void *value) {
  gpr_avl_node *old_root = avl.root;
  avl.root = add(avl.vtable, avl.root, key, value);
  assert_invariants(avl.root);
  unref_node(avl.vtable, old_root);
  return avl;
}

void gpr_avl_destroy(gpr_avl avl) { unref_node(avl.vtable, avl.root); }
