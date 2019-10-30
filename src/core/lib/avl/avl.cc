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

#include <grpc/support/port_platform.h>

#include "src/core/lib/avl/avl.h"

#include <assert.h>
#include <stdlib.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/useful.h"

grpc_avl grpc_avl_create(const grpc_avl_vtable* vtable) {
  grpc_avl out;
  out.vtable = vtable;
  out.root = nullptr;
  return out;
}

static grpc_avl_node* ref_node(grpc_avl_node* node) {
  if (node) {
    gpr_ref(&node->refs);
  }
  return node;
}

static void unref_node(const grpc_avl_vtable* vtable, grpc_avl_node* node,
                       void* user_data) {
  if (node == nullptr) {
    return;
  }
  if (gpr_unref(&node->refs)) {
    vtable->destroy_key(node->key, user_data);
    vtable->destroy_value(node->value, user_data);
    unref_node(vtable, node->left, user_data);
    unref_node(vtable, node->right, user_data);
    gpr_free(node);
  }
}

static long node_height(grpc_avl_node* node) {
  return node == nullptr ? 0 : node->height;
}

#ifndef NDEBUG
static long calculate_height(grpc_avl_node* node) {
  return node == nullptr ? 0
                         : 1 + GPR_MAX(calculate_height(node->left),
                                       calculate_height(node->right));
}

static grpc_avl_node* assert_invariants(grpc_avl_node* n) {
  if (n == nullptr) return nullptr;
  assert_invariants(n->left);
  assert_invariants(n->right);
  assert(calculate_height(n) == n->height);
  assert(labs(node_height(n->left) - node_height(n->right)) <= 1);
  return n;
}
#else
static grpc_avl_node* assert_invariants(grpc_avl_node* n) { return n; }
#endif

grpc_avl_node* new_node(void* key, void* value, grpc_avl_node* left,
                        grpc_avl_node* right) {
  grpc_avl_node* node = static_cast<grpc_avl_node*>(gpr_malloc(sizeof(*node)));
  gpr_ref_init(&node->refs, 1);
  node->key = key;
  node->value = value;
  node->left = assert_invariants(left);
  node->right = assert_invariants(right);
  node->height = 1 + GPR_MAX(node_height(left), node_height(right));
  return node;
}

static grpc_avl_node* get(const grpc_avl_vtable* vtable, grpc_avl_node* node,
                          void* key, void* user_data) {
  long cmp;

  if (node == nullptr) {
    return nullptr;
  }

  cmp = vtable->compare_keys(node->key, key, user_data);
  if (cmp == 0) {
    return node;
  } else if (cmp > 0) {
    return get(vtable, node->left, key, user_data);
  } else {
    return get(vtable, node->right, key, user_data);
  }
}

void* grpc_avl_get(grpc_avl avl, void* key, void* user_data) {
  grpc_avl_node* node = get(avl.vtable, avl.root, key, user_data);
  return node ? node->value : nullptr;
}

int grpc_avl_maybe_get(grpc_avl avl, void* key, void** value, void* user_data) {
  grpc_avl_node* node = get(avl.vtable, avl.root, key, user_data);
  if (node != nullptr) {
    *value = node->value;
    return 1;
  }
  return 0;
}

static grpc_avl_node* rotate_left(const grpc_avl_vtable* vtable, void* key,
                                  void* value, grpc_avl_node* left,
                                  grpc_avl_node* right, void* user_data) {
  grpc_avl_node* n = new_node(vtable->copy_key(right->key, user_data),
                              vtable->copy_value(right->value, user_data),
                              new_node(key, value, left, ref_node(right->left)),
                              ref_node(right->right));
  unref_node(vtable, right, user_data);
  return n;
}

static grpc_avl_node* rotate_right(const grpc_avl_vtable* vtable, void* key,
                                   void* value, grpc_avl_node* left,
                                   grpc_avl_node* right, void* user_data) {
  grpc_avl_node* n =
      new_node(vtable->copy_key(left->key, user_data),
               vtable->copy_value(left->value, user_data), ref_node(left->left),
               new_node(key, value, ref_node(left->right), right));
  unref_node(vtable, left, user_data);
  return n;
}

static grpc_avl_node* rotate_left_right(const grpc_avl_vtable* vtable,
                                        void* key, void* value,
                                        grpc_avl_node* left,
                                        grpc_avl_node* right, void* user_data) {
  /* rotate_right(..., rotate_left(left), right) */
  grpc_avl_node* n =
      new_node(vtable->copy_key(left->right->key, user_data),
               vtable->copy_value(left->right->value, user_data),
               new_node(vtable->copy_key(left->key, user_data),
                        vtable->copy_value(left->value, user_data),
                        ref_node(left->left), ref_node(left->right->left)),
               new_node(key, value, ref_node(left->right->right), right));
  unref_node(vtable, left, user_data);
  return n;
}

static grpc_avl_node* rotate_right_left(const grpc_avl_vtable* vtable,
                                        void* key, void* value,
                                        grpc_avl_node* left,
                                        grpc_avl_node* right, void* user_data) {
  /* rotate_left(..., left, rotate_right(right)) */
  grpc_avl_node* n =
      new_node(vtable->copy_key(right->left->key, user_data),
               vtable->copy_value(right->left->value, user_data),
               new_node(key, value, left, ref_node(right->left->left)),
               new_node(vtable->copy_key(right->key, user_data),
                        vtable->copy_value(right->value, user_data),
                        ref_node(right->left->right), ref_node(right->right)));
  unref_node(vtable, right, user_data);
  return n;
}

static grpc_avl_node* rebalance(const grpc_avl_vtable* vtable, void* key,
                                void* value, grpc_avl_node* left,
                                grpc_avl_node* right, void* user_data) {
  switch (node_height(left) - node_height(right)) {
    case 2:
      if (node_height(left->left) - node_height(left->right) == -1) {
        return assert_invariants(
            rotate_left_right(vtable, key, value, left, right, user_data));
      } else {
        return assert_invariants(
            rotate_right(vtable, key, value, left, right, user_data));
      }
    case -2:
      if (node_height(right->left) - node_height(right->right) == 1) {
        return assert_invariants(
            rotate_right_left(vtable, key, value, left, right, user_data));
      } else {
        return assert_invariants(
            rotate_left(vtable, key, value, left, right, user_data));
      }
    default:
      return assert_invariants(new_node(key, value, left, right));
  }
}

static grpc_avl_node* add_key(const grpc_avl_vtable* vtable,
                              grpc_avl_node* node, void* key, void* value,
                              void* user_data) {
  long cmp;
  if (node == nullptr) {
    return new_node(key, value, nullptr, nullptr);
  }
  cmp = vtable->compare_keys(node->key, key, user_data);
  if (cmp == 0) {
    return new_node(key, value, ref_node(node->left), ref_node(node->right));
  } else if (cmp > 0) {
    return rebalance(vtable, vtable->copy_key(node->key, user_data),
                     vtable->copy_value(node->value, user_data),
                     add_key(vtable, node->left, key, value, user_data),
                     ref_node(node->right), user_data);
  } else {
    return rebalance(
        vtable, vtable->copy_key(node->key, user_data),
        vtable->copy_value(node->value, user_data), ref_node(node->left),
        add_key(vtable, node->right, key, value, user_data), user_data);
  }
}

grpc_avl grpc_avl_add(grpc_avl avl, void* key, void* value, void* user_data) {
  grpc_avl_node* old_root = avl.root;
  avl.root = add_key(avl.vtable, avl.root, key, value, user_data);
  assert_invariants(avl.root);
  unref_node(avl.vtable, old_root, user_data);
  return avl;
}

static grpc_avl_node* in_order_head(grpc_avl_node* node) {
  while (node->left != nullptr) {
    node = node->left;
  }
  return node;
}

static grpc_avl_node* in_order_tail(grpc_avl_node* node) {
  while (node->right != nullptr) {
    node = node->right;
  }
  return node;
}

static grpc_avl_node* remove_key(const grpc_avl_vtable* vtable,
                                 grpc_avl_node* node, void* key,
                                 void* user_data) {
  long cmp;
  if (node == nullptr) {
    return nullptr;
  }
  cmp = vtable->compare_keys(node->key, key, user_data);
  if (cmp == 0) {
    if (node->left == nullptr) {
      return ref_node(node->right);
    } else if (node->right == nullptr) {
      return ref_node(node->left);
    } else if (node->left->height < node->right->height) {
      grpc_avl_node* h = in_order_head(node->right);
      return rebalance(
          vtable, vtable->copy_key(h->key, user_data),
          vtable->copy_value(h->value, user_data), ref_node(node->left),
          remove_key(vtable, node->right, h->key, user_data), user_data);
    } else {
      grpc_avl_node* h = in_order_tail(node->left);
      return rebalance(vtable, vtable->copy_key(h->key, user_data),
                       vtable->copy_value(h->value, user_data),
                       remove_key(vtable, node->left, h->key, user_data),
                       ref_node(node->right), user_data);
    }
  } else if (cmp > 0) {
    return rebalance(vtable, vtable->copy_key(node->key, user_data),
                     vtable->copy_value(node->value, user_data),
                     remove_key(vtable, node->left, key, user_data),
                     ref_node(node->right), user_data);
  } else {
    return rebalance(
        vtable, vtable->copy_key(node->key, user_data),
        vtable->copy_value(node->value, user_data), ref_node(node->left),
        remove_key(vtable, node->right, key, user_data), user_data);
  }
}

grpc_avl grpc_avl_remove(grpc_avl avl, void* key, void* user_data) {
  grpc_avl_node* old_root = avl.root;
  avl.root = remove_key(avl.vtable, avl.root, key, user_data);
  assert_invariants(avl.root);
  unref_node(avl.vtable, old_root, user_data);
  return avl;
}

grpc_avl grpc_avl_ref(grpc_avl avl, void* /*user_data*/) {
  ref_node(avl.root);
  return avl;
}

void grpc_avl_unref(grpc_avl avl, void* user_data) {
  unref_node(avl.vtable, avl.root, user_data);
}

int grpc_avl_is_empty(grpc_avl avl) { return avl.root == nullptr; }
