/*
 *
 * Copyright 2016 gRPC authors.
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

#include "src/core/lib/support/mpscq.h"

#include <stdlib.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/useful.h>
#include "test/core/util/test_config.h"

typedef struct test_node {
  gpr_mpscq_node node;
  size_t i;
  size_t* ctr;
} test_node;

static test_node* new_node(size_t i, size_t* ctr) {
  test_node* n = static_cast<test_node*>(gpr_malloc(sizeof(test_node)));
  n->i = i;
  n->ctr = ctr;
  return n;
}

static void test_serial(void) {
  gpr_log(GPR_DEBUG, "test_serial");
  gpr_mpscq q;
  gpr_mpscq_init(&q);
  for (size_t i = 0; i < 10000000; i++) {
    gpr_mpscq_push(&q, &new_node(i, nullptr)->node);
  }
  for (size_t i = 0; i < 10000000; i++) {
    test_node* n = (test_node*)gpr_mpscq_pop(&q);
    GPR_ASSERT(n);
    GPR_ASSERT(n->i == i);
    gpr_free(n);
  }
}

typedef struct {
  size_t ctr;
  gpr_mpscq* q;
  gpr_event* start;
} thd_args;

#define THREAD_ITERATIONS 10000

static void test_thread(void* args) {
  thd_args* a = static_cast<thd_args*>(args);
  gpr_event_wait(a->start, gpr_inf_future(GPR_CLOCK_REALTIME));
  for (size_t i = 1; i <= THREAD_ITERATIONS; i++) {
    gpr_mpscq_push(a->q, &new_node(i, &a->ctr)->node);
  }
}

static void test_mt(void) {
  gpr_log(GPR_DEBUG, "test_mt");
  gpr_event start;
  gpr_event_init(&start);
  gpr_thd_id thds[100];
  thd_args ta[GPR_ARRAY_SIZE(thds)];
  gpr_mpscq q;
  gpr_mpscq_init(&q);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(thds); i++) {
    gpr_thd_options options = gpr_thd_options_default();
    gpr_thd_options_set_joinable(&options);
    ta[i].ctr = 0;
    ta[i].q = &q;
    ta[i].start = &start;
    GPR_ASSERT(gpr_thd_new(&thds[i], test_thread, &ta[i], &options));
  }
  size_t num_done = 0;
  size_t spins = 0;
  gpr_event_set(&start, (void*)1);
  while (num_done != GPR_ARRAY_SIZE(thds)) {
    gpr_mpscq_node* n;
    while ((n = gpr_mpscq_pop(&q)) == nullptr) {
      spins++;
    }
    test_node* tn = (test_node*)n;
    GPR_ASSERT(*tn->ctr == tn->i - 1);
    *tn->ctr = tn->i;
    if (tn->i == THREAD_ITERATIONS) num_done++;
    gpr_free(tn);
  }
  gpr_log(GPR_DEBUG, "spins: %" PRIdPTR, spins);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(thds); i++) {
    gpr_thd_join(thds[i]);
  }
  gpr_mpscq_destroy(&q);
}

typedef struct {
  thd_args* ta;
  size_t num_thds;
  gpr_mu mu;
  size_t num_done;
  size_t spins;
  gpr_mpscq* q;
  gpr_event* start;
} pull_args;

static void pull_thread(void* arg) {
  pull_args* pa = static_cast<pull_args*>(arg);
  gpr_event_wait(pa->start, gpr_inf_future(GPR_CLOCK_REALTIME));

  for (;;) {
    gpr_mu_lock(&pa->mu);
    if (pa->num_done == pa->num_thds) {
      gpr_mu_unlock(&pa->mu);
      return;
    }
    gpr_mpscq_node* n;
    while ((n = gpr_mpscq_pop(pa->q)) == nullptr) {
      pa->spins++;
    }
    test_node* tn = (test_node*)n;
    GPR_ASSERT(*tn->ctr == tn->i - 1);
    *tn->ctr = tn->i;
    if (tn->i == THREAD_ITERATIONS) pa->num_done++;
    gpr_free(tn);
    gpr_mu_unlock(&pa->mu);
  }
}

static void test_mt_multipop(void) {
  gpr_log(GPR_DEBUG, "test_mt_multipop");
  gpr_event start;
  gpr_event_init(&start);
  gpr_thd_id thds[100];
  gpr_thd_id pull_thds[100];
  thd_args ta[GPR_ARRAY_SIZE(thds)];
  gpr_mpscq q;
  gpr_mpscq_init(&q);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(thds); i++) {
    gpr_thd_options options = gpr_thd_options_default();
    gpr_thd_options_set_joinable(&options);
    ta[i].ctr = 0;
    ta[i].q = &q;
    ta[i].start = &start;
    GPR_ASSERT(gpr_thd_new(&thds[i], test_thread, &ta[i], &options));
  }
  pull_args pa;
  pa.ta = ta;
  pa.num_thds = GPR_ARRAY_SIZE(thds);
  pa.spins = 0;
  pa.num_done = 0;
  pa.q = &q;
  pa.start = &start;
  gpr_mu_init(&pa.mu);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pull_thds); i++) {
    gpr_thd_options options = gpr_thd_options_default();
    gpr_thd_options_set_joinable(&options);
    GPR_ASSERT(gpr_thd_new(&pull_thds[i], pull_thread, &pa, &options));
  }
  gpr_event_set(&start, (void*)1);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pull_thds); i++) {
    gpr_thd_join(pull_thds[i]);
  }
  gpr_log(GPR_DEBUG, "spins: %" PRIdPTR, pa.spins);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(thds); i++) {
    gpr_thd_join(thds[i]);
  }
  gpr_mpscq_destroy(&q);
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  test_serial();
  test_mt();
  test_mt_multipop();
  return 0;
}
