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

#include "src/core/lib/support/stack_lockfree.h"

#include <stdlib.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include "test/core/util/test_config.h"

/* max stack size supported */
#define MAX_STACK_SIZE 65534

#define MAX_THREADS 32

static void test_serial_sized(size_t size) {
  gpr_stack_lockfree *stack = gpr_stack_lockfree_create(size);
  size_t i;
  size_t j;

  /* First try popping empty */
  GPR_ASSERT(gpr_stack_lockfree_pop(stack) == -1);

  /* Now add one item and check it */
  gpr_stack_lockfree_push(stack, 3);
  GPR_ASSERT(gpr_stack_lockfree_pop(stack) == 3);
  GPR_ASSERT(gpr_stack_lockfree_pop(stack) == -1);

  /* Now add repeatedly more items and check them */
  for (i = 1; i < size; i *= 2) {
    for (j = 0; j <= i; j++) {
      GPR_ASSERT(gpr_stack_lockfree_push(stack, (int)j) == (j == 0));
    }
    for (j = 0; j <= i; j++) {
      GPR_ASSERT(gpr_stack_lockfree_pop(stack) == (int)(i - j));
    }
    GPR_ASSERT(gpr_stack_lockfree_pop(stack) == -1);
  }

  gpr_stack_lockfree_destroy(stack);
}

static void test_serial() {
  size_t i;
  for (i = 128; i < MAX_STACK_SIZE; i *= 2) {
    test_serial_sized(i);
  }
  test_serial_sized(MAX_STACK_SIZE);
}

struct test_arg {
  gpr_stack_lockfree *stack;
  int stack_size;
  int nthreads;
  int rank;
  int sum;
};

static void test_mt_body(void *v) {
  struct test_arg *arg = (struct test_arg *)v;
  int lo, hi;
  int i;
  int res;
  lo = arg->rank * arg->stack_size / arg->nthreads;
  hi = (arg->rank + 1) * arg->stack_size / arg->nthreads;
  for (i = lo; i < hi; i++) {
    gpr_stack_lockfree_push(arg->stack, i);
    if ((res = gpr_stack_lockfree_pop(arg->stack)) != -1) {
      arg->sum += res;
    }
  }
  while ((res = gpr_stack_lockfree_pop(arg->stack)) != -1) {
    arg->sum += res;
  }
}

static void test_mt_sized(size_t size, int nth) {
  gpr_stack_lockfree *stack;
  struct test_arg args[MAX_THREADS];
  gpr_thd_id thds[MAX_THREADS];
  int sum;
  int i;
  gpr_thd_options options = gpr_thd_options_default();

  stack = gpr_stack_lockfree_create(size);
  for (i = 0; i < nth; i++) {
    args[i].stack = stack;
    args[i].stack_size = (int)size;
    args[i].nthreads = nth;
    args[i].rank = i;
    args[i].sum = 0;
  }
  gpr_thd_options_set_joinable(&options);
  for (i = 0; i < nth; i++) {
    GPR_ASSERT(gpr_thd_new(&thds[i], test_mt_body, &args[i], &options));
  }
  sum = 0;
  for (i = 0; i < nth; i++) {
    gpr_thd_join(thds[i]);
    sum = sum + args[i].sum;
  }
  GPR_ASSERT((unsigned)sum == ((unsigned)size * (size - 1)) / 2);
  gpr_stack_lockfree_destroy(stack);
}

static void test_mt() {
  size_t size;
  int nth;
  for (nth = 1; nth < MAX_THREADS; nth++) {
    for (size = 128; size < MAX_STACK_SIZE; size *= 2) {
      test_mt_sized(size, nth);
    }
    test_mt_sized(MAX_STACK_SIZE, nth);
  }
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_serial();
  test_mt();
  return 0;
}
