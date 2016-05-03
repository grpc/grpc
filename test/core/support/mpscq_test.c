/*
 *
 * Copyright 2016, Google Inc.
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

#include "src/core/lib/support/mpscq.h"

#include <stdlib.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/useful.h>
#include "test/core/util/test_config.h"

typedef struct test_node {
  gpr_mpscq_node node;
  size_t i;
  size_t *ctr;
} test_node;

static test_node *new_node(size_t i, size_t *ctr) {
  test_node *n = gpr_malloc(sizeof(test_node));
  n->i = i;
  n->ctr = ctr;
  return n;
}

static void test_serial(void) {
  gpr_log(GPR_DEBUG, "test_serial");
  gpr_mpscq q;
  gpr_mpscq_init(&q);
  for (size_t i = 0; i < 10000000; i++) {
    gpr_mpscq_push(&q, &new_node(i, NULL)->node);
  }
  for (size_t i = 0; i < 10000000; i++) {
    test_node *n = (test_node *)gpr_mpscq_pop(&q);
    GPR_ASSERT(n);
    GPR_ASSERT(n->i == i);
    gpr_free(n);
  }
}

typedef struct {
  size_t ctr;
  gpr_mpscq *q;
} thd_args;

#define THREAD_ITERATIONS 1000000

static void test_thread(void *args) {
  thd_args *a = args;
  for (size_t i = 1; i <= THREAD_ITERATIONS; i++) {
    gpr_mpscq_push(a->q, &new_node(i, &a->ctr)->node);
  }
}

static void test_mt(void) {
  gpr_log(GPR_DEBUG, "test_mt");
  gpr_thd_id thds[100];
  thd_args ta[GPR_ARRAY_SIZE(thds)];
  gpr_mpscq q;
  gpr_mpscq_init(&q);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(thds); i++) {
    gpr_thd_options options = gpr_thd_options_default();
    gpr_thd_options_set_joinable(&options);
    ta[i].ctr = 0;
    ta[i].q = &q;
    GPR_ASSERT(gpr_thd_new(&thds[i], test_thread, &ta[i], &options));
  }
  size_t num_done = 0;
  size_t spins = 0;
  while (num_done != GPR_ARRAY_SIZE(thds)) {
    gpr_mpscq_node *n;
    while ((n = gpr_mpscq_pop(&q)) == NULL) {
      spins++;
    }
    test_node *tn = (test_node *)n;
    GPR_ASSERT(*tn->ctr == tn->i - 1);
    *tn->ctr = tn->i;
    if (tn->i == THREAD_ITERATIONS) num_done++;
    gpr_free(tn);
  }
  gpr_log(GPR_DEBUG, "spins: %d", spins);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(thds); i++) {
    gpr_thd_join(thds[i]);
  }
  gpr_mpscq_destroy(&q);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_serial();
  test_mt();
  return 0;
}
