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

#include "src/core/lib/gprpp/mpscq.h"

#include <inttypes.h>
#include <stdlib.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/thd.h"
#include "test/core/util/test_config.h"

using grpc_core::MultiProducerSingleConsumerQueue;

typedef struct test_node {
  MultiProducerSingleConsumerQueue::Node node;
  size_t i;
  size_t* ctr;
} test_node;

static test_node* new_node(size_t i, size_t* ctr) {
  test_node* n = new test_node();
  n->i = i;
  n->ctr = ctr;
  return n;
}

static void test_serial(void) {
  gpr_log(GPR_DEBUG, "test_serial");
  MultiProducerSingleConsumerQueue q;
  for (size_t i = 0; i < 10000000; i++) {
    q.Push(&new_node(i, nullptr)->node);
  }
  for (size_t i = 0; i < 10000000; i++) {
    test_node* n = reinterpret_cast<test_node*>(q.Pop());
    GPR_ASSERT(n);
    GPR_ASSERT(n->i == i);
    delete n;
  }
}

typedef struct {
  size_t ctr;
  MultiProducerSingleConsumerQueue* q;
  gpr_event* start;
} thd_args;

#define THREAD_ITERATIONS 10000

static void test_thread(void* args) {
  thd_args* a = static_cast<thd_args*>(args);
  gpr_event_wait(a->start, gpr_inf_future(GPR_CLOCK_REALTIME));
  for (size_t i = 1; i <= THREAD_ITERATIONS; i++) {
    a->q->Push(&new_node(i, &a->ctr)->node);
  }
}

static void test_mt(void) {
  gpr_log(GPR_DEBUG, "test_mt");
  gpr_event start;
  gpr_event_init(&start);
  grpc_core::Thread thds[100];
  thd_args ta[GPR_ARRAY_SIZE(thds)];
  MultiProducerSingleConsumerQueue q;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(thds); i++) {
    ta[i].ctr = 0;
    ta[i].q = &q;
    ta[i].start = &start;
    thds[i] = grpc_core::Thread("grpc_mt_test", test_thread, &ta[i]);
    thds[i].Start();
  }
  size_t num_done = 0;
  size_t spins = 0;
  gpr_event_set(&start, reinterpret_cast<void*>(1));
  while (num_done != GPR_ARRAY_SIZE(thds)) {
    MultiProducerSingleConsumerQueue::Node* n;
    while ((n = q.Pop()) == nullptr) {
      spins++;
    }
    test_node* tn = reinterpret_cast<test_node*>(n);
    GPR_ASSERT(*tn->ctr == tn->i - 1);
    *tn->ctr = tn->i;
    if (tn->i == THREAD_ITERATIONS) num_done++;
    delete tn;
  }
  gpr_log(GPR_DEBUG, "spins: %" PRIdPTR, spins);
  for (auto& th : thds) {
    th.Join();
  }
}

typedef struct {
  thd_args* ta;
  size_t num_thds;
  gpr_mu mu;
  size_t num_done;
  size_t spins;
  MultiProducerSingleConsumerQueue* q;
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
    MultiProducerSingleConsumerQueue::Node* n;
    while ((n = pa->q->Pop()) == nullptr) {
      pa->spins++;
    }
    test_node* tn = reinterpret_cast<test_node*>(n);
    GPR_ASSERT(*tn->ctr == tn->i - 1);
    *tn->ctr = tn->i;
    if (tn->i == THREAD_ITERATIONS) pa->num_done++;
    delete tn;
    gpr_mu_unlock(&pa->mu);
  }
}

static void test_mt_multipop(void) {
  gpr_log(GPR_DEBUG, "test_mt_multipop");
  gpr_event start;
  gpr_event_init(&start);
  grpc_core::Thread thds[50];
  grpc_core::Thread pull_thds[50];
  thd_args ta[GPR_ARRAY_SIZE(thds)];
  MultiProducerSingleConsumerQueue q;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(thds); i++) {
    ta[i].ctr = 0;
    ta[i].q = &q;
    ta[i].start = &start;
    thds[i] = grpc_core::Thread("grpc_multipop_test", test_thread, &ta[i]);
    thds[i].Start();
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
    pull_thds[i] = grpc_core::Thread("grpc_multipop_pull", pull_thread, &pa);
    pull_thds[i].Start();
  }
  gpr_event_set(&start, reinterpret_cast<void*>(1));
  for (auto& pth : pull_thds) {
    pth.Join();
  }
  gpr_log(GPR_DEBUG, "spins: %" PRIdPTR, pa.spins);
  for (auto& th : thds) {
    th.Join();
  }
  gpr_mu_destroy(&pa.mu);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  test_serial();
  test_mt();
  test_mt_multipop();
  return 0;
}
