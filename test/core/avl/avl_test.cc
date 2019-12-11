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

#include "src/core/lib/avl/avl.h"

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/useful.h"
#include "test/core/util/test_config.h"

static int* box(int x) {
  int* b = static_cast<int*>(gpr_malloc(sizeof(*b)));
  *b = x;
  return b;
}

static long int_compare(void* int1, void* int2, void* /*unused*/) {
  return (*static_cast<int*>(int1)) - (*static_cast<int*>(int2));
}
static void* int_copy(void* p, void* /*unused*/) {
  return box(*static_cast<int*>(p));
}

static void destroy(void* p, void* /*unused*/) { gpr_free(p); }

static const grpc_avl_vtable int_int_vtable = {destroy, int_copy, int_compare,
                                               destroy, int_copy};

static void check_get(grpc_avl avl, int key, int value) {
  int* k = box(key);
  GPR_ASSERT(*(int*)grpc_avl_get(avl, k, nullptr) == value);
  gpr_free(k);
}

static void check_negget(grpc_avl avl, int key) {
  int* k = box(key);
  GPR_ASSERT(grpc_avl_get(avl, k, nullptr) == nullptr);
  gpr_free(k);
}

static grpc_avl remove_int(grpc_avl avl, int key) {
  int* k = box(key);
  avl = grpc_avl_remove(avl, k, nullptr);
  gpr_free(k);
  return avl;
}

static void test_get(void) {
  grpc_avl avl;
  gpr_log(GPR_DEBUG, "test_get");
  avl = grpc_avl_create(&int_int_vtable);
  avl = grpc_avl_add(avl, box(1), box(11), nullptr);
  avl = grpc_avl_add(avl, box(2), box(22), nullptr);
  avl = grpc_avl_add(avl, box(3), box(33), nullptr);
  check_get(avl, 1, 11);
  check_get(avl, 2, 22);
  check_get(avl, 3, 33);
  check_negget(avl, 4);
  grpc_avl_unref(avl, nullptr);
}

static void test_ll(void) {
  grpc_avl avl;
  gpr_log(GPR_DEBUG, "test_ll");
  avl = grpc_avl_create(&int_int_vtable);
  avl = grpc_avl_add(avl, box(5), box(1), nullptr);
  avl = grpc_avl_add(avl, box(4), box(2), nullptr);
  avl = grpc_avl_add(avl, box(3), box(3), nullptr);
  GPR_ASSERT(*(int*)avl.root->key == 4);
  GPR_ASSERT(*(int*)avl.root->left->key == 3);
  GPR_ASSERT(*(int*)avl.root->right->key == 5);
  grpc_avl_unref(avl, nullptr);
}

static void test_lr(void) {
  grpc_avl avl;
  gpr_log(GPR_DEBUG, "test_lr");
  avl = grpc_avl_create(&int_int_vtable);
  avl = grpc_avl_add(avl, box(5), box(1), nullptr);
  avl = grpc_avl_add(avl, box(3), box(2), nullptr);
  avl = grpc_avl_add(avl, box(4), box(3), nullptr);
  GPR_ASSERT(*(int*)avl.root->key == 4);
  GPR_ASSERT(*(int*)avl.root->left->key == 3);
  GPR_ASSERT(*(int*)avl.root->right->key == 5);
  grpc_avl_unref(avl, nullptr);
}

static void test_rr(void) {
  grpc_avl avl;
  gpr_log(GPR_DEBUG, "test_rr");
  avl = grpc_avl_create(&int_int_vtable);
  avl = grpc_avl_add(avl, box(3), box(1), nullptr);
  avl = grpc_avl_add(avl, box(4), box(2), nullptr);
  avl = grpc_avl_add(avl, box(5), box(3), nullptr);
  GPR_ASSERT(*(int*)avl.root->key == 4);
  GPR_ASSERT(*(int*)avl.root->left->key == 3);
  GPR_ASSERT(*(int*)avl.root->right->key == 5);
  grpc_avl_unref(avl, nullptr);
}

static void test_rl(void) {
  grpc_avl avl;
  gpr_log(GPR_DEBUG, "test_rl");
  avl = grpc_avl_create(&int_int_vtable);
  avl = grpc_avl_add(avl, box(3), box(1), nullptr);
  avl = grpc_avl_add(avl, box(5), box(2), nullptr);
  avl = grpc_avl_add(avl, box(4), box(3), nullptr);
  GPR_ASSERT(*(int*)avl.root->key == 4);
  GPR_ASSERT(*(int*)avl.root->left->key == 3);
  GPR_ASSERT(*(int*)avl.root->right->key == 5);
  grpc_avl_unref(avl, nullptr);
}

static void test_unbalanced(void) {
  grpc_avl avl;
  gpr_log(GPR_DEBUG, "test_unbalanced");
  avl = grpc_avl_create(&int_int_vtable);
  avl = grpc_avl_add(avl, box(5), box(1), nullptr);
  avl = grpc_avl_add(avl, box(4), box(2), nullptr);
  avl = grpc_avl_add(avl, box(3), box(3), nullptr);
  avl = grpc_avl_add(avl, box(2), box(4), nullptr);
  avl = grpc_avl_add(avl, box(1), box(5), nullptr);
  GPR_ASSERT(*(int*)avl.root->key == 4);
  GPR_ASSERT(*(int*)avl.root->left->key == 2);
  GPR_ASSERT(*(int*)avl.root->left->left->key == 1);
  GPR_ASSERT(*(int*)avl.root->left->right->key == 3);
  GPR_ASSERT(*(int*)avl.root->right->key == 5);
  grpc_avl_unref(avl, nullptr);
}

static void test_replace(void) {
  grpc_avl avl;
  gpr_log(GPR_DEBUG, "test_replace");
  avl = grpc_avl_create(&int_int_vtable);
  avl = grpc_avl_add(avl, box(1), box(1), nullptr);
  avl = grpc_avl_add(avl, box(1), box(2), nullptr);
  check_get(avl, 1, 2);
  check_negget(avl, 2);
  grpc_avl_unref(avl, nullptr);
}

static void test_remove(void) {
  grpc_avl avl;
  grpc_avl avl3, avl4, avl5, avln;
  gpr_log(GPR_DEBUG, "test_remove");
  avl = grpc_avl_create(&int_int_vtable);
  avl = grpc_avl_add(avl, box(3), box(1), nullptr);
  avl = grpc_avl_add(avl, box(4), box(2), nullptr);
  avl = grpc_avl_add(avl, box(5), box(3), nullptr);

  avl3 = remove_int(grpc_avl_ref(avl, nullptr), 3);
  avl4 = remove_int(grpc_avl_ref(avl, nullptr), 4);
  avl5 = remove_int(grpc_avl_ref(avl, nullptr), 5);
  avln = remove_int(grpc_avl_ref(avl, nullptr), 1);

  grpc_avl_unref(avl, nullptr);

  check_negget(avl3, 3);
  check_get(avl3, 4, 2);
  check_get(avl3, 5, 3);
  grpc_avl_unref(avl3, nullptr);

  check_get(avl4, 3, 1);
  check_negget(avl4, 4);
  check_get(avl4, 5, 3);
  grpc_avl_unref(avl4, nullptr);

  check_get(avl5, 3, 1);
  check_get(avl5, 4, 2);
  check_negget(avl5, 5);
  grpc_avl_unref(avl5, nullptr);

  check_get(avln, 3, 1);
  check_get(avln, 4, 2);
  check_get(avln, 5, 3);
  grpc_avl_unref(avln, nullptr);
}

static void test_badcase1(void) {
  grpc_avl avl;

  gpr_log(GPR_DEBUG, "test_badcase1");

  avl = grpc_avl_create(&int_int_vtable);
  avl = grpc_avl_add(avl, box(88), box(1), nullptr);
  avl = remove_int(avl, 643);
  avl = remove_int(avl, 983);
  avl = grpc_avl_add(avl, box(985), box(4), nullptr);
  avl = grpc_avl_add(avl, box(640), box(5), nullptr);
  avl = grpc_avl_add(avl, box(41), box(6), nullptr);
  avl = grpc_avl_add(avl, box(112), box(7), nullptr);
  avl = grpc_avl_add(avl, box(342), box(8), nullptr);
  avl = remove_int(avl, 1013);
  avl = grpc_avl_add(avl, box(434), box(10), nullptr);
  avl = grpc_avl_add(avl, box(520), box(11), nullptr);
  avl = grpc_avl_add(avl, box(231), box(12), nullptr);
  avl = grpc_avl_add(avl, box(852), box(13), nullptr);
  avl = remove_int(avl, 461);
  avl = grpc_avl_add(avl, box(108), box(15), nullptr);
  avl = grpc_avl_add(avl, box(806), box(16), nullptr);
  avl = grpc_avl_add(avl, box(827), box(17), nullptr);
  avl = remove_int(avl, 796);
  avl = grpc_avl_add(avl, box(340), box(19), nullptr);
  avl = grpc_avl_add(avl, box(498), box(20), nullptr);
  avl = grpc_avl_add(avl, box(203), box(21), nullptr);
  avl = grpc_avl_add(avl, box(751), box(22), nullptr);
  avl = grpc_avl_add(avl, box(150), box(23), nullptr);
  avl = remove_int(avl, 237);
  avl = grpc_avl_add(avl, box(830), box(25), nullptr);
  avl = remove_int(avl, 1007);
  avl = remove_int(avl, 394);
  avl = grpc_avl_add(avl, box(65), box(28), nullptr);
  avl = remove_int(avl, 904);
  avl = remove_int(avl, 123);
  avl = grpc_avl_add(avl, box(238), box(31), nullptr);
  avl = grpc_avl_add(avl, box(184), box(32), nullptr);
  avl = remove_int(avl, 331);
  avl = grpc_avl_add(avl, box(827), box(34), nullptr);

  check_get(avl, 830, 25);

  grpc_avl_unref(avl, nullptr);
}

static void test_stress(int amount_of_stress) {
  int added[1024];
  int i, j;
  int deletions = 0;
  grpc_avl avl;

  unsigned seed = static_cast<unsigned>(time(nullptr));

  gpr_log(GPR_DEBUG, "test_stress amount=%d seed=%u", amount_of_stress, seed);

  srand(static_cast<unsigned>(time(nullptr)));
  avl = grpc_avl_create(&int_int_vtable);

  memset(added, 0, sizeof(added));

  for (i = 1; deletions < amount_of_stress; i++) {
    int idx = rand() % static_cast<int> GPR_ARRAY_SIZE(added);
    GPR_ASSERT(i);
    if (rand() < RAND_MAX / 2) {
      added[idx] = i;
      printf("avl = grpc_avl_add(avl, box(%d), box(%d), NULL); /* d=%d */\n",
             idx, i, deletions);
      avl = grpc_avl_add(avl, box(idx), box(i), nullptr);
    } else {
      deletions += (added[idx] != 0);
      added[idx] = 0;
      printf("avl = remove_int(avl, %d); /* d=%d */\n", idx, deletions);
      avl = remove_int(avl, idx);
    }
    for (j = 0; j < static_cast<int> GPR_ARRAY_SIZE(added); j++) {
      if (added[j] != 0) {
        check_get(avl, j, added[j]);
      } else {
        check_negget(avl, j);
      }
    }
  }

  grpc_avl_unref(avl, nullptr);
}

int main(int argc, char* argv[]) {
  grpc::testing::TestEnvironment env(argc, argv);

  test_get();
  test_ll();
  test_lr();
  test_rr();
  test_rl();
  test_unbalanced();
  test_replace();
  test_remove();
  test_badcase1();
  test_stress(10);

  return 0;
}
