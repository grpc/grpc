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
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "test/core/util/test_config.h"

static int *box(int x) {
  int *b = gpr_malloc(sizeof(*b));
  *b = x;
  return b;
}

static long int_compare(void *int1, void *int2) {
  return (*(int *)int1) - (*(int *)int2);
}
static void *int_copy(void *p) { return box(*(int *)p); }

static const gpr_avl_vtable int_int_vtable = {gpr_free, int_copy, int_compare,
                                              gpr_free, int_copy};

static void check_get(gpr_avl avl, int key, int value) {
  int *k = box(key);
  gpr_log(GPR_DEBUG, "check avl[%d] == %d", key, value);
  GPR_ASSERT(*(int *)gpr_avl_get(avl, k) == value);
  gpr_free(k);
}

static void check_negget(gpr_avl avl, int key) {
  int *k = box(key);
  gpr_log(GPR_DEBUG, "check avl[%d] == nil", key);
  GPR_ASSERT(gpr_avl_get(avl, k) == NULL);
  gpr_free(k);
}

static void test_get(void) {
  gpr_avl avl;
  gpr_log(GPR_DEBUG, "test_get");
  avl = gpr_avl_add(
      gpr_avl_add(gpr_avl_add(gpr_avl_create(&int_int_vtable), box(1), box(11)),
                  box(2), box(22)),
      box(3), box(33));
  check_get(avl, 1, 11);
  check_get(avl, 2, 22);
  check_get(avl, 3, 33);
  check_negget(avl, 4);
  gpr_avl_destroy(avl);
}

static void test_ll(void) {
  gpr_avl avl;
  gpr_log(GPR_DEBUG, "test_ll");
  avl = gpr_avl_add(
      gpr_avl_add(gpr_avl_add(gpr_avl_create(&int_int_vtable), box(5), box(1)),
                  box(4), box(2)),
      box(3), box(3));
  GPR_ASSERT(*(int *)avl.root->key == 4);
  GPR_ASSERT(*(int *)avl.root->left->key == 3);
  GPR_ASSERT(*(int *)avl.root->right->key == 5);
  gpr_avl_destroy(avl);
}

static void test_lr(void) {
  gpr_avl avl;
  gpr_log(GPR_DEBUG, "test_lr");
  avl = gpr_avl_add(
      gpr_avl_add(gpr_avl_add(gpr_avl_create(&int_int_vtable), box(5), box(1)),
                  box(3), box(2)),
      box(4), box(3));
  GPR_ASSERT(*(int *)avl.root->key == 4);
  GPR_ASSERT(*(int *)avl.root->left->key == 3);
  GPR_ASSERT(*(int *)avl.root->right->key == 5);
  gpr_avl_destroy(avl);
}

static void test_rr(void) {
  gpr_avl avl;
  gpr_log(GPR_DEBUG, "test_rr");
  avl = gpr_avl_add(
      gpr_avl_add(gpr_avl_add(gpr_avl_create(&int_int_vtable), box(3), box(1)),
                  box(4), box(2)),
      box(5), box(3));
  GPR_ASSERT(*(int *)avl.root->key == 4);
  GPR_ASSERT(*(int *)avl.root->left->key == 3);
  GPR_ASSERT(*(int *)avl.root->right->key == 5);
  gpr_avl_destroy(avl);
}

static void test_rl(void) {
  gpr_avl avl;
  gpr_log(GPR_DEBUG, "test_rl");
  avl = gpr_avl_add(
      gpr_avl_add(gpr_avl_add(gpr_avl_create(&int_int_vtable), box(3), box(1)),
                  box(5), box(2)),
      box(4), box(3));
  GPR_ASSERT(*(int *)avl.root->key == 4);
  GPR_ASSERT(*(int *)avl.root->left->key == 3);
  GPR_ASSERT(*(int *)avl.root->right->key == 5);
  gpr_avl_destroy(avl);
}

static void test_unbalanced(void) {
  gpr_avl avl;
  gpr_log(GPR_DEBUG, "test_unbalanced");
  avl = gpr_avl_add(
      gpr_avl_add(
          gpr_avl_add(gpr_avl_add(gpr_avl_add(gpr_avl_create(&int_int_vtable),
                                              box(5), box(1)),
                                  box(4), box(2)),
                      box(3), box(3)),
          box(2), box(4)),
      box(1), box(5));
  GPR_ASSERT(*(int *)avl.root->key == 4);
  GPR_ASSERT(*(int *)avl.root->left->key == 2);
  GPR_ASSERT(*(int *)avl.root->left->left->key == 1);
  GPR_ASSERT(*(int *)avl.root->left->right->key == 3);
  GPR_ASSERT(*(int *)avl.root->right->key == 5);
  gpr_avl_destroy(avl);
}

static void test_replace(void) {
  gpr_avl avl;
  gpr_log(GPR_DEBUG, "test_replace");
  avl =
      gpr_avl_add(gpr_avl_add(gpr_avl_create(&int_int_vtable), box(1), box(1)),
                  box(1), box(2));
  check_get(avl, 1, 2);
  gpr_avl_destroy(avl);
}

int main(int argc, char *argv[]) {
  grpc_test_init(argc, argv);

  test_get();
  test_ll();
  test_lr();
  test_rr();
  test_rl();
  test_unbalanced();
  test_replace();

  return 0;
}
