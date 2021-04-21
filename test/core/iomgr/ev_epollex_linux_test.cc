/*
 *
 * Copyright 2018 gRPC authors.
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
#if defined(GRPC_LINUX_EPOLL_CREATE1) && defined(GRPC_LINUX_EVENTFD)
#include "src/core/lib/iomgr/ev_epollex_linux.h"

#include <grpc/grpc.h>
#include <string.h>
#include <sys/eventfd.h>

#include "test/core/util/test_config.h"

static void pollset_destroy(void* ps, grpc_error_handle /*error*/) {
  grpc_pollset_destroy(static_cast<grpc_pollset*>(ps));
  gpr_free(ps);
}

// This test is added to cover the case found in bug:
// https://github.com/grpc/grpc/issues/15760
static void test_pollable_owner_fd() {
  grpc_core::ExecCtx exec_ctx;
  int ev_fd1;
  int ev_fd2;
  grpc_fd* grpc_fd1;
  grpc_fd* grpc_fd2;
  grpc_pollset* ps;
  gpr_mu* mu;

  // == Create two grpc_fds ==
  // All we need is two file descriptors. Doesn't matter what type. We use
  // eventfd type here for the purpose of this test
  ev_fd1 = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  ev_fd2 = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (ev_fd1 < 0 || ev_fd2 < 0) {
    gpr_log(GPR_ERROR, "Error in creating event fds for the test");
    return;
  }
  grpc_fd1 = grpc_fd_create(ev_fd1, "epollex-test-fd1", false);
  grpc_fd2 = grpc_fd_create(ev_fd2, "epollex-test-fd2", false);
  grpc_core::ExecCtx::Get()->Flush();

  // == Create a pollset ==
  ps = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
  grpc_pollset_init(ps, &mu);
  grpc_core::ExecCtx::Get()->Flush();

  // == Add fd1 to pollset ==
  grpc_pollset_add_fd(ps, grpc_fd1);
  grpc_core::ExecCtx::Get()->Flush();

  // == Destroy fd1 ==
  grpc_fd_orphan(grpc_fd1, nullptr, nullptr, "test fd1 orphan");
  grpc_core::ExecCtx::Get()->Flush();

  // = Add fd2 to pollset ==
  //
  // Before https://github.com/grpc/grpc/issues/15760, the following line caused
  // unexpected behavior (The previous grpc_pollset_add_fd(ps, grpc_fd1) created
  // an underlying structure in epollex that held a reference to grpc_fd1 which
  // was being accessed here even after grpc_fd_orphan(grpc_fd1) was called
  grpc_pollset_add_fd(ps, grpc_fd2);
  grpc_core::ExecCtx::Get()->Flush();

  // == Destroy fd2 ==
  grpc_fd_orphan(grpc_fd2, nullptr, nullptr, "test fd2 orphan");
  grpc_core::ExecCtx::Get()->Flush();

  // == Destroy pollset
  grpc_closure ps_destroy_closure;
  GRPC_CLOSURE_INIT(&ps_destroy_closure, pollset_destroy, ps,
                    grpc_schedule_on_exec_ctx);
  grpc_pollset_shutdown(ps, &ps_destroy_closure);
  grpc_core::ExecCtx::Get()->Flush();
}

int main(int argc, char** argv) {
  const char* poll_strategy = nullptr;
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  {
    grpc_core::ExecCtx exec_ctx;
    poll_strategy = grpc_get_poll_strategy_name();
    if (poll_strategy != nullptr && strcmp(poll_strategy, "epollex") == 0) {
      test_pollable_owner_fd();
    } else {
      gpr_log(GPR_INFO,
              "Skipping the test. The test is only relevant for 'epollex' "
              "strategy. and the current strategy is: '%s'",
              poll_strategy);
    }
  }

  grpc_shutdown();
  return 0;
}
#else /* defined(GRPC_LINUX_EPOLL_CREATE1) && defined(GRPC_LINUX_EVENTFD) */
int main(int /*argc*/, char** /*argv*/) { return 0; }
#endif
