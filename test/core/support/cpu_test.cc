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

/* Test gpr per-cpu support:
   gpr_cpu_num_cores()
   gpr_cpu_current_cpu()
*/

#include <grpc/support/alloc.h>
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <stdio.h>
#include <string.h>
#include "test/core/util/test_config.h"

/* Test structure is essentially:
   1) Figure out how many cores are present on the test system
   2) Create 3 times that many threads
   3) Have each thread do some amount of work (basically want to
      gaurantee that all threads are running at once, and enough of them
      to run on all cores).
   4) Each thread checks what core it is running on, and marks that core
      as "used" in the test.
   5) Count number of "used" cores.

   The test will fail if:
   1) gpr_cpu_num_cores() == 0
   2) Any result from gpr_cpu_current_cpu() >= gpr_cpu_num_cores()
   3) Ideally, we would fail if not all cores were seen as used. Unfortunately,
      this is only probabilistically true, and depends on the OS, it's
      scheduler, etc. So we just print out an indication of how many were seen;
      hopefully developers can use this to sanity check their system.
*/

/* Status shared across threads */
struct cpu_test {
  gpr_mu mu;
  int nthreads;
  uint32_t ncores;
  int is_done;
  gpr_cv done_cv;
  int* used;  /* is this core used? */
  unsigned r; /* random number */
};

static void worker_thread(void* arg) {
  struct cpu_test* ct = (struct cpu_test*)arg;
  uint32_t cpu;
  unsigned r = 12345678;
  unsigned i, j;
  /* Avoid repetitive division calculations */
  int64_t max_i = 1000 / grpc_test_slowdown_factor();
  int64_t max_j = 1000 / grpc_test_slowdown_factor();
  for (i = 0; i < max_i; i++) {
    /* run for a bit - just calculate something random. */
    for (j = 0; j < max_j; j++) {
      r = (r * 17) & ((r - i) | (r * i));
    }
    cpu = gpr_cpu_current_cpu();
    GPR_ASSERT(cpu < ct->ncores);
    gpr_mu_lock(&ct->mu);
    ct->used[cpu] = 1;
    for (j = 0; j < ct->ncores; j++) {
      if (!ct->used[j]) break;
    }
    gpr_mu_unlock(&ct->mu);
    if (j == ct->ncores) {
      break; /* all cpus have been used - no further use in running this test */
    }
  }
  gpr_mu_lock(&ct->mu);
  ct->r = r; /* make it look like we care about r's value... */
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
  gpr_thd_id thd;
  ct.ncores = gpr_cpu_num_cores();
  GPR_ASSERT(ct.ncores > 0);
  ct.nthreads = (int)ct.ncores * 3;
  ct.used = static_cast<int*>(gpr_malloc(ct.ncores * sizeof(int)));
  memset(ct.used, 0, ct.ncores * sizeof(int));
  gpr_mu_init(&ct.mu);
  gpr_cv_init(&ct.done_cv);
  ct.is_done = 0;
  for (i = 0; i < ct.ncores * 3; i++) {
    GPR_ASSERT(gpr_thd_new(&thd, &worker_thread, &ct, nullptr));
  }
  gpr_mu_lock(&ct.mu);
  while (!ct.is_done) {
    gpr_cv_wait(&ct.done_cv, &ct.mu, gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  gpr_mu_unlock(&ct.mu);
  fprintf(stderr, "Saw cores [");
  for (i = 0; i < ct.ncores; i++) {
    if (ct.used[i]) {
      fprintf(stderr, "%d,", i);
      cores_seen++;
    }
  }
  fprintf(stderr, "] (%d/%d)\n", cores_seen, ct.ncores);
  gpr_free(ct.used);
}

int main(int argc, char* argv[]) {
  grpc_test_init(argc, argv);
  cpu_test();
  return 0;
}
