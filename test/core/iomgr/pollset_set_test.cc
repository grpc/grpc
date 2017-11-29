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
#include "src/core/lib/iomgr/port.h"

/* This test only relevant on linux systems where epoll is available */
#ifdef GRPC_LINUX_EPOLL

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "test/core/util/test_config.h"

/*******************************************************************************
 * test_pollset_set
 */

typedef struct test_pollset_set {
  grpc_pollset_set* pss;
} test_pollset_set;

void init_test_pollset_sets(test_pollset_set* pollset_sets, const int num_pss) {
  for (int i = 0; i < num_pss; i++) {
    pollset_sets[i].pss = grpc_pollset_set_create();
  }
}

void cleanup_test_pollset_sets(grpc_exec_ctx* exec_ctx,
                               test_pollset_set* pollset_sets,
                               const int num_pss) {
  for (int i = 0; i < num_pss; i++) {
    grpc_pollset_set_destroy(exec_ctx, pollset_sets[i].pss);
    pollset_sets[i].pss = nullptr;
  }
}

/*******************************************************************************
 * test_pollset
 */

typedef struct test_pollset {
  grpc_pollset* ps;
  gpr_mu* mu;
} test_pollset;

static void init_test_pollsets(test_pollset* pollsets, const int num_pollsets) {
  for (int i = 0; i < num_pollsets; i++) {
    pollsets[i].ps =
        static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    grpc_pollset_init(pollsets[i].ps, &pollsets[i].mu);
  }
}

static void destroy_pollset(grpc_exec_ctx* exec_ctx, void* p,
                            grpc_error* error) {
  grpc_pollset_destroy(exec_ctx, static_cast<grpc_pollset*>(p));
}

static void cleanup_test_pollsets(grpc_exec_ctx* exec_ctx,
                                  test_pollset* pollsets,
                                  const int num_pollsets) {
  grpc_closure destroyed;
  for (int i = 0; i < num_pollsets; i++) {
    GRPC_CLOSURE_INIT(&destroyed, destroy_pollset, pollsets[i].ps,
                      grpc_schedule_on_exec_ctx);
    grpc_pollset_shutdown(exec_ctx, pollsets[i].ps, &destroyed);

    grpc_exec_ctx_flush(exec_ctx);
    gpr_free(pollsets[i].ps);
    pollsets[i].ps = nullptr;
  }
}

/*******************************************************************************
 * test_fd
 */

typedef struct test_fd {
  grpc_fd* fd;
  grpc_wakeup_fd wakeup_fd;

  bool is_on_readable_called; /* Is on_readable closure is called ? */
  grpc_closure on_readable;   /* Closure to call when this fd is readable */
} test_fd;

void on_readable(grpc_exec_ctx* exec_ctx, void* tfd, grpc_error* error) {
  ((test_fd*)tfd)->is_on_readable_called = true;
}

static void reset_test_fd(grpc_exec_ctx* exec_ctx, test_fd* tfd) {
  tfd->is_on_readable_called = false;

  GRPC_CLOSURE_INIT(&tfd->on_readable, on_readable, tfd,
                    grpc_schedule_on_exec_ctx);
  grpc_fd_notify_on_read(exec_ctx, tfd->fd, &tfd->on_readable);
}

static void init_test_fds(grpc_exec_ctx* exec_ctx, test_fd* tfds,
                          const int num_fds) {
  for (int i = 0; i < num_fds; i++) {
    GPR_ASSERT(GRPC_ERROR_NONE == grpc_wakeup_fd_init(&tfds[i].wakeup_fd));
    tfds[i].fd = grpc_fd_create(GRPC_WAKEUP_FD_GET_READ_FD(&tfds[i].wakeup_fd),
                                "test_fd");
    reset_test_fd(exec_ctx, &tfds[i]);
  }
}

static void cleanup_test_fds(grpc_exec_ctx* exec_ctx, test_fd* tfds,
                             const int num_fds) {
  int release_fd;

  for (int i = 0; i < num_fds; i++) {
    grpc_fd_shutdown(exec_ctx, tfds[i].fd,
                     GRPC_ERROR_CREATE_FROM_STATIC_STRING("fd cleanup"));
    grpc_exec_ctx_flush(exec_ctx);

    /* grpc_fd_orphan frees the memory allocated for grpc_fd. Normally it also
     * calls close() on the underlying fd. In our case, we are using
     * grpc_wakeup_fd and we would like to destroy it ourselves (by calling
     * grpc_wakeup_fd_destroy). To prevent grpc_fd from calling close() on the
     * underlying fd, call it with a non-NULL 'release_fd' parameter */
    grpc_fd_orphan(exec_ctx, tfds[i].fd, nullptr, &release_fd,
                   false /* already_closed */, "test_fd_cleanup");
    grpc_exec_ctx_flush(exec_ctx);

    grpc_wakeup_fd_destroy(&tfds[i].wakeup_fd);
  }
}

static void make_test_fds_readable(test_fd* tfds, const int num_fds) {
  for (int i = 0; i < num_fds; i++) {
    GPR_ASSERT(GRPC_ERROR_NONE == grpc_wakeup_fd_wakeup(&tfds[i].wakeup_fd));
  }
}

static void verify_readable_and_reset(grpc_exec_ctx* exec_ctx, test_fd* tfds,
                                      const int num_fds) {
  for (int i = 0; i < num_fds; i++) {
    /* Verify that the on_readable callback was called */
    GPR_ASSERT(tfds[i].is_on_readable_called);

    /* Reset the tfd[i] structure */
    GPR_ASSERT(GRPC_ERROR_NONE ==
               grpc_wakeup_fd_consume_wakeup(&tfds[i].wakeup_fd));
    reset_test_fd(exec_ctx, &tfds[i]);
  }
}

/*******************************************************************************
 * Main tests
 */

/* Test some typical scenarios in pollset_set */
static void pollset_set_test_basic() {
  /* We construct the following structure for this test:
   *
   *        +---> FD0 (Added before PSS1, PS1 and PS2 are added to PSS0)
   *        |
   *        +---> FD5 (Added after PSS1, PS1 and PS2 are added to PSS0)
   *        |
   *        |
   *        |           +---> FD1 (Added before PSS1 is added to PSS0)
   *        |           |
   *        |           +---> FD6 (Added after PSS1 is added to PSS0)
   *        |           |
   *        +---> PSS1--+           +--> FD2 (Added before PS0 is added to PSS1)
   *        |           |           |
   *        |           +---> PS0---+
   *        |                       |
   * PSS0---+                       +--> FD7 (Added after PS0 is added to PSS1)
   *        |
   *        |
   *        |           +---> FD3 (Added before PS1 is added to PSS0)
   *        |           |
   *        +---> PS1---+
   *        |           |
   *        |           +---> FD8 (Added after PS1 added to PSS0)
   *        |
   *        |
   *        |           +---> FD4 (Added before PS2 is added to PSS0)
   *        |           |
   *        +---> PS2---+
   *                    |
   *                    +---> FD9 (Added after PS2 is added to PSS0)
   */
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_pollset_worker* worker;
  grpc_millis deadline;

  test_fd tfds[10];
  test_pollset pollsets[3];
  test_pollset_set pollset_sets[2];
  const int num_fds = GPR_ARRAY_SIZE(tfds);
  const int num_ps = GPR_ARRAY_SIZE(pollsets);
  const int num_pss = GPR_ARRAY_SIZE(pollset_sets);

  init_test_fds(&exec_ctx, tfds, num_fds);
  init_test_pollsets(pollsets, num_ps);
  init_test_pollset_sets(pollset_sets, num_pss);

  /* Construct the pollset_set/pollset/fd tree (see diagram above) */

  grpc_pollset_set_add_fd(&exec_ctx, pollset_sets[0].pss, tfds[0].fd);
  grpc_pollset_set_add_fd(&exec_ctx, pollset_sets[1].pss, tfds[1].fd);

  grpc_pollset_add_fd(&exec_ctx, pollsets[0].ps, tfds[2].fd);
  grpc_pollset_add_fd(&exec_ctx, pollsets[1].ps, tfds[3].fd);
  grpc_pollset_add_fd(&exec_ctx, pollsets[2].ps, tfds[4].fd);

  grpc_pollset_set_add_pollset_set(&exec_ctx, pollset_sets[0].pss,
                                   pollset_sets[1].pss);

  grpc_pollset_set_add_pollset(&exec_ctx, pollset_sets[1].pss, pollsets[0].ps);
  grpc_pollset_set_add_pollset(&exec_ctx, pollset_sets[0].pss, pollsets[1].ps);
  grpc_pollset_set_add_pollset(&exec_ctx, pollset_sets[0].pss, pollsets[2].ps);

  grpc_pollset_set_add_fd(&exec_ctx, pollset_sets[0].pss, tfds[5].fd);
  grpc_pollset_set_add_fd(&exec_ctx, pollset_sets[1].pss, tfds[6].fd);

  grpc_pollset_add_fd(&exec_ctx, pollsets[0].ps, tfds[7].fd);
  grpc_pollset_add_fd(&exec_ctx, pollsets[1].ps, tfds[8].fd);
  grpc_pollset_add_fd(&exec_ctx, pollsets[2].ps, tfds[9].fd);

  grpc_exec_ctx_flush(&exec_ctx);

  /* Test that if any FD in the above structure is readable, it is observable by
   * doing grpc_pollset_work on any pollset
   *
   *   For every pollset, do the following:
   *     - (Ensure that all FDs are in reset state)
   *     - Make all FDs readable
   *     - Call grpc_pollset_work() on the pollset
   *     - Flush the exec_ctx
   *     - Verify that on_readable call back was called for all FDs (and
   *       reset the FDs)
   * */
  for (int i = 0; i < num_ps; i++) {
    make_test_fds_readable(tfds, num_fds);

    gpr_mu_lock(pollsets[i].mu);
    deadline = grpc_timespec_to_millis_round_up(
        grpc_timeout_milliseconds_to_deadline(2));
    GPR_ASSERT(GRPC_ERROR_NONE ==
               grpc_pollset_work(&exec_ctx, pollsets[i].ps, &worker, deadline));
    gpr_mu_unlock(pollsets[i].mu);

    grpc_exec_ctx_flush(&exec_ctx);

    verify_readable_and_reset(&exec_ctx, tfds, num_fds);
    grpc_exec_ctx_flush(&exec_ctx);
  }

  /* Test tear down */
  grpc_pollset_set_del_fd(&exec_ctx, pollset_sets[0].pss, tfds[0].fd);
  grpc_pollset_set_del_fd(&exec_ctx, pollset_sets[0].pss, tfds[5].fd);
  grpc_pollset_set_del_fd(&exec_ctx, pollset_sets[1].pss, tfds[1].fd);
  grpc_pollset_set_del_fd(&exec_ctx, pollset_sets[1].pss, tfds[6].fd);
  grpc_exec_ctx_flush(&exec_ctx);

  grpc_pollset_set_del_pollset(&exec_ctx, pollset_sets[1].pss, pollsets[0].ps);
  grpc_pollset_set_del_pollset(&exec_ctx, pollset_sets[0].pss, pollsets[1].ps);
  grpc_pollset_set_del_pollset(&exec_ctx, pollset_sets[0].pss, pollsets[2].ps);

  grpc_pollset_set_del_pollset_set(&exec_ctx, pollset_sets[0].pss,
                                   pollset_sets[1].pss);
  grpc_exec_ctx_flush(&exec_ctx);

  cleanup_test_fds(&exec_ctx, tfds, num_fds);
  cleanup_test_pollsets(&exec_ctx, pollsets, num_ps);
  cleanup_test_pollset_sets(&exec_ctx, pollset_sets, num_pss);
  grpc_exec_ctx_finish(&exec_ctx);
}

/* Same FD added multiple times to the pollset_set tree */
void pollset_set_test_dup_fds() {
  /* We construct the following structure for this test:
   *
   *        +---> FD0
   *        |
   *        |
   * PSS0---+
   *        |           +---> FD0 (also under PSS0)
   *        |           |
   *        +---> PSS1--+           +--> FD1 (also under PSS1)
   *                    |           |
   *                    +---> PS ---+
   *                    |           |
   *                    |           +--> FD2
   *                    +---> FD1
   */
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_pollset_worker* worker;
  grpc_millis deadline;

  test_fd tfds[3];
  test_pollset pollset;
  test_pollset_set pollset_sets[2];
  const int num_fds = GPR_ARRAY_SIZE(tfds);
  const int num_ps = 1;
  const int num_pss = GPR_ARRAY_SIZE(pollset_sets);

  init_test_fds(&exec_ctx, tfds, num_fds);
  init_test_pollsets(&pollset, num_ps);
  init_test_pollset_sets(pollset_sets, num_pss);

  /* Construct the structure */
  grpc_pollset_set_add_fd(&exec_ctx, pollset_sets[0].pss, tfds[0].fd);
  grpc_pollset_set_add_fd(&exec_ctx, pollset_sets[1].pss, tfds[0].fd);
  grpc_pollset_set_add_fd(&exec_ctx, pollset_sets[1].pss, tfds[1].fd);

  grpc_pollset_add_fd(&exec_ctx, pollset.ps, tfds[1].fd);
  grpc_pollset_add_fd(&exec_ctx, pollset.ps, tfds[2].fd);

  grpc_pollset_set_add_pollset(&exec_ctx, pollset_sets[1].pss, pollset.ps);
  grpc_pollset_set_add_pollset_set(&exec_ctx, pollset_sets[0].pss,
                                   pollset_sets[1].pss);

  /* Test. Make all FDs readable and make sure that can be observed by doing a
   * grpc_pollset_work on the pollset 'PS' */
  make_test_fds_readable(tfds, num_fds);

  gpr_mu_lock(pollset.mu);
  deadline = grpc_timespec_to_millis_round_up(
      grpc_timeout_milliseconds_to_deadline(2));
  GPR_ASSERT(GRPC_ERROR_NONE ==
             grpc_pollset_work(&exec_ctx, pollset.ps, &worker, deadline));
  gpr_mu_unlock(pollset.mu);
  grpc_exec_ctx_flush(&exec_ctx);

  verify_readable_and_reset(&exec_ctx, tfds, num_fds);
  grpc_exec_ctx_flush(&exec_ctx);

  /* Tear down */
  grpc_pollset_set_del_fd(&exec_ctx, pollset_sets[0].pss, tfds[0].fd);
  grpc_pollset_set_del_fd(&exec_ctx, pollset_sets[1].pss, tfds[0].fd);
  grpc_pollset_set_del_fd(&exec_ctx, pollset_sets[1].pss, tfds[1].fd);

  grpc_pollset_set_del_pollset(&exec_ctx, pollset_sets[1].pss, pollset.ps);
  grpc_pollset_set_del_pollset_set(&exec_ctx, pollset_sets[0].pss,
                                   pollset_sets[1].pss);
  grpc_exec_ctx_flush(&exec_ctx);

  cleanup_test_fds(&exec_ctx, tfds, num_fds);
  cleanup_test_pollsets(&exec_ctx, &pollset, num_ps);
  cleanup_test_pollset_sets(&exec_ctx, pollset_sets, num_pss);
  grpc_exec_ctx_finish(&exec_ctx);
}

/* Pollset_set with an empty pollset */
void pollset_set_test_empty_pollset() {
  /* We construct the following structure for this test:
   *
   *        +---> PS0 (EMPTY)
   *        |
   *        +---> FD0
   *        |
   * PSS0---+
   *        |          +---> FD1
   *        |          |
   *        +---> PS1--+
   *                   |
   *                   +---> FD2
   */
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_pollset_worker* worker;
  grpc_millis deadline;

  test_fd tfds[3];
  test_pollset pollsets[2];
  test_pollset_set pollset_set;
  const int num_fds = GPR_ARRAY_SIZE(tfds);
  const int num_ps = GPR_ARRAY_SIZE(pollsets);
  const int num_pss = 1;

  init_test_fds(&exec_ctx, tfds, num_fds);
  init_test_pollsets(pollsets, num_ps);
  init_test_pollset_sets(&pollset_set, num_pss);

  /* Construct the structure */
  grpc_pollset_set_add_fd(&exec_ctx, pollset_set.pss, tfds[0].fd);
  grpc_pollset_add_fd(&exec_ctx, pollsets[1].ps, tfds[1].fd);
  grpc_pollset_add_fd(&exec_ctx, pollsets[1].ps, tfds[2].fd);

  grpc_pollset_set_add_pollset(&exec_ctx, pollset_set.pss, pollsets[0].ps);
  grpc_pollset_set_add_pollset(&exec_ctx, pollset_set.pss, pollsets[1].ps);

  /* Test. Make all FDs readable and make sure that can be observed by doing
   * grpc_pollset_work on the empty pollset 'PS0' */
  make_test_fds_readable(tfds, num_fds);

  gpr_mu_lock(pollsets[0].mu);
  deadline = grpc_timespec_to_millis_round_up(
      grpc_timeout_milliseconds_to_deadline(2));
  GPR_ASSERT(GRPC_ERROR_NONE ==
             grpc_pollset_work(&exec_ctx, pollsets[0].ps, &worker, deadline));
  gpr_mu_unlock(pollsets[0].mu);
  grpc_exec_ctx_flush(&exec_ctx);

  verify_readable_and_reset(&exec_ctx, tfds, num_fds);
  grpc_exec_ctx_flush(&exec_ctx);

  /* Tear down */
  grpc_pollset_set_del_fd(&exec_ctx, pollset_set.pss, tfds[0].fd);
  grpc_pollset_set_del_pollset(&exec_ctx, pollset_set.pss, pollsets[0].ps);
  grpc_pollset_set_del_pollset(&exec_ctx, pollset_set.pss, pollsets[1].ps);
  grpc_exec_ctx_flush(&exec_ctx);

  cleanup_test_fds(&exec_ctx, tfds, num_fds);
  cleanup_test_pollsets(&exec_ctx, pollsets, num_ps);
  cleanup_test_pollset_sets(&exec_ctx, &pollset_set, num_pss);
  grpc_exec_ctx_finish(&exec_ctx);
}

int main(int argc, char** argv) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_test_init(argc, argv);
  grpc_init();
  const char* poll_strategy = grpc_get_poll_strategy_name();

  if (poll_strategy != nullptr &&
      (strcmp(poll_strategy, "epollsig") == 0 ||
       strcmp(poll_strategy, "epoll-threadpool") == 0)) {
    pollset_set_test_basic();
    pollset_set_test_dup_fds();
    pollset_set_test_empty_pollset();
  } else {
    gpr_log(GPR_INFO,
            "Skipping the test. The test is only relevant for 'epoll' "
            "strategy. and the current strategy is: '%s'",
            poll_strategy);
  }

  grpc_exec_ctx_finish(&exec_ctx);
  grpc_shutdown();
  return 0;
}
#else  /* defined(GRPC_LINUX_EPOLL) */
int main(int argc, char** argv) { return 0; }
#endif /* !defined(GRPC_LINUX_EPOLL) */
