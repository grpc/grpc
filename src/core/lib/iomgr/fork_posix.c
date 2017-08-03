/*
 *
 * Copyright 2017 gRPC authors.
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

#ifdef GRPC_POSIX_FORK

#include <string.h>

#include <grpc/fork.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/iomgr/wakeup_fd_posix.h"
#include "src/core/lib/support/env.h"
#include "src/core/lib/support/fork.h"
#include "src/core/lib/support/thd_internal.h"

int grpc_prefork() {
  if (!grpc_fork_support_enabled()) {
    gpr_log(GPR_ERROR,
            "Fork support not enabled; try running with the "
            "environment variable GRPC_ENABLE_FORK_SUPPORT=1");
    return 0;
  }
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_timer_manager_set_threading(false);
  grpc_executor_set_threading(&exec_ctx, false);
  grpc_exec_ctx_finish(&exec_ctx);
  if (!gpr_await_threads(
          gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                       gpr_time_from_seconds(3, GPR_TIMESPAN)))) {
    gpr_log(GPR_ERROR, "gRPC thread still active! Cannot fork!");
    return 0;
  }
  return 1;
}

void grpc_postfork_parent() {
  grpc_timer_manager_set_threading(true);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_executor_set_threading(&exec_ctx, true);
  grpc_exec_ctx_finish(&exec_ctx);
}

void grpc_postfork_child() {
  grpc_timer_manager_set_threading(true);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_executor_set_threading(&exec_ctx, true);
  grpc_exec_ctx_finish(&exec_ctx);

  grpc_wakeup_fds_postfork();
  grpc_fork_engine();
}

#endif  // GRPC_POSIX_FORK
