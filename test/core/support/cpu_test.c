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
   5) Check that all cores were "used"

   The test will fail if:
   1) gpr_cpu_num_cores() == 0
   2) The result of gpr_cpu_current_cpu() >= gpr_cpu_num_cores()
   3) Not all cores are used/seen in the test. If a system does not exhibit
      this property (e.g. some cores reserved/unusable), then this condition
      will have to be rethought.
*/

/* Status shared across threads */
struct cpu_test {
  gpr_mu mu;
  int nthreads;
  gpr_uint32 ncores;
  int is_done;
  gpr_cv done_cv;
  int *used; /* is this core used? */
  int r;     /* random number */
};

static void worker_thread(void *arg) {
  struct cpu_test *ct = (struct cpu_test *)arg;
  gpr_uint32 cpu;
  int r = 12345678;
  int i, j;
  for (i = 0; i < 1000; i++) {
    /* run for a bit - just calculate something random. */
    for (j = 0; j < 1000000; j++) {
      r = (r * 17) & ((r - i) | (r * i));
    }
    cpu = gpr_cpu_current_cpu();
    GPR_ASSERT(cpu < ct->ncores);
    gpr_mu_lock(&ct->mu);
    ct->used[cpu] = 1;
    gpr_mu_unlock(&ct->mu);
  }
  gpr_mu_lock(&ct->mu);
  fprintf(stderr, "thread done on core %d\n", cpu);
  ct->r = r; /* make it look like we care about r's value... */
  ct->nthreads--;
  if (ct->nthreads == 0) {
    ct->is_done = 1;
    gpr_cv_signal(&ct->done_cv);
  }
  gpr_mu_unlock(&ct->mu);
}

static void cpu_test(void) {
  gpr_uint32 i;
  struct cpu_test ct;
  gpr_thd_id thd;
  ct.ncores = gpr_cpu_num_cores();
  GPR_ASSERT(ct.ncores > 0);
  fprintf(stderr, "#cores = %d\n", ct.ncores);
  ct.nthreads = (int)ct.ncores * 3;
  ct.used = gpr_malloc(ct.ncores * sizeof(int));
  memset(ct.used, 0, ct.ncores * sizeof(int));
  gpr_mu_init(&ct.mu);
  gpr_cv_init(&ct.done_cv);
  ct.is_done = 0;
  for (i = 0; i < ct.ncores * 3; i++) {
    GPR_ASSERT(gpr_thd_new(&thd, &worker_thread, &ct, NULL));
  }
  gpr_mu_lock(&ct.mu);
  while (!ct.is_done) {
    gpr_cv_wait(&ct.done_cv, &ct.mu, gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  gpr_mu_unlock(&ct.mu);
  for (i = 0; i < ct.ncores; i++) {
    GPR_ASSERT(ct.used[i]);
  }
}

int main(int argc, char *argv[]) {
  grpc_test_init(argc, argv);
  cpu_test();
  return 0;
}
