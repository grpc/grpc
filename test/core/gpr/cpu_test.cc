//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

// Test gpr per-cpu support:
// gpr_cpu_num_cores()
// gpr_cpu_current_cpu()
//

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gtest/gtest.h"

#include <grpc/support/alloc.h>
#include <grpc/support/cpu.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/thd.h"
#include "test/core/util/test_config.h"

// Test structure is essentially:
// 1) Figure out how many cores are present on the test system
// 2) Create 3 times that many threads
// 3) Have each thread do some amount of work (basically want to
//    gaurantee that all threads are running at once, and enough of them
//    to run on all cores).
// 4) Each thread checks what core it is running on, and marks that core
//    as "used" in the test.
// 5) Count number of "used" cores.

// The test will fail if:
// 1) gpr_cpu_num_cores() == 0
// 2) Any result from gpr_cpu_current_cpu() >= gpr_cpu_num_cores()
// 3) Ideally, we would fail if not all cores were seen as used. Unfortunately,
//    this is only probabilistically true, and depends on the OS, it's
//    scheduler, etc. So we just print out an indication of how many were seen;
//    hopefully developers can use this to sanity check their system.
//

// Status shared across threads
struct cpu_test {
  gpr_mu mu;
  int nthreads;
  uint32_t ncores;
  int is_done;
  gpr_cv done_cv;
  int* used;   // is this core used?
  unsigned r;  // random number
};

static void worker_thread(void* arg) {
  struct cpu_test* ct = static_cast<struct cpu_test*>(arg);
  uint32_t cpu;
  unsigned r = 12345678;
  unsigned i, j;
  // Avoid repetitive division calculations
  int64_t max_i = 1000 / grpc_test_slowdown_factor();
  int64_t max_j = 1000 / grpc_test_slowdown_factor();
  for (i = 0; i < max_i; i++) {
    // run for a bit - just calculate something random.
    for (j = 0; j < max_j; j++) {
      r = (r * 17) & ((r - i) | (r * i));
    }
    cpu = gpr_cpu_current_cpu();
    ASSERT_LT(cpu, ct->ncores);
    gpr_mu_lock(&ct->mu);
    ct->used[cpu] = 1;
    for (j = 0; j < ct->ncores; j++) {
      if (!ct->used[j]) break;
    }
    gpr_mu_unlock(&ct->mu);
    if (j == ct->ncores) {
      break;  // all cpus have been used - no further use in running this test
    }
  }
  gpr_mu_lock(&ct->mu);
  ct->r = r;  // make it look like we care about r's value...
  ct->nthreads--;
  if (ct->nthreads == 0) {
    ct->is_done = 1;
    gpr_cv_signal(&ct->done_cv);
  }
  gpr_mu_unlock(&ct->mu);
}

static void cpu_test(void) {
  uint32_t i;
  int cores_seen = 0;
  struct cpu_test ct;
  ct.ncores = gpr_cpu_num_cores();
  ASSERT_GT(ct.ncores, 0);
  ct.nthreads = static_cast<int>(ct.ncores) * 3;
  ct.used = static_cast<int*>(gpr_malloc(ct.ncores * sizeof(int)));
  memset(ct.used, 0, ct.ncores * sizeof(int));
  gpr_mu_init(&ct.mu);
  gpr_cv_init(&ct.done_cv);
  ct.is_done = 0;

  uint32_t nthreads = ct.ncores * 3;
  grpc_core::Thread* thd =
      static_cast<grpc_core::Thread*>(gpr_malloc(sizeof(*thd) * nthreads));

  for (i = 0; i < nthreads; i++) {
    thd[i] = grpc_core::Thread("grpc_cpu_test", &worker_thread, &ct);
    thd[i].Start();
  }
  gpr_mu_lock(&ct.mu);
  while (!ct.is_done) {
    gpr_cv_wait(&ct.done_cv, &ct.mu, gpr_inf_future(GPR_CLOCK_MONOTONIC));
  }
  gpr_mu_unlock(&ct.mu);
  for (i = 0; i < nthreads; i++) {
    thd[i].Join();
  }
  gpr_free(thd);
  fprintf(stderr, "Saw cores [");
  fflush(stderr);
  for (i = 0; i < ct.ncores; i++) {
    if (ct.used[i]) {
      fprintf(stderr, "%d,", i);
      fflush(stderr);
      cores_seen++;
    }
  }
  fprintf(stderr, "] (%d/%d)\n", cores_seen, ct.ncores);
  fflush(stderr);
  gpr_mu_destroy(&ct.mu);
  gpr_cv_destroy(&ct.done_cv);
  gpr_free(ct.used);
}

TEST(CpuTest, MainTest) { cpu_test(); }

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
