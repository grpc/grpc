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
#include "src/core/lib/iomgr/port.h"

/* This test only relevant on linux systems where epoll() is available */
#ifdef GRPC_LINUX_EPOLL
#include "src/core/lib/iomgr/ev_epollsig_linux.h"
#include "src/core/lib/iomgr/ev_posix.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/iomgr.h"
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
    grpc_fd_shutdown(exec_ctx, tfds[i].fd,
                     GRPC_ERROR_CREATE_FROM_STATIC_STRING("test_fd_cleanup"));
    grpc_exec_ctx_flush(exec_ctx);

    grpc_fd_orphan(exec_ctx, tfds[i].fd, NULL, &release_fd,
                   false /* already_closed */, "test_fd_cleanup");
    grpc_exec_ctx_flush(exec_ctx);

    GPR_ASSERT(release_fd == tfds[i].inner_fd);
    close(tfds[i].inner_fd);
  }
}

static void test_pollset_init(test_pollset *pollsets, int num_pollsets) {
  int i;
  for (i = 0; i < num_pollsets; i++) {
    pollsets[i].pollset = gpr_zalloc(grpc_pollset_size());
    grpc_pollset_init(pollsets[i].pollset, &pollsets[i].mu);
  }
}

static void destroy_pollset(grpc_exec_ctx *exec_ctx, void *p,
                            grpc_error *error) {
  grpc_pollset_destroy(exec_ctx, p);
}

static void test_pollset_cleanup(grpc_exec_ctx *exec_ctx,
                                 test_pollset *pollsets, int num_pollsets) {
  grpc_closure destroyed;
  int i;

  for (i = 0; i < num_pollsets; i++) {
    GRPC_CLOSURE_INIT(&destroyed, destroy_pollset, pollsets[i].pollset,
                      grpc_schedule_on_exec_ctx);
    grpc_pollset_shutdown(exec_ctx, pollsets[i].pollset, &destroyed);

    grpc_exec_ctx_flush(exec_ctx);
    gpr_free(pollsets[i].pollset);
  }
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

#define NUM_FDS 8
#define NUM_POLLSETS 4

static void test_add_fd_to_pollset() {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  test_fd tfds[NUM_FDS];
  int fds[NUM_FDS];
  test_pollset pollsets[NUM_POLLSETS];
  void *expected_pi = NULL;
  int i;

  test_fd_init(tfds, fds, NUM_FDS);
  test_pollset_init(pollsets, NUM_POLLSETS);

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
  for (i = 1; i < NUM_FDS; i++) {
    GPR_ASSERT(grpc_are_polling_islands_equal(
        expected_pi, grpc_fd_get_polling_island(tfds[i].fd)));
  }

  /* Compare Fd:0's polling island with that of all other pollsets */
  for (i = 0; i < NUM_POLLSETS; i++) {
    GPR_ASSERT(grpc_are_polling_islands_equal(
        expected_pi, grpc_pollset_get_polling_island(pollsets[i].pollset)));
  }

  test_fd_cleanup(&exec_ctx, tfds, NUM_FDS);
  test_pollset_cleanup(&exec_ctx, pollsets, NUM_POLLSETS);
  grpc_exec_ctx_finish(&exec_ctx);
}

#undef NUM_FDS
#undef NUM_POLLSETS

typedef struct threading_shared {
  gpr_mu *mu;
  grpc_pollset *pollset;
  grpc_wakeup_fd *wakeup_fd;
  grpc_fd *wakeup_desc;
  grpc_closure on_wakeup;
  int wakeups;
} threading_shared;

static __thread int thread_wakeups = 0;

static void test_threading_loop(void *arg) {
  threading_shared *shared = arg;
  while (thread_wakeups < 1000000) {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_pollset_worker *worker;
    gpr_mu_lock(shared->mu);
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_work",
        grpc_pollset_work(&exec_ctx, shared->pollset, &worker,
                          gpr_now(GPR_CLOCK_MONOTONIC),
                          gpr_inf_future(GPR_CLOCK_MONOTONIC))));
    gpr_mu_unlock(shared->mu);
    grpc_exec_ctx_finish(&exec_ctx);
  }
}

static void test_threading_wakeup(grpc_exec_ctx *exec_ctx, void *arg,
                                  grpc_error *error) {
  threading_shared *shared = arg;
  ++shared->wakeups;
  ++thread_wakeups;
  if (error == GRPC_ERROR_NONE) {
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "consume_wakeup", grpc_wakeup_fd_consume_wakeup(shared->wakeup_fd)));
    grpc_fd_notify_on_read(exec_ctx, shared->wakeup_desc, &shared->on_wakeup);
    GPR_ASSERT(GRPC_LOG_IF_ERROR("wakeup_next",
                                 grpc_wakeup_fd_wakeup(shared->wakeup_fd)));
  }
}

static void test_threading(void) {
  threading_shared shared;
  shared.pollset = gpr_zalloc(grpc_pollset_size());
  grpc_pollset_init(shared.pollset, &shared.mu);

  gpr_thd_id thds[10];
  for (size_t i = 0; i < GPR_ARRAY_SIZE(thds); i++) {
    gpr_thd_options opt = gpr_thd_options_default();
    gpr_thd_options_set_joinable(&opt);
    gpr_thd_new(&thds[i], test_threading_loop, &shared, &opt);
  }
  grpc_wakeup_fd fd;
  GPR_ASSERT(GRPC_LOG_IF_ERROR("wakeup_fd_init", grpc_wakeup_fd_init(&fd)));
  shared.wakeup_fd = &fd;
  shared.wakeup_desc = grpc_fd_create(fd.read_fd, "wakeup");
  shared.wakeups = 0;
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_pollset_add_fd(&exec_ctx, shared.pollset, shared.wakeup_desc);
    grpc_fd_notify_on_read(
        &exec_ctx, shared.wakeup_desc,
        GRPC_CLOSURE_INIT(&shared.on_wakeup, test_threading_wakeup, &shared,
                          grpc_schedule_on_exec_ctx));
    grpc_exec_ctx_finish(&exec_ctx);
  }
  GPR_ASSERT(GRPC_LOG_IF_ERROR("wakeup_first",
                               grpc_wakeup_fd_wakeup(shared.wakeup_fd)));
  for (size_t i = 0; i < GPR_ARRAY_SIZE(thds); i++) {
    gpr_thd_join(thds[i]);
  }
  fd.read_fd = 0;
  grpc_wakeup_fd_destroy(&fd);
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_fd_shutdown(&exec_ctx, shared.wakeup_desc, GRPC_ERROR_CANCELLED);
    grpc_fd_orphan(&exec_ctx, shared.wakeup_desc, NULL, NULL,
                   false /* already_closed */, "done");
    grpc_pollset_shutdown(&exec_ctx, shared.pollset,
                          GRPC_CLOSURE_CREATE(destroy_pollset, shared.pollset,
                                              grpc_schedule_on_exec_ctx));
    grpc_exec_ctx_finish(&exec_ctx);
  }
  gpr_free(shared.pollset);
}

int main(int argc, char **argv) {
  const char *poll_strategy = NULL;
  grpc_test_init(argc, argv);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_iomgr_init(&exec_ctx);
  grpc_iomgr_start(&exec_ctx);

  poll_strategy = grpc_get_poll_strategy_name();
  if (poll_strategy != NULL && strcmp(poll_strategy, "epollsig") == 0) {
    test_add_fd_to_pollset();
    test_threading();
  } else {
    gpr_log(GPR_INFO,
            "Skipping the test. The test is only relevant for 'epollsig' "
            "strategy. and the current strategy is: '%s'",
            poll_strategy);
  }

  grpc_iomgr_shutdown(&exec_ctx);
  grpc_exec_ctx_finish(&exec_ctx);
  return 0;
}
#else /* defined(GRPC_LINUX_EPOLL) */
int main(int argc, char **argv) { return 0; }
#endif /* !defined(GRPC_LINUX_EPOLL) */
