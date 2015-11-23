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

typedef struct gpr_avl_node {
  gpr_refcount refs;
  void *key;
  void *value;
  struct gpr_avl_node *left;
  struct gpr_avl_node *right;
  long height;
} gpr_avl_node;

typedef struct gpr_avl_vtable {
  void (*destroy_key)(void *key);
  void *(*copy_key)(void *key);
  long (*compare_keys)(void *key1, void *key2);
  void (*destroy_value)(void *value);
  void *(*copy_value)(void *value);
} gpr_avl_vtable;

typedef struct gpr_avl {
  const gpr_avl_vtable *vtable;
  gpr_avl_node *root;
} gpr_avl;

gpr_avl gpr_avl_create(const gpr_avl_vtable *vtable);
void gpr_avl_destroy(gpr_avl avl);
gpr_avl gpr_avl_add(gpr_avl avl, void *key, void *value);
gpr_avl gpr_avl_remove(gpr_avl avl, void *key);
void *gpr_avl_get(gpr_avl avl, void *key);

#endif
