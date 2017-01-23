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
#include "src/core/lib/iomgr/port.h"

/* This test only relevant on linux systems where epoll() is available */
#ifdef GRPC_LINUX_EPOLL
#include "src/core/lib/iomgr/ev_epoll_linux.h"
#include "src/core/lib/iomgr/ev_posix.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/workqueue.h"
#include "test/core/util/test_config.h"

typedef struct test_pollset {
  grpc_pollset *pollset;
  gpr_mu *mu;
} test_pollset;

typedef struct test_fd {
  int inner_fd;
  grpc_fd *fd;
} test_fd;

/* num_fds should be an even number */
static void test_fd_init(test_fd *tfds, int *fds, int num_fds) {
  int i;
  int r;

  /* Create some dummy file descriptors. Currently using pipe file descriptors
   * for this test but we could use any other type of file descriptors. Also,
   * since pipe() used in this test creates two fds in each call, num_fds should
   * be an even number */
  GPR_ASSERT((num_fds % 2) == 0);
  for (i = 0; i < num_fds; i = i + 2) {
    r = pipe(fds + i);
    if (r != 0) {
      gpr_log(GPR_ERROR, "Error in creating pipe. %d (%s)", errno,
              strerror(errno));
      return;
    }
  }

  for (i = 0; i < num_fds; i++) {
    tfds[i].inner_fd = fds[i];
    tfds[i].fd = grpc_fd_create(fds[i], "test_fd");
  }
}

static void test_fd_cleanup(grpc_exec_ctx *exec_ctx, test_fd *tfds,
                            int num_fds) {
  int release_fd;
  int i;

  for (i = 0; i < num_fds; i++) {
    grpc_fd_shutdown(exec_ctx, tfds[i].fd);
    grpc_exec_ctx_flush(exec_ctx);

    grpc_fd_orphan(exec_ctx, tfds[i].fd, NULL, &release_fd, "test_fd_cleanup");
    grpc_exec_ctx_flush(exec_ctx);

    GPR_ASSERT(release_fd == tfds[i].inner_fd);
    close(tfds[i].inner_fd);
  }
}

static void test_pollset_init(test_pollset *pollsets, int num_pollsets) {
  int i;
  for (i = 0; i < num_pollsets; i++) {
    pollsets[i].pollset = gpr_malloc(grpc_pollset_size());
    grpc_pollset_init(pollsets[i].pollset, &pollsets[i].mu);
  }
}

static void destroy_pollset(grpc_exec_ctx *exec_ctx, void *p,
                            grpc_error *error) {
  grpc_pollset_destroy(p);
}

static void test_pollset_cleanup(grpc_exec_ctx *exec_ctx,
                                 test_pollset *pollsets, int num_pollsets) {
  grpc_closure destroyed;
  int i;

  for (i = 0; i < num_pollsets; i++) {
    grpc_closure_init(&destroyed, destroy_pollset, pollsets[i].pollset,
                      grpc_schedule_on_exec_ctx);
    grpc_pollset_shutdown(exec_ctx, pollsets[i].pollset, &destroyed);

    grpc_exec_ctx_flush(exec_ctx);
    gpr_free(pollsets[i].pollset);
  }
}

static void increment(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  ++*(int *)arg;
}

/*
 * Validate that merging two workqueues preserves the closures in each queue.
 * This is a regression test for a bug in
 * polling_island_merge()[ev_epoll_linux.c], where the parent relationship was
 * inverted.
 */
static void test_pollset_queue_merge_items() {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  const int num_fds = 2;
  const int num_pollsets = 2;
  const int num_closures = 4;
  test_fd tfds[num_fds];
  int fds[num_fds];
  test_pollset pollsets[num_pollsets];
  grpc_closure closures[num_closures];
  int i;
  int result = 0;

  test_fd_init(tfds, fds, num_fds);
  test_pollset_init(pollsets, num_pollsets);

  /* Two distinct polling islands, each with their own FD and pollset. */
  for (i = 0; i < num_fds; i++) {
    grpc_pollset_add_fd(&exec_ctx, pollsets[i].pollset, tfds[i].fd);
    grpc_exec_ctx_flush(&exec_ctx);
  }

  /* Enqeue the closures, 3 to polling island 0 and 1 to polling island 1. */
  grpc_closure_init(
      closures, increment, &result,
      grpc_workqueue_scheduler(grpc_fd_get_polling_island(tfds[0].fd)));
  grpc_closure_init(
      closures + 1, increment, &result,
      grpc_workqueue_scheduler(grpc_fd_get_polling_island(tfds[0].fd)));
  grpc_closure_init(
      closures + 2, increment, &result,
      grpc_workqueue_scheduler(grpc_fd_get_polling_island(tfds[0].fd)));
  grpc_closure_init(
      closures + 3, increment, &result,
      grpc_workqueue_scheduler(grpc_fd_get_polling_island(tfds[1].fd)));
  for (i = 0; i < num_closures; ++i) {
    grpc_closure_sched(&exec_ctx, closures + i, GRPC_ERROR_NONE);
  }

  /* Merge the two polling islands. */
  grpc_pollset_add_fd(&exec_ctx, pollsets[0].pollset, tfds[1].fd);
  grpc_exec_ctx_flush(&exec_ctx);

  /*
   * Execute the closures, verify we see each one execute when executing work on
   * the merged polling island.
   */
  grpc_pollset_worker *worker = NULL;
  for (i = 0; i < num_closures; ++i) {
    const gpr_timespec deadline = gpr_time_add(
        gpr_now(GPR_CLOCK_MONOTONIC), gpr_time_from_seconds(2, GPR_TIMESPAN));
    gpr_mu_lock(pollsets[1].mu);
    GRPC_LOG_IF_ERROR(
        "grpc_pollset_work",
        grpc_pollset_work(&exec_ctx, pollsets[1].pollset, &worker,
                          gpr_now(GPR_CLOCK_MONOTONIC), deadline));
    gpr_mu_unlock(pollsets[1].mu);
  }
  GPR_ASSERT(result == num_closures);

  test_fd_cleanup(&exec_ctx, tfds, num_fds);
  test_pollset_cleanup(&exec_ctx, pollsets, num_pollsets);
  grpc_exec_ctx_finish(&exec_ctx);
}

/*
 * Cases to test:
 *  case 1) Polling islands of both fd and pollset are NULL
 *  case 2) Polling island of fd is NULL but that of pollset is not-NULL
 *  case 3) Polling island of fd is not-NULL but that of pollset is NULL
 *  case 4) Polling islands of both fd and pollset are not-NULL and:
 *     case 4.1) Polling islands of fd and pollset are equal
 *     case 4.2) Polling islands of fd and pollset are NOT-equal (This results
 *     in a merge)
 * */
static void test_add_fd_to_pollset() {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  const int num_fds = 8;
  const int num_pollsets = 4;
  test_fd tfds[num_fds];
  int fds[num_fds];
  test_pollset pollsets[num_pollsets];
  void *expected_pi = NULL;
  int i;

  test_fd_init(tfds, fds, num_fds);
  test_pollset_init(pollsets, num_pollsets);

  /*Step 1.
   * Create three polling islands (This will exercise test case 1 and 2) with
   * the following configuration:
   *   polling island 0 = { fds:0,1,2, pollsets:0}
   *   polling island 1 = { fds:3,4,   pollsets:1}
   *   polling island 2 = { fds:5,6,7  pollsets:2}
   *
   *Step 2.
   * Add pollset 3 to polling island 0 (by adding fds 0 and 1 to pollset 3)
   * (This will exercise test cases 3 and 4.1). The configuration becomes:
   *   polling island 0 = { fds:0,1,2, pollsets:0,3} <<< pollset 3 added here
   *   polling island 1 = { fds:3,4,   pollsets:1}
   *   polling island 2 = { fds:5,6,7  pollsets:2}
   *
   *Step 3.
   * Merge polling islands 0 and 1 by adding fd 0 to pollset 1 (This will
   * exercise test case 4.2). The configuration becomes:
   *   polling island (merged) = {fds: 0,1,2,3,4, pollsets: 0,1,3}
   *   polling island 2 = {fds: 5,6,7 pollsets: 2}
   *
   *Step 4.
   * Finally do one more merge by adding fd 3 to pollset 2.
   *   polling island (merged) = {fds: 0,1,2,3,4,5,6,7, pollsets: 0,1,2,3}
   */

  /* == Step 1 == */
  for (i = 0; i <= 2; i++) {
    grpc_pollset_add_fd(&exec_ctx, pollsets[0].pollset, tfds[i].fd);
    grpc_exec_ctx_flush(&exec_ctx);
  }

  for (i = 3; i <= 4; i++) {
    grpc_pollset_add_fd(&exec_ctx, pollsets[1].pollset, tfds[i].fd);
    grpc_exec_ctx_flush(&exec_ctx);
  }

  for (i = 5; i <= 7; i++) {
    grpc_pollset_add_fd(&exec_ctx, pollsets[2].pollset, tfds[i].fd);
    grpc_exec_ctx_flush(&exec_ctx);
  }

  /* == Step 2 == */
  for (i = 0; i <= 1; i++) {
    grpc_pollset_add_fd(&exec_ctx, pollsets[3].pollset, tfds[i].fd);
    grpc_exec_ctx_flush(&exec_ctx);
  }

  /* == Step 3 == */
  grpc_pollset_add_fd(&exec_ctx, pollsets[1].pollset, tfds[0].fd);
  grpc_exec_ctx_flush(&exec_ctx);

  /* == Step 4 == */
  grpc_pollset_add_fd(&exec_ctx, pollsets[2].pollset, tfds[3].fd);
  grpc_exec_ctx_flush(&exec_ctx);

  /* All polling islands are merged at this point */

  /* Compare Fd:0's polling island with that of all other Fds */
  expected_pi = grpc_fd_get_polling_island(tfds[0].fd);
  for (i = 1; i < num_fds; i++) {
    GPR_ASSERT(grpc_are_polling_islands_equal(
        expected_pi, grpc_fd_get_polling_island(tfds[i].fd)));
  }

  /* Compare Fd:0's polling island with that of all other pollsets */
  for (i = 0; i < num_pollsets; i++) {
    GPR_ASSERT(grpc_are_polling_islands_equal(
        expected_pi, grpc_pollset_get_polling_island(pollsets[i].pollset)));
  }

  test_fd_cleanup(&exec_ctx, tfds, num_fds);
  test_pollset_cleanup(&exec_ctx, pollsets, num_pollsets);
  grpc_exec_ctx_finish(&exec_ctx);
}

int main(int argc, char **argv) {
  const char *poll_strategy = NULL;
  grpc_test_init(argc, argv);
  grpc_iomgr_init();

  poll_strategy = grpc_get_poll_strategy_name();
  if (poll_strategy != NULL && strcmp(poll_strategy, "epoll") == 0) {
    test_add_fd_to_pollset();
    test_pollset_queue_merge_items();
  } else {
    gpr_log(GPR_INFO,
            "Skipping the test. The test is only relevant for 'epoll' "
            "strategy. and the current strategy is: '%s'",
            poll_strategy);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_iomgr_shutdown(&exec_ctx);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  return 0;
}
#else /* defined(GRPC_LINUX_EPOLL) */
int main(int argc, char **argv) { return 0; }
#endif /* !defined(GRPC_LINUX_EPOLL) */
